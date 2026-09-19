/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 天气 Measure（契约与 INI 键见 MeasureWeather.h）。
 *
 * 数据流：CacheFile= 指向的 OWM 当前天气报文 → FileUtil::ReadTextFile（BOM 判编码，
 * 无 BOM 按 UTF-8）→ JsonParser 解析 → 取 main.temp / name / weather[0] → 按 Units=
 * 换算 → m_Value（温度数值）+ m_StringValue（"城市 温度 描述"，供 MeterString 的 %1）。
 *
 * 报文时间戳口径：过期判定只看报文自带的 dt（unix 秒），不用文件 mtime —— 缓存被
 * 复制、同步或解包后 mtime 会失真，而 dt 是数据自身的时效。
 */
#include "MeasureWeather.h"
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

#include <ctime>
#include <iomanip>
#include <sstream>

namespace raindock {

namespace {

// 一次解析的结果（OWM /data/2.5/weather 响应子集）。
struct WeatherSample
{
    bool         valid = false;
    double       tempC = 0.0;    // main.temp（metric 口径即摄氏）
    std::wstring city;           // name
    std::wstring desc;           // weather[0].description（缺则退回 weather[0].main）
    bool         hasDt = false;  // 报文是否带 dt
    long long    dt    = 0;      // unix 秒
};

// 环境变量兜底读 API Key。
std::wstring ReadEnvApiKey()
{
    constexpr DWORD kMaxKeyLen = 128;
    WCHAR buf[kMaxKeyLen] = {};
    const DWORD len = ::GetEnvironmentVariableW(L"OPENWEATHER_API_KEY", buf, kMaxKeyLen);
    // len >= kMaxKeyLen 说明被截断：宁可判为「无 Key」，也不要拿半截 Key 去请求。
    if (len == 0 || len >= kMaxKeyLen) return {};
    return std::wstring(buf, len);
}

// 解析 OWM 报文。任何结构性缺失都返回 valid=false（调用方走失败回退）；OWM 的错误
// 响应体（{"cod":"404","message":"city not found"}）天然没有 main 对象，会落到这里。
WeatherSample ParseSample(const std::wstring& text)
{
    WeatherSample sample;

    JsonValue root;
    if (!JsonParser::Parse(text, root)) return sample;

    const JsonValue* main = root.Find(L"main");
    if (!main) return sample;
    const JsonValue* temp = main->Find(L"temp");
    if (!temp || !temp->IsNumber()) return sample;

    sample.tempC = temp->AsNumber();
    sample.valid = true;

    if (const JsonValue* name = root.Find(L"name")) {
        sample.city = name->AsString();
    }

    if (const JsonValue* weather = root.Find(L"weather")) {
        const JsonValue& first = weather->At(0);
        if (const JsonValue* desc = first.Find(L"description")) {
            sample.desc = desc->AsString();
        }
        if (sample.desc.empty()) {
            if (const JsonValue* group = first.Find(L"main")) {
                sample.desc = group->AsString();
            }
        }
    }

    if (const JsonValue* dt = root.Find(L"dt")) {
        if (dt->IsNumber()) {
            sample.dt = static_cast<long long>(dt->AsNumber());
            sample.hasDt = true;
        }
    }

    return sample;
}

// 温度 → "23.5°C" / "74.3°F"（用 UCN 写度数符号，不依赖源文件编码）。
std::wstring FormatTemp(double temp, bool imperial)
{
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1) << temp;
    return oss.str() + (imperial ? L"\u00B0F" : L"\u00B0C");
}

}  // namespace

MeasureWeather::MeasureWeather(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
    // 构造期不读盘：真值要等 ReadOptions 读完 CacheFile= 才有意义
    // （与 MeasureDisk 的生命周期约定一致）。
}

std::wstring MeasureWeather::ResolveCachePath(const std::wstring& raw)
{
    if (raw.empty() || PathUtil::IsAbsolute(raw)) return raw;

    // 相对路径按「皮肤 @Resources → 皮肤 INI 目录」顺序解析。两者都没有时保留原样，
    // 交给 CreateFile 按进程工作目录解析（Smoke 直接构造 Measure、皮肤为空时走这条）。
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

void MeasureWeather::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    m_ApiKey = parser.ReadString(section, L"ApiKey", L"");
    if (m_ApiKey.empty()) {
        m_ApiKey = ReadEnvApiKey();
    }

    m_City = parser.ReadString(section, L"City", m_City);

    const std::wstring units = parser.ReadString(section, L"Units", L"metric");
    m_Imperial = StringUtil::EqualsIgnoreCase(units, L"imperial");

    // 已解析过的绝对路径再走一次 ResolveCachePath 是幂等的（IsAbsolute 直接返回）。
    m_CacheFile = ResolveCachePath(parser.ReadString(section, L"CacheFile", m_CacheFile));
    m_ExpireMinutes = parser.ReadInt(section, L"ExpireMinutes", m_ExpireMinutes);
}

void MeasureWeather::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);

    // 重置回退基准：皮肤重新加载后不得沿用上一份皮肤的旧读数。
    m_HasValue = false;
    m_LastString.clear();
    m_Value = 0.0;
}

void MeasureWeather::RestoreLastValue()
{
    if (m_HasValue) m_StringValue = m_LastString;
}

void MeasureWeather::UpdateValue()
{
    std::wstring text;
    if (m_CacheFile.empty() || !FileUtil::ReadTextFile(m_CacheFile, text)) {
        RestoreLastValue();   // 缓存缺失/不可读
        return;
    }

    const WeatherSample sample = ParseSample(text);
    if (!sample.valid) {
        RestoreLastValue();   // 报错体或畸形报文
        return;
    }

    const double temp = m_Imperial ? (sample.tempC * 9.0 / 5.0 + 32.0) : sample.tempC;
    m_Value = temp;
    m_HasValue = true;

    bool stale = false;
    if (m_ExpireMinutes > 0 && sample.hasDt) {
        const long long now = static_cast<long long>(std::time(nullptr));
        stale = (now - sample.dt) > static_cast<long long>(m_ExpireMinutes) * 60;
    }

    const std::wstring& city = sample.city.empty() ? m_City : sample.city;

    std::wstring out;
    if (!city.empty()) {
        out += city;
        out += L' ';
    }
    out += FormatTemp(temp, m_Imperial);
    if (!sample.desc.empty()) {
        out += L' ';
        out += sample.desc;
    }
    if (stale) out += L" (cache stale)";

    m_StringValue = out;
    m_LastString  = out;
}

}  // namespace raindock
