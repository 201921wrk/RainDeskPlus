/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 媒体播放 Measure（契约与 INI 键见 MeasureMedia.h）。
 *
 * 数据流：StateFile= 指向的媒体会话快照 → FileUtil::ReadTextFile（BOM 判编码，
 * 无 BOM 按 UTF-8）→ JsonParser 解析 → 按 Field= 取字段 → m_StringValue；
 * position/duration 归一化后写入 m_Value 供 MeterBar 使用。
 *
 * 控制流：Command() → ResolveAppCommand() 归一化命令名 → SendMessageTimeout 广播
 * 到 HWND_BROADCAST。用 SMTO_ABORTIFHUNG + 200ms 超时，避免某个卡死的窗口把挂件
 * 的 UI 线程一起拖住。
 */
#include "MeasureMedia.h"
#include "ConfigParser.h"
#include "FileUtil.h"
#include "JsonParser.h"
#include "PathUtil.h"
#include "Skin.h"
#include "StringUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace raindock {

namespace {

// 字段缺失时的占位：不能让 m_StringValue 留空，否则基类会回退成裸数值。
const wchar_t* const kPlaceholder = L"-";

// 广播单条媒体指令的等待上限（毫秒）。
constexpr UINT kAppCommandTimeoutMs = 200;

// 一次解析的结果（媒体会话快照子集）。
struct MediaSample
{
    bool         valid = false;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring state;      // 归一化后的英文状态：Playing / Paused / Stopped
    double       position = 0.0;   // 秒
    double       duration = 0.0;   // 秒
};

// 解析快照。valid 的判据是「能解析成 JSON 对象，且至少描述了曲目或播放状态」；
// 播放器的错误体（如 {"cod":"404","message":"..."}）因此落到失败回退分支。
MediaSample ParseSample(const std::wstring& text)
{
    MediaSample sample;

    JsonValue root;
    if (!JsonParser::Parse(text, root)) return sample;

    // 字符串字段：类型不匹配视同缺失，不抛异常（JsonValue 的容错口径）。
    const auto ReadString = [&root](const wchar_t* key) -> std::wstring {
        const JsonValue* v = root.Find(key);
        return (v && v->IsString()) ? v->AsString() : std::wstring();
    };
    const auto ReadNumber = [&root](const wchar_t* key, double def) -> double {
        const JsonValue* v = root.Find(key);
        return (v && v->IsNumber()) ? v->AsNumber() : def;
    };

    sample.title  = ReadString(L"title");
    sample.artist = ReadString(L"artist");
    sample.album  = ReadString(L"album");

    // 状态归一化：快照可写 Playing/playing/PLAYING（大小写不敏感），未知值一律
    // 视为 Stopped —— 无播放会话时"没有在播"才是正确语义。
    const std::wstring rawState = ReadString(L"state");
    if (StringUtil::EqualsIgnoreCase(rawState, L"playing")) {
        sample.state = L"Playing";
    } else if (StringUtil::EqualsIgnoreCase(rawState, L"paused")) {
        sample.state = L"Paused";
    } else {
        sample.state = L"Stopped";
    }

    sample.position = ReadNumber(L"position", 0.0);
    sample.duration = ReadNumber(L"duration", 0.0);

    sample.valid = !sample.title.empty() || (root.Find(L"state") != nullptr);
    return sample;
}

// 秒 → "M:SS"（满一小时进位为 "H:MM:SS"）。非正数一律按 0 处理。
std::wstring FormatClock(double seconds)
{
    long long total = 0;
    if (seconds > 0.0) total = static_cast<long long>(seconds + 0.5);

    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;

    wchar_t buf[32] = {};
    if (h > 0) {
        ::swprintf_s(buf, L"%lld:%02lld:%02lld", h, m, s);
    } else {
        ::swprintf_s(buf, L"%lld:%02lld", m, s);
    }
    return buf;
}

// 命令名 → WM_APPCOMMAND 的 lParam 值。系统该通道只有四条指令，播放与暂停合一，
// 故 Play / Pause / Toggle 都映射到 APPCOMMAND_MEDIA_PLAY_PAUSE。
bool MapAppCommand(const std::wstring& command, WPARAM& appCommand)
{
    if (StringUtil::EqualsIgnoreCase(command, L"Play") ||
        StringUtil::EqualsIgnoreCase(command, L"Pause") ||
        StringUtil::EqualsIgnoreCase(command, L"PausePlay") ||
        StringUtil::EqualsIgnoreCase(command, L"PlayPause") ||
        StringUtil::EqualsIgnoreCase(command, L"Toggle")) {
        appCommand = APPCOMMAND_MEDIA_PLAY_PAUSE;
        return true;
    }
    if (StringUtil::EqualsIgnoreCase(command, L"Next") ||
        StringUtil::EqualsIgnoreCase(command, L"NextTrack")) {
        appCommand = APPCOMMAND_MEDIA_NEXTTRACK;
        return true;
    }
    if (StringUtil::EqualsIgnoreCase(command, L"Prev") ||
        StringUtil::EqualsIgnoreCase(command, L"Previous") ||
        StringUtil::EqualsIgnoreCase(command, L"PrevTrack")) {
        appCommand = APPCOMMAND_MEDIA_PREVIOUSTRACK;
        return true;
    }
    if (StringUtil::EqualsIgnoreCase(command, L"Stop")) {
        appCommand = APPCOMMAND_MEDIA_STOP;
        return true;
    }
    return false;
}

}  // namespace

MeasureMedia::MeasureMedia(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
    // 构造期不读盘：真值要等 ReadOptions 读完 StateFile= 才有意义
    // （与 MeasureWeather / MeasureDisk 的生命周期约定一致）。
}

std::wstring MeasureMedia::ResolveStatePath(const std::wstring& raw)
{
    if (raw.empty() || PathUtil::IsAbsolute(raw)) return raw;

    // 相对路径按「皮肤 @Resources → 皮肤 INI 目录」顺序解析。两者都没有时保留
    // 原样，交给 CreateFile 按进程工作目录解析（Smoke 直构 Measure 时走这条）。
    Skin* skin = GetSkin();
    if (skin) {
        const std::wstring& resources = skin->GetResourcesPath();
        if (!resources.empty()) return resources + raw;

        const std::wstring& iniPath = skin->GetIniPath();
        if (!iniPath.empty()) {
            return PathUtil::GetFolderFromFilePath(iniPath) + raw;
        }
    }
    return raw;
}

bool MeasureMedia::ResolveAppCommand(const std::wstring& command, WPARAM& appCommand)
{
    return MapAppCommand(command, appCommand);
}

void MeasureMedia::Command(const std::wstring& command)
{
    WPARAM appCommand = 0;
    if (!MapAppCommand(command, appCommand)) {
        return;   // 未识别的命令静默忽略（与未知 Bang 的处理一致）
    }

    // lParam 低位是 APPCOMMAND，高位是设备/按键标志位（此处给 0）。
    const LPARAM lParam = MAKELPARAM(0, appCommand);
    // 广播而非投递：媒体键语义要求同一条指令被所有顶层窗口看到，由系统/播放器
    // 各自决定是否处理。SMTO_ABORTIFHUNG 保证卡死窗口不会阻塞挂件的 UI 线程。
    ::SendMessageTimeoutW(HWND_BROADCAST, WM_APPCOMMAND, 0, lParam,
                          SMTO_ABORTIFHUNG | SMTO_NORMAL, kAppCommandTimeoutMs, nullptr);
}

void MeasureMedia::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    // 已解析过的绝对路径再走一次 ResolveStatePath 是幂等的（IsAbsolute 直接返回）。
    m_StateFile = ResolveStatePath(parser.ReadString(section, L"StateFile", m_StateFile));

    const std::wstring field = parser.ReadString(section, L"Field", L"Title");
    if (StringUtil::EqualsIgnoreCase(field, L"Artist")) {
        m_Field = Field::Artist;
    } else if (StringUtil::EqualsIgnoreCase(field, L"Album")) {
        m_Field = Field::Album;
    } else if (StringUtil::EqualsIgnoreCase(field, L"State")) {
        m_Field = Field::State;
    } else if (StringUtil::EqualsIgnoreCase(field, L"Progress")) {
        m_Field = Field::Progress;
    } else {
        // Title 及任何未知取值都按 Title 处理：默认字段必须是能反映"在放什么"的那个。
        m_Field = Field::Title;
    }

    // 进度是 0..1 归一化量纲，未显式给量程时补默认上限，否则 MeterBar 因
    // span<=0 恒取 0（MeterBar.cpp:71-74）。显式写了 MaxValue= 的用户配置优先。
    if (m_MaxValue <= m_MinValue) m_MaxValue = 1.0;
}

void MeasureMedia::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);

    // 重置回退基准：皮肤重新加载后不得沿用上一份皮肤的旧读数。
    m_HasValue = false;
    m_LastString.clear();
    m_Value = 0.0;
}

void MeasureMedia::RestoreLastValue()
{
    if (m_HasValue) {
        m_StringValue = m_LastString;
        return;
    }

    // 从未成功过：给出该字段的空形态。数值留在 0（m_Value 不被覆写即保持初值）。
    m_StringValue = [this]() -> std::wstring {
        switch (m_Field) {
        case Field::State:    return L"Stopped";
        case Field::Progress: return L"0:00 / 0:00";
        default:              return kPlaceholder;
        }
    }();
}

void MeasureMedia::UpdateValue()
{
    std::wstring text;
    if (m_StateFile.empty() || !FileUtil::ReadTextFile(m_StateFile, text)) {
        RestoreLastValue();   // 快照缺失/不可读
        return;
    }

    const MediaSample sample = ParseSample(text);
    if (!sample.valid) {
        RestoreLastValue();   // JSON 畸形或非媒体报文
        return;
    }

    // 进度：duration 非正时无刻度可言，取 0 而不是让条恒满。
    m_Value = (sample.duration > 0.0)
                  ? std::clamp(sample.position / sample.duration, 0.0, 1.0)
                  : 0.0;

    std::wstring out;
    switch (m_Field) {
    case Field::Artist: out = sample.artist.empty() ? kPlaceholder : sample.artist; break;
    case Field::Album:  out = sample.album.empty()  ? kPlaceholder : sample.album;  break;
    case Field::State:  out = sample.state; break;
    case Field::Progress:
        out = FormatClock(sample.position) + L" / " + FormatClock(sample.duration);
        break;
    case Field::Title:
    default:            out = sample.title.empty()  ? kPlaceholder : sample.title;  break;
    }

    m_StringValue = out;
    m_LastString  = out;
    m_HasValue    = true;
}

}  // namespace raindock
