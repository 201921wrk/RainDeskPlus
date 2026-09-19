/*
 * RainDeskPlus - Desktop beautification platform
 * Based on Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * M3 Smoke 验证入口：
 *   1) 在 exe 目录生成 smoke.ini（Time/CPU/Memory/Net/Disk 五 Measure + MeterText + Variables）
 *   2) ConfigParser 加载，断言段序 / 变量展开 / 数值读取
 *   3) 各 Measure Initialize → Update×3（间隔采样，让差分类 Measure 产出真值）
 *   4) MeterString 经 Direct2D/DirectWrite 离屏渲染文本：
 *      WIC 位图像素统计 → PNG 落盘（smoke_text.png 人工核验）
 *      D18-19 追加 MeterBar / MeterLine 离屏渲染（smoke_bar.png / smoke_line.png）
 *      D20-21 追加 MeasureWeather 离线骨架（缓存报文解析 / 过期 / 失败回退 / Key 解析）
 *      D22-23 追加 MeasureCalendar 离线骨架（整月网格 / 闰平年 / 周起点 / 今日标记）
 *      D24 追加 MeasureMedia 离线骨架（快照解析 / Field 产出 / 失败回退 / APPCOMMAND 映射）
 *      D25 追加 MeasureAudio 离线骨架（radix-2 FFT / 8 频段划分 / 电平峰值 / 失败回退）
 *      D26-30 追加皮肤 / 主题系统（[Theme] Name= → Themes\<名>.ini 低优先级并入；
 *             挂件取色/字号 + Dock 面板色/透明度统一由主题管辖，!Refresh 即切换）
 *   5) 断言结果写入 smoke_result.txt（GUI 子系统无 stdout，落盘供脚本断言）
 *   6) 全部通过返回 0，任一失败返回 1
 *
 * D16（DD-11：Dock 生命周期归 App，不进 CRainmeter）：
 *   在上述 Smoke 之后持有 DockWindow —— Start(Skins\<theme>\Dock.ini) 进消息循环，
 *   退出前 Stop() 回写配置。ON 线默认常驻（窗口一直可见，便于悬停放大的人工验收）；
 *   设置了 RAINDOCK_SMOKE_MS 则限时自动退出，保住 Smoke 的自动化契约。
 *
 * Build: 链接 RainDeskPlusCore (+RainDeskPlusDock)。
 */
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>   // netioapi.h 的 MIB_IF_TABLE2/GetIfTable2 由 _WS2IPDEF_ 解锁
#include <iphlpapi.h>
#include <d2d1.h>

#include "ConfigParser.h"
#include "MeasureTime.h"
#include "MeasureCalendar.h"
#include "MeasureCPU.h"
#include "MeasureMemory.h"
#include "MeasureNet.h"
#include "MeasureDisk.h"
#include "MeasureWeather.h"
#include "MeasureMedia.h"
#include "MeasureAudio.h"
#include "Rainmeter.h"
#include "MeterString.h"
#include "MeterBar.h"
#include "MeterLine.h"
#include "Canvas.h"
#include "Skin.h"
#include "MathParser.h"
#include "StringUtil.h"
#include "Pcre.h"        // D36-40 性能项 #1：pcre16_compile 计数断言锚点

#ifdef RAINDOCK_BUILD_DOCK
#include <cstdlib>       // wcstoul：解析 RAINDOCK_SMOKE_MS（D16）
#include "DockWindow.h"  // 由 CMake 在该 target 上定义 RAINDOCK_BUILD_DOCK（D16）
#endif

using namespace raindock;

namespace {

// ---------- 小工具 ----------

std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? dir.substr(0, slash + 1) : dir;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              out.data(), n, nullptr, nullptr);
    }
    return out;
}

bool WriteUtf8File(const std::wstring& path, const std::string& utf8)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(f);
}

// 读回整份文件字节，供「落盘字节格式」断言使用（D31-35）。
bool ReadUtf8File(const std::wstring& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) { out.clear(); return true; }
    out.resize(static_cast<size_t>(size));
    f.read(&out[0], static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

// ---------- Smoke 框架 ----------

struct SmokeLog
{
    std::string text;
    int pass = 0;
    int fail = 0;
    std::wstring outPath;   // 增量落盘：中途崩溃也不丢已记录日志

    void Record(bool ok, const char* name, const std::string& detail)
    {
        if (ok) ++pass; else ++fail;
        text += ok ? "PASS " : "FAIL ";
        text += name;
        text += " | ";
        text += detail;
        text += "\n";
        if (!outPath.empty()) WriteUtf8File(outPath, text);
    }
};

const char* kSmokeIni =
    "; RainDeskPlus M2 Smoke 测试配置\n"
    "[Variables]\n"
    "FontFace=Segoe UI\n"
    "\n"
    "[MeasureTime]\n"
    "Measure=Time\n"
    "Format=%Y-%m-%d %H:%M:%S\n"
    "FontFace=#FontFace#\n"
    "\n"
    "[MeasureCPU]\n"
    "Measure=CPU\n"
    "Processor=0\n"
    "\n"
    "[MeasureMemory]\n"
    "Measure=Memory\n"
    "Mode=UsedPercent\n"
    "\n"
    "[MeasureNet]\n"
    "Measure=Net\n"
    "Type=InOctets\n"
    "Cumulative=1\n"
    "Interface=Best\n"
    "\n"
    "[MeasureDisk]\n"
    "Measure=Disk\n"
    "Drive=C:\n"
    "Total=0\n"
    "\n"
    "[MeterBar]\n"
    "X=0\n"
    "Y=0\n"
    "W=220\n"
    "H=10\n"
    "BarOrientation=Horizontal\n"
    "BarColor=90,170,255,255\n"
    "SolidColor=255,255,255,48\n"
    "\n"
    "[MeterLine]\n"
    "X=0\n"
    "Y=0\n"
    "W=220\n"
    "H=40\n"
    "LineColor=255,120,160,255\n"
    "LineWidth=2\n"
    "LineCount=30\n"
    "AutoScale=1\n"
    "\n"
    "[MeterText]\n"
    "Meter=String\n"
    "X=10\n"
    "Y=10\n"
    "FontFace=#FontFace#\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=RainDeskPlus M3 | %1\n";

const char* kSkinIni =
    "; RainDeskPlus D20 Smoke 皮肤（CPU/内存/磁盘/网络 + 天气）\n"
    "[Rainmeter]\n"
    "Update=500\n"
    "\n"
    "[Variables]\n"
    "FontFace=Segoe UI\n"
    "\n"
    "[MeasureCPU]\n"
    "Measure=CPU\n"
    "Processor=0\n"
    "MinValue=0\n"
    "MaxValue=100\n"
    "\n"
    "[MeasureMemory]\n"
    "Measure=Memory\n"
    "Mode=UsedPercent\n"
    "\n"
    "[MeasureDisk]\n"
    "Measure=Disk\n"
    "Drive=C:\n"
    "Total=0\n"
    "\n"
    "[MeasureNet]\n"
    "Measure=Net\n"
    "Type=InOctets\n"
    "Interface=Best\n"
    "\n"
    "[MeasureWeather]\n"
    "Measure=Weather\n"
    "City=Beijing\n"
    "Units=metric\n"
    "; 相对路径 → 皮肤 @Resources 目录（Smoke 中由 SetResourcesPath 指定为 exe\\ResCache\\）\n"
    "CacheFile=smoke_weather_cache.json\n"
    "ExpireMinutes=0\n"
    "\n"
    "[MeterCPUText]\n"
    "Meter=String\n"
    "MeasureName=MeasureCPU\n"
    "X=10\n"
    "Y=8\n"
    "FontFace=#FontFace#\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=RainDeskPlus D18 | CPU: %1%\n"
    "\n"
    "[MeterCPUBar]\n"
    "Meter=Bar\n"
    "MeasureName=MeasureCPU\n"
    "X=10\n"
    "Y=34\n"
    "W=200\n"
    "H=10\n"
    "BarColor=90,170,255,255\n"
    "SolidColor=255,255,255,48\n"
    "\n"
    "[MeterDiskBar]\n"
    "Meter=Bar\n"
    "MeasureName=MeasureDisk\n"
    "X=10\n"
    "Y=50\n"
    "W=200\n"
    "H=10\n"
    "BarColor=255,190,90,255\n"
    "SolidColor=255,255,255,48\n"
    "\n"
    "[MeterNetLine]\n"
    "Meter=Line\n"
    "MeasureName=MeasureNet\n"
    "X=10\n"
    "Y=66\n"
    "W=200\n"
    "H=36\n"
    "LineColor=255,120,160,255\n"
    "LineWidth=2\n"
    "LineCount=20\n"
    "AutoScale=1\n"
    "\n"
    "[MeterWeatherText]\n"
    "Meter=String\n"
    "MeasureName=MeasureWeather\n"
    "X=10\n"
    "Y=104\n"
    "FontFace=#FontFace#\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=Weather: %1\n";

// D36-40 性能项 #1 夹具：唯一 Measure 带 IfMatch + IfMatchAction，表达式恒定
// → 每次 Update 都会进入 IfActions 分支，但正则只应编译一次（其余复用缓存）。
const char* kIfMatchIni =
    "; RainDeskPlus D36-40 IfMatch 正则缓存夹具\n"
    "[Rainmeter]\n"
    "Update=100\n"
    "\n"
    "[MeasureNow]\n"
    "Measure=Time\n"
    "Format=%H:%M:%S\n"
    "IfMatch=^\\d\\d:\\d\\d:\\d\\d$\n"
    "IfMatchAction=!SetVariable IfMatched 1\n"
    "\n"
    "[MeterNow]\n"
    "Meter=String\n"
    "MeasureName=MeasureNow\n"
    "X=0\n"
    "Y=0\n"
    "FontFace=Segoe UI\n"
    "FontSize=12\n"
    "FontColor=255,255,255,255\n"
    "Text=%1\n";

// D36-40 性能项 #2 夹具：静态画面。唯一 Measure 为离线 Weather（固定缓存报文
// → 每帧同值同串），唯一 Meter 为 String（文本相同 → SetText 早退 → 几何不变），
// 故视觉指纹逐帧恒定，脏检查应持续跳过 Clear+Render。
const char* kStableIni =
    "; RainDeskPlus D36-40 脏检查夹具（静态画面）\n"
    "[Rainmeter]\n"
    "Update=100\n"
    "\n"
    "[MeasureWeather]\n"
    "Measure=Weather\n"
    "City=Beijing\n"
    "Units=metric\n"
    "CacheFile=smoke_weather_cache.json\n"
    "ExpireMinutes=0\n"
    "\n"
    "[MeterWeatherText]\n"
    "Meter=String\n"
    "MeasureName=MeasureWeather\n"
    "X=10\n"
    "Y=8\n"
    "FontFace=Segoe UI\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=Weather: %1\n";

// D36-40 端到端：主题切换夹具。两份皮肤体逐字节相同，唯一差异是 [Theme] Name=
// 指向的 Themes\<名>.ini。Meter 文本写 #ThemeLabel#（由主题文件的 [Variables] 提供），
// 故「同一皮肤 + 不同主题 → MeterString::GetText() 不同」可直接观测到。
const char* kThemeBodyDark =
    "[Theme]\n"
    "Name=SmokeDark\n"
    "\n"
    "[MeterTheme]\n"
    "Meter=String\n"
    "X=0\n"
    "Y=0\n"
    "FontFace=Segoe UI\n"
    "FontSize=14\n"
    "FontColor=255,255,255,255\n"
    "Text=#ThemeLabel#\n";
const char* kThemeBodyLight =
    "[Theme]\n"
    "Name=SmokeLight\n"
    "\n"
    "[MeterTheme]\n"
    "Meter=String\n"
    "X=0\n"
    "Y=0\n"
    "FontFace=Segoe UI\n"
    "FontSize=14\n"
    "FontColor=255,255,255,255\n"
    "Text=#ThemeLabel#\n";

#ifdef RAINDOCK_BUILD_DOCK
// ---------- D16：Dock 宿主辅助 ----------

// 从 exe 目录逐级上溯（含自身，最多 5 级）查找 "Skins" 目录，返回其绝对路径。
// 与 Library/Rainmeter.cpp 的 AutoDetectSkinsRoot 同源：后者处于匿名 namespace
// （internal linkage），App 无法跨 TU 调用，故此处复刻同样的上溯规则。
std::wstring FindSkinsRoot()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!::GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return {};

    std::wstring dir(exePath);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    dir.resize(slash);   // 去掉文件名，得到 exe 目录

    for (int level = 0; level <= 5; ++level) {
        const std::wstring candidate = dir + L"\\Skins";
        const DWORD attr = ::GetFileAttributesW(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return candidate;
        }
        slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
    }
    return {};
}

// RAINDOCK_SMOKE_MS：返回常驻时限（毫秒）；未设置或非正数返回 0（表示常驻）。
DWORD GetSmokeTimeoutMs()
{
    wchar_t buf[32] = {};
    if (::GetEnvironmentVariableW(L"RAINDOCK_SMOKE_MS", buf, _countof(buf)) == 0) return 0;
    return static_cast<DWORD>(::wcstoul(buf, nullptr, 10));
}
#endif

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    // WIC 工厂（CoCreateInstance）需要 COM；STA 足够离屏渲染。
    const bool comOk = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    const std::wstring exeDir   = GetExeDir();
    const std::wstring iniPath  = exeDir + L"smoke.ini";
    const std::wstring outPath  = exeDir + L"smoke_result.txt";

    SmokeLog log;
    log.outPath = outPath;

    // ---------- 1) 生成 smoke.ini ----------
    const bool iniWritten = WriteUtf8File(iniPath, kSmokeIni);
    log.Record(iniWritten, "ini-write", iniWritten ? WideToUtf8(iniPath) : "ofstream failed");

    // ---------- 2) ConfigParser：加载 + 段序 + 变量展开 + 读取器 ----------
    ConfigParser parser;
    const bool loaded = parser.LoadFile(iniPath);
    log.Record(loaded, "parser-load", loaded ? "LoadFile ok" : "LoadFile failed");

    if (loaded) {
        // 段序保持（Ini 出现顺序）
        const auto sections = parser.GetSections();
        bool orderOk = (sections.size() >= 6) &&
                       (sections[0] == L"Variables") &&
                       (sections[1] == L"MeasureTime") &&
                       (sections[2] == L"MeasureCPU") &&
                       (sections[3] == L"MeasureMemory") &&
                       (sections[4] == L"MeasureNet") &&
                       (sections[5] == L"MeasureDisk");
        log.Record(orderOk, "parser-section-order",
                   "first6=Variables,Time,CPU,Memory,Net,Disk");

        // ReadString 自动 #Var# 展开
        const std::wstring face = parser.ReadString(L"MeasureTime", L"FontFace", L"");
        log.Record(face == L"Segoe UI", "parser-var-expand",
                   "FontFace=" + WideToUtf8(face));

        // ReplaceVariables + SetVariable 合并视图
        parser.SetVariable(L"Greet", L"hello");
        std::wstring expand = L"#Greet# world";
        parser.ReplaceVariables(expand);
        log.Record(expand == L"hello world", "parser-setvar",
                   "expand=" + WideToUtf8(expand));

        // 数值读取器
        const int proc = parser.ReadInt(L"MeasureCPU", L"Processor", -1);
        log.Record(proc == 0, "parser-read-int", "Processor=" + std::to_string(proc));
    }

    // ---------- 3) Measure 全链路 ----------
    // Time
    {
        MeasureTime time(nullptr, L"MeasureTime");
        time.Initialize(parser, iniPath);
        time.Update();
        const wchar_t* s = time.GetString();
        const std::wstring str(s ? s : L"");
        const bool ok = str.size() >= 19 && str.find(L'-') != std::wstring::npos
                        && str.find(L':') != std::wstring::npos;
        log.Record(ok, "measure-time", "GetString=" + WideToUtf8(str));
    }

    // CPU：3 次差分采样
    {
        MeasureCPU cpu(nullptr, L"MeasureCPU");
        cpu.Initialize(parser, iniPath);
        cpu.Update(); ::Sleep(400);
        cpu.Update(); ::Sleep(400);
        cpu.Update();
        const double v = cpu.GetValue();
        const bool ok = (v >= 0.0 && v <= 100.0);
        log.Record(ok, "measure-cpu", "value=" + std::to_string(v) + " (expect 0-100)");
    }

    // Memory：UsedPercent 模式
    {
        MeasureMemory mem(nullptr, L"MeasureMemory");
        mem.Initialize(parser, iniPath);
        mem.Update();
        const double v = mem.GetValue();
        const double maxV = mem.GetMaxValue();
        const bool ok = (v > 0.0 && v <= 100.0) && (maxV > 0.0);
        log.Record(ok, "measure-memory",
                   "value=" + std::to_string(v) + " max=" + std::to_string(maxV));
    }

    // Net：Cumulative InOctets（开机以来必有入流量）+ 接口级 dump 诊断
    {
        MeasureNet net(nullptr, L"MeasureNet");
        net.Initialize(parser, iniPath);
        net.Update();
        const double v = net.GetValue();
        const bool ok = (v > 0.0);
        log.Record(ok, "measure-net", "best-cumulative-in=" + std::to_string(v));

        // 诊断：dump GetIfTable2 有流量的非回环接口（验证 Best 选择数据源）
        PMIB_IF_TABLE2 ifTable = nullptr;
        if (::GetIfTable2(&ifTable) == NO_ERROR && ifTable) {
            for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
                const MIB_IF_ROW2& r = ifTable->Table[i];
                if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                if (r.InOctets == 0 && r.OutOctets == 0) continue;
                char row[256];
                std::snprintf(row, sizeof(row), "  ifidx=%lu in=%llu out=%llu oper=%d",
                              r.InterfaceIndex,
                              static_cast<unsigned long long>(r.InOctets),
                              static_cast<unsigned long long>(r.OutOctets),
                              static_cast<int>(r.OperStatus));
                log.text += row;
                log.text += "\n";
            }
            ::FreeMibTable(ifTable);
        }
    }

    // Disk：系统盘剩余空间（Total=0 → 剩余容量；GetMaxValue 恒为该盘总容量）
    {
        MeasureDisk disk(nullptr, L"MeasureDisk");
        disk.Initialize(parser, iniPath);
        disk.Update();
        const double freeBytes = disk.GetValue();
        const double totalBytes = disk.GetMaxValue();
        const wchar_t* ds = disk.GetString();
        const std::wstring str(ds ? ds : L"");
        // 人类可读串形如 "123.4 GB"：非空且以单位末字符 'B' 结尾
        const bool ok = (freeBytes > 0.0) && (totalBytes > 0.0) &&
                        (freeBytes <= totalBytes) && (!str.empty() && str.back() == L'B');
        log.Record(ok, "measure-disk",
                   "free=" + std::to_string(freeBytes) +
                   " total=" + std::to_string(totalBytes) +
                   " string=" + WideToUtf8(str));
    }

    // ---------- 3.5) M3：MeterString Direct2D/DirectWrite 离屏渲染 ----------
    {
        log.Record(comOk, "com-init", comOk ? "CoInitializeEx ok" : "CoInitializeEx failed");

        ID2D1RenderTarget* rt = nullptr;
        IWICBitmap* bmp = nullptr;
        const bool created = Canvas::CreateOffscreenTarget(400, 80, &rt, &bmp);
        log.Record(created, "canvas-offscreen",
                   created ? "WIC RT 400x80 created" : "CreateOffscreenTarget failed");

        if (created && rt && bmp && loaded) {
            // 文本来源：[MeterText] Text=%1 → MeasureTime 字符串（INI → Measure → Meter 全链路）
            MeasureTime time(nullptr, L"MeasureTime");
            time.Initialize(parser, iniPath);
            time.Update();

            MeterString ms(nullptr, L"MeterText");
            ms.Initialize(parser, &time);

            const int tw = ms.GetTextWidth(), th = ms.GetTextHeight();
            log.Record(tw > 0 && th > 0, "meterstring-metrics",
                       "text size=" + std::to_string(tw) + "x" + std::to_string(th) +
                       " text=" + WideToUtf8(ms.GetText()));

            rt->BeginDraw();
            rt->Clear(D2D1_COLOR_F{0, 0, 0, 0});   // 透明背景，靠 alpha 判定命中像素
            ms.Draw(rt);
            const HRESULT hrEnd = rt->EndDraw();
            log.Record(SUCCEEDED(hrEnd), "meterstring-enddraw",
                       SUCCEEDED(hrEnd) ? "EndDraw ok" : "EndDraw failed");

            // 像素检查：alpha 通道命中数（抗锯齿阈值 > 8）
            UINT w = 0, h = 0;
            bmp->GetSize(&w, &h);
            const UINT stride = w * 4;
            std::vector<uint8_t> buf(static_cast<size_t>(stride) * h);
            size_t lit = 0;
            bool copied = SUCCEEDED(bmp->CopyPixels(nullptr, stride,
                                                    static_cast<UINT>(buf.size()), buf.data()));
            if (copied) {
                for (size_t i = 3; i < buf.size(); i += 4) {
                    if (buf[i] > 8) ++lit;
                }
            }
            log.Record(copied && lit > 100, "meterstring-d2d-render",
                       "lit pixels=" + std::to_string(lit) +
                       (copied ? "" : " (CopyPixels failed)"));

            // PNG 落盘供人工核验渲染效果
            const std::wstring pngPath = exeDir + L"smoke_text.png";
            const bool saved = Canvas::SaveBitmapToPng(bmp, pngPath);
            log.Record(saved, "meterstring-save-png",
                       saved ? WideToUtf8(pngPath) : "SaveBitmapToPng failed");

            bmp->Release();
            rt->Release();
        }
    }

    // ---------- 3.6) D18-19：MeterBar / MeterLine 离屏渲染 ----------
    {
        ID2D1RenderTarget* rt = nullptr;
        IWICBitmap* bmp = nullptr;
        const bool created = Canvas::CreateOffscreenTarget(240, 60, &rt, &bmp);
        log.Record(created, "d18-offscreen",
                   created ? "WIC RT 240x60 created" : "CreateOffscreenTarget failed");

        if (created && rt && bmp && loaded) {
            // 统计 alpha 命中：>8 为可视像素；>200 为不透明填充（条/线本体，
            // 与 alpha=48 的底槽区分），据此证明填充确实画出来了。
            auto CountPixels = [](IWICBitmap* t, size_t& lit, size_t& solid) {
                UINT w = 0, h = 0;
                t->GetSize(&w, &h);
                const UINT stride = w * 4;
                std::vector<uint8_t> buf(static_cast<size_t>(stride) * h);
                lit = 0;
                solid = 0;
                if (FAILED(t->CopyPixels(nullptr, stride, static_cast<UINT>(buf.size()),
                                         buf.data()))) {
                    return false;
                }
                for (size_t i = 3; i < buf.size(); i += 4) {
                    if (buf[i] > 8) ++lit;
                    if (buf[i] > 200) ++solid;
                }
                return true;
            };

            // Bar：绑定 MeasureMemory（Mode=UsedPercent → 比值恒落在 (0,1)）
            {
                MeasureMemory mem(nullptr, L"MeasureMemory");
                mem.Initialize(parser, iniPath);
                mem.Update();

                MeterBar bar(nullptr, L"MeterBar");
                bar.Initialize(parser, &mem);
                bar.Update();

                rt->BeginDraw();
                rt->Clear(D2D1_COLOR_F{0, 0, 0, 0});
                bar.Draw(rt);
                const HRESULT hrEnd = rt->EndDraw();
                log.Record(SUCCEEDED(hrEnd), "meterbar-enddraw",
                           SUCCEEDED(hrEnd) ? "EndDraw ok" : "EndDraw failed");

                size_t lit = 0, solid = 0;
                const bool copied = CountPixels(bmp, lit, solid);
                log.Record(copied && lit > 100 && solid > 100, "meterbar-d2d-render",
                           "lit=" + std::to_string(lit) + " fill=" + std::to_string(solid) +
                           (copied ? "" : " (CopyPixels failed)"));

                const std::wstring pngPath = exeDir + L"smoke_bar.png";
                const bool saved = Canvas::SaveBitmapToPng(bmp, pngPath);
                log.Record(saved, "meterbar-save-png",
                           saved ? WideToUtf8(pngPath) : "SaveBitmapToPng failed");
            }

            // Line：无绑定 Measure，直接灌入 30 个递增样本后渲染（量程 = 峰值）
            {
                MeterLine line(nullptr, L"MeterLine");
                line.Initialize(parser, nullptr);
                for (int i = 0; i < 30; ++i) {
                    line.PushSample(i * 3.0);
                }

                rt->BeginDraw();
                rt->Clear(D2D1_COLOR_F{0, 0, 0, 0});
                line.Draw(rt);
                const HRESULT hrEnd = rt->EndDraw();
                log.Record(SUCCEEDED(hrEnd), "meterline-enddraw",
                           SUCCEEDED(hrEnd) ? "EndDraw ok" : "EndDraw failed");

                size_t lit = 0, solid = 0;
                const bool copied = CountPixels(bmp, lit, solid);
                log.Record(copied && lit > 100 && solid > 100, "meterline-d2d-render",
                           "lit=" + std::to_string(lit) + " stroke=" + std::to_string(solid) +
                           (copied ? "" : " (CopyPixels failed)"));

                const std::wstring pngPath = exeDir + L"smoke_line.png";
                const bool saved = Canvas::SaveBitmapToPng(bmp, pngPath);
                log.Record(saved, "meterline-save-png",
                           saved ? WideToUtf8(pngPath) : "SaveBitmapToPng failed");
            }

            bmp->Release();
            rt->Release();
        }
    }

    // ---------- 3.7) D20-21：MeasureWeather 离线骨架 ----------
    // 报文与配置全部落在 exe 目录（不污染源码树）；CacheFile 用绝对路径，
    // 本节因此不依赖皮肤上下文 —— 皮肤内相对路径解析由 §5 覆盖。
    {
        const std::wstring weatherIni  = exeDir + L"smoke_weather.ini";
        const std::wstring freshJson   = exeDir + L"smoke_weather_fresh.json";
        const std::wstring staleJson   = exeDir + L"smoke_weather_stale.json";
        const std::wstring mutableJson = exeDir + L"smoke_weather_mutable.json";
        const std::wstring absentJson  = exeDir + L"smoke_weather_absent.json";

        const long long nowSec = static_cast<long long>(std::time(nullptr));
        // 期望串：城市 + 温度（1 位小数 + 度数符号）+ 中文描述
        const std::wstring kMetricString = L"Beijing 23.5\u00B0C \u6674\u5929";

        // OWM /data/2.5/weather 报文子集；description 用中文，顺带验证 UTF-8 → UTF-16 全链路。
        auto MakeJson = [](long long dt) {
            return std::string("{\"main\":{\"temp\":23.5},\"name\":\"Beijing\","
                               "\"weather\":[{\"main\":\"Clear\",\"description\":\"晴天\"}],"
                               "\"dt\":") +
                   std::to_string(dt) + "}";
        };

        ::DeleteFileW(absentJson.c_str());   // 确保「缓存缺失」分支真的缺文件
        const bool filesOk = WriteUtf8File(freshJson, MakeJson(nowSec - 60)) &&
                             WriteUtf8File(staleJson, MakeJson(nowSec - 3600)) &&
                             WriteUtf8File(mutableJson, MakeJson(nowSec - 60));

        // 一次生成全部 Weather 段，覆盖 ApiKey 三种来源与过期/缺失/损坏等分支
        std::wstring ini;
        auto AddSection = [&ini](const wchar_t* name, const std::wstring& cache,
                                 const wchar_t* units, int expireMinutes,
                                 const wchar_t* apiKey) {
            ini += L"[";
            ini += name;
            ini += L"]\nMeasure=Weather\n";
            if (apiKey) { ini += L"ApiKey="; ini += apiKey; ini += L"\n"; }
            ini += L"Units=";
            ini += units;
            ini += L"\nCacheFile=";
            ini += cache;
            ini += L"\nExpireMinutes=";
            ini += std::to_wstring(expireMinutes);
            ini += L"\n\n";
        };
        AddSection(L"WeatherMetric",   freshJson,   L"metric",   30, L"smoke-test-placeholder");
        AddSection(L"WeatherImperial", freshJson,   L"imperial", 30, L"smoke-test-placeholder");
        AddSection(L"WeatherStale",    staleJson,   L"metric",   30, L"smoke-test-placeholder");
        AddSection(L"WeatherNoExpire", staleJson,   L"metric",    0, L"smoke-test-placeholder");
        AddSection(L"WeatherAbsent",   absentJson,  L"metric",   30, L"smoke-test-placeholder");
        AddSection(L"WeatherMutable",  mutableJson, L"metric",   30, L"smoke-test-placeholder");
        AddSection(L"WeatherEnvKey",   freshJson,   L"metric",   30, L"");       // 空值 → 环境变量兜底
        AddSection(L"WeatherNoKey",    freshJson,   L"metric",   30, nullptr);   // 无键 + 无环境变量

        const bool iniOk = WriteUtf8File(weatherIni, WideToUtf8(ini));
        log.Record(filesOk && iniOk, "weather-files-write",
                   "seed fresh/stale/mutable + smoke_weather.ini @ " + WideToUtf8(exeDir));

        ConfigParser wp;
        const bool wpLoaded = iniOk && wp.LoadFile(weatherIni);

        if (wpLoaded) {
            // metric：数值 = 报文 main.temp，字符串 = 「城市 温度 描述」
            {
                MeasureWeather w(nullptr, L"WeatherMetric");
                w.Initialize(wp, weatherIni);
                w.Update();
                const double v = w.GetValue();
                const wchar_t* s = w.GetString();
                const std::wstring str(s ? s : L"");
                log.Record(std::fabs(v - 23.5) < 1e-6, "weather-value-metric",
                           "value=" + std::to_string(v) + " (expect 23.5)");
                log.Record(str == kMetricString, "weather-string-metric",
                           "GetString=" + WideToUtf8(str));
                log.Record(w.HasApiKey(), "weather-apikey-ini",
                           w.HasApiKey() ? "ApiKey= 非空 → 采用 INI 值" : "INI Key 未生效");
            }

            // imperial：23.5°C → 74.3°F（同一报文，仅换算与单位不同）
            {
                MeasureWeather w(nullptr, L"WeatherImperial");
                w.Initialize(wp, weatherIni);
                w.Update();
                const double v = w.GetValue();
                const wchar_t* s = w.GetString();
                const std::wstring str(s ? s : L"");
                const bool ok = std::fabs(v - 74.3) < 1e-6 &&
                                str.find(L"\u00B0F") != std::wstring::npos;
                log.Record(ok, "weather-imperial",
                           "value=" + std::to_string(v) + " GetString=" + WideToUtf8(str));
            }

            // 过期：报文 dt 距今 60 分钟 > ExpireMinutes=30 → 追加 stale 提示，但数值照常
            {
                MeasureWeather w(nullptr, L"WeatherStale");
                w.Initialize(wp, weatherIni);
                w.Update();
                const wchar_t* s = w.GetString();
                const std::wstring str(s ? s : L"");
                const bool ok = str.find(L"cache stale") != std::wstring::npos &&
                                std::fabs(w.GetValue() - 23.5) < 1e-6;
                log.Record(ok, "weather-expire-stale",
                           "GetString=" + WideToUtf8(str) + " (expect 含 cache stale)");
            }

            // ExpireMinutes=0：同一过期报文不再提示（数值仍为真值）
            {
                MeasureWeather w(nullptr, L"WeatherNoExpire");
                w.Initialize(wp, weatherIni);
                w.Update();
                const wchar_t* s = w.GetString();
                const std::wstring str(s ? s : L"");
                const bool ok = str.find(L"cache stale") == std::wstring::npos &&
                                std::fabs(w.GetValue() - 23.5) < 1e-6;
                log.Record(ok, "weather-expire-disabled",
                           "GetString=" + WideToUtf8(str) + " (expect 无 stale)");
            }

            // 缓存缺失且从未成功过 → 数值 0、字符串落到基类的 "0.0"
            {
                MeasureWeather w(nullptr, L"WeatherAbsent");
                w.Initialize(wp, weatherIni);
                w.Update();
                const wchar_t* s = w.GetString();
                const std::wstring str(s ? s : L"");
                const bool ok = (w.GetValue() == 0.0) && (str == L"0.0");
                log.Record(ok, "weather-absent-first",
                           "value=" + std::to_string(w.GetValue()) +
                           " GetString=" + WideToUtf8(str) + " (expect 0 / 0.0)");
            }

            // 失败回退：先成功一次拿到基准，再依次投入坏 JSON / OWM 错误体 / 删文件，
            // 三次都必须沿用上一次成功值（数值 + 完整字符串），不得打回 0。
            {
                MeasureWeather w(nullptr, L"WeatherMutable");
                w.Initialize(wp, weatherIni);
                w.Update();
                const wchar_t* s0 = w.GetString();
                const std::wstring baseline(s0 ? s0 : L"");
                const bool firstOk = std::fabs(w.GetValue() - 23.5) < 1e-6 &&
                                     baseline == kMetricString;

                const auto StillBaseline = [&w, &baseline]() {
                    const wchar_t* s = w.GetString();
                    return std::fabs(w.GetValue() - 23.5) < 1e-6 &&
                           std::wstring(s ? s : L"") == baseline;
                };

                WriteUtf8File(mutableJson, "{ this is not json ]");
                w.Update();
                const std::wstring afterBad(w.GetString() ? w.GetString() : L"");
                log.Record(firstOk && StillBaseline(), "weather-fallback-badjson",
                           "GetString=" + WideToUtf8(afterBad) + " (expect 保留上次值)");

                WriteUtf8File(mutableJson, "{\"cod\":\"404\",\"message\":\"city not found\"}");
                w.Update();
                const std::wstring afterErr(w.GetString() ? w.GetString() : L"");
                log.Record(firstOk && StillBaseline(), "weather-fallback-errorbody",
                           "GetString=" + WideToUtf8(afterErr) + " (expect 保留上次值)");

                ::DeleteFileW(mutableJson.c_str());
                w.Update();
                const std::wstring afterGone(w.GetString() ? w.GetString() : L"");
                log.Record(firstOk && StillBaseline(), "weather-fallback-absent",
                           "GetString=" + WideToUtf8(afterGone) + " (expect 保留上次值)");
            }

            // Key 解析：INI 留空时读环境变量 OPENWEATHER_API_KEY；都没有则为无 Key
            {
                ::SetEnvironmentVariableW(L"OPENWEATHER_API_KEY", L"smoke-env-placeholder");
                MeasureWeather w(nullptr, L"WeatherEnvKey");
                w.Initialize(wp, weatherIni);
                log.Record(w.HasApiKey(), "weather-apikey-env",
                           w.HasApiKey() ? "ApiKey= 空 → 环境变量兜底生效" : "环境变量未生效");

                ::SetEnvironmentVariableW(L"OPENWEATHER_API_KEY", nullptr);   // 清除，还原无 Key 环境
                MeasureWeather w2(nullptr, L"WeatherNoKey");
                w2.Initialize(wp, weatherIni);
                log.Record(!w2.HasApiKey(), "weather-apikey-absent",
                           w2.HasApiKey() ? "无 Key 环境却报有 Key" : "无 INI 无环境变量 → 无 Key（符合预期）");

                // 无 Key 不影响离线骨架：缓存报文照常解析出真值
                w2.Update();
                const std::wstring str2(w2.GetString() ? w2.GetString() : L"");
                log.Record(std::fabs(w2.GetValue() - 23.5) < 1e-6 && str2 == kMetricString,
                           "weather-offline-without-key",
                           "value=" + std::to_string(w2.GetValue()) +
                           " GetString=" + WideToUtf8(str2));
            }
        }
    }

    // ---------- 3.8) D22-23：MeasureCalendar 整月网格 ----------
    // 固定 Year=/Month=/TimeStamp= 即可离线复现整月版式（不依赖运行时的“今天”）；
    // 沿用 §3.7 的「独立 INI + 直接构造 Measure」模式，与主 smoke.ini 的段序断言无关。
    {
        const std::wstring calIni = exeDir + L"smoke_calendar.ini";

        std::wstring ini;
        auto AddCalendar = [&ini](const wchar_t* name, const wchar_t* options) {
            ini += L"[";
            ini += name;
            ini += L"]\nMeasure=Calendar\n";
            ini += options;
            ini += L"\n";
        };
        // 闰年 / 平年 2 月：只关心天数
        AddCalendar(L"CalendarLeap",     L"Year=2024\nMonth=2\nHighlightToday=0");
        AddCalendar(L"CalendarPlain",    L"Year=2023\nMonth=2\nHighlightToday=0");
        // 周起点：2024-09-01 恰为周日 → WeekStart=0 时 1 日在首行首格，=1 时退到首行末格
        AddCalendar(L"CalendarSunFirst", L"Year=2024\nMonth=9\nWeekStart=0\nHighlightToday=0");
        AddCalendar(L"CalendarMonFirst", L"Year=2024\nMonth=9\nWeekStart=1\nHighlightToday=0");
        // 今日标记：1707998400 = 2024-02-15 12:00 UTC（±12h 时区内都仍落在 15 日）
        AddCalendar(L"CalendarToday",
                    L"TimeStamp=1707998400\nTimeZone=0\nDaylightSavingTime=0");
        // 固定年月与参考月不一致：不存在“今日”，不得残留标记
        AddCalendar(L"CalendarOtherMonth",
                    L"Year=2023\nMonth=2\nTimeStamp=1707998400\nTimeZone=0\nDaylightSavingTime=0");

        ConfigParser cp;
        const bool iniOk = WriteUtf8File(calIni, WideToUtf8(ini)) && cp.LoadFile(calIni);
        log.Record(iniOk, "calendar-files-write",
                   iniOk ? WideToUtf8(calIni) : "write/load failed");

        if (iniOk) {
            // 版式约定：每格 3 字符 + 列间 1 空格 → 每行 27 字符、共 7 行（表头 + 6 周）
            constexpr size_t kRowWidth = 27;

            auto LoadGrid = [&](const wchar_t* name) {
                MeasureCalendar c(nullptr, name);
                c.Initialize(cp, calIni);
                c.Update();
                return std::pair<double, std::wstring>(
                    c.GetValue(), std::wstring(c.GetString() ? c.GetString() : L""));
            };
            auto Header = [](const std::wstring& grid) {
                const size_t eol = grid.find(L'\n');
                return grid.substr(0, eol == std::wstring::npos ? grid.size() : eol);
            };
            auto Lines = [](const std::wstring& grid) {
                return grid.empty()
                           ? size_t{0}
                           : 1 + static_cast<size_t>(std::count(grid.begin(), grid.end(), L'\n'));
            };
            auto Marks = [](const std::wstring& grid) {
                return static_cast<size_t>(std::count(grid.begin(), grid.end(), L'*'));
            };

            {
                const auto leap = LoadGrid(L"CalendarLeap");
                const std::wstring header = Header(leap.second);
                log.Record(leap.first == 29.0, "calendar-leap-days",
                           "value=" + std::to_string(leap.first) + " (expect 2024-02 = 29)");
                log.Record(Lines(leap.second) == 7 && header.size() == kRowWidth,
                           "calendar-grid-shape",
                           "lines=" + std::to_string(Lines(leap.second)) +
                           " headerWidth=" + std::to_string(header.size()) +
                           " (expect 7 行 / 27 字符)");
            }
            {
                const auto plain = LoadGrid(L"CalendarPlain");
                log.Record(plain.first == 28.0, "calendar-plain-days",
                           "value=" + std::to_string(plain.first) + " (expect 2023-02 = 28)");
            }
            {
                const auto sun = LoadGrid(L"CalendarSunFirst");
                const std::wstring header = Header(sun.second);
                // 表头随 WeekStart 轮转：周日首 → Sun 开头，且 1 日紧贴首行首格
                const bool ok = header.compare(0, 3, L"Sun") == 0 &&
                                sun.second.compare(header.size() + 1, 3, L" 1 ") == 0;
                log.Record(ok, "calendar-weekstart-sunday",
                           "header=" + WideToUtf8(header) + " (expect Sun 首 / 1 日在首行首格)");
            }
            {
                const auto mon = LoadGrid(L"CalendarMonFirst");
                const std::wstring header = Header(mon.second);
                // 周一为首：1 日退到首行末格（第 7 格 → 字符偏移 24），首格留白
                const bool ok = header.compare(0, 3, L"Mon") == 0 &&
                                mon.second.compare(header.size() + 1, 3, L"   ") == 0 &&
                                mon.second.compare(header.size() + 1 + 24, 3, L" 1 ") == 0;
                log.Record(ok, "calendar-weekstart-monday",
                           "header=" + WideToUtf8(header) + " (expect Mon 首 / 1 日在首行末格)");
            }
            {
                const auto today = LoadGrid(L"CalendarToday");
                const bool ok = today.first == 29.0 && Marks(today.second) == 1 &&
                                today.second.find(L"15*") != std::wstring::npos;
                log.Record(ok, "calendar-today-mark",
                           "days=" + std::to_string(today.first) +
                           " marks=" + std::to_string(Marks(today.second)) +
                           " (expect 2024-02 仅 15* 一处标记)");
            }
            {
                const auto other = LoadGrid(L"CalendarOtherMonth");
                log.Record(other.first == 28.0 && Marks(other.second) == 0, "calendar-today-offmonth",
                           "days=" + std::to_string(other.first) +
                           " marks=" + std::to_string(Marks(other.second)) +
                           " (固定月≠参考月 → 无今日标记)");
            }
        }
    }

    // ---------- 3.9) D24：MeasureMedia 离线骨架 ----------
    // 快照与配置全部落在 exe 目录（不污染源码树）；StateFile 用绝对路径，
    // 本节因此不依赖皮肤上下文 —— 皮肤内相对路径解析由 §5 覆盖。
    // 控制命令只验证「命令名 → WM_APPCOMMAND」的映射，不真的广播：真广播会去操作
    // 开发机当前正在播放的音乐，不是离线回归该做的事。
    {
        const std::wstring mediaIni   = exeDir + L"smoke_media.ini";
        const std::wstring freshJson  = exeDir + L"smoke_media_fresh.json";
        const std::wstring lowerJson  = exeDir + L"smoke_media_lower.json";
        const std::wstring badJson    = exeDir + L"smoke_media_bad.json";
        const std::wstring errJson    = exeDir + L"smoke_media_error.json";
        const std::wstring mutJson    = exeDir + L"smoke_media_mutable.json";
        const std::wstring absentJson = exeDir + L"smoke_media_absent.json";

        // 快照口径：title / artist / album / state / position / duration（秒）
        const std::string kFresh =
            "{\"title\":\"Dawn Over the Harbor\",\"artist\":\"RainDesk Ensemble\","
            "\"album\":\"Ambient Sessions\",\"state\":\"Playing\","
            "\"position\":78,\"duration\":245}";
        // state 小写也要归一化成 Playing（大小写不敏感）
        const std::string kLower =
            "{\"title\":\"Lower Case State\",\"state\":\"playing\","
            "\"position\":0,\"duration\":0}";
        // 播放器错误体：能解析成 JSON 对象，但不含曲目/状态 → 视同无有效报文
        const std::string kErrorBody = "{\"cod\":\"404\",\"message\":\"not found\"}";

        ::DeleteFileW(absentJson.c_str());   // 确保「快照缺失」分支真的缺文件
        const bool filesOk = WriteUtf8File(freshJson, kFresh) &&
                             WriteUtf8File(lowerJson, kLower) &&
                             WriteUtf8File(badJson, "{ this is not json") &&
                             WriteUtf8File(errJson, kErrorBody) &&
                             WriteUtf8File(mutJson, kFresh);

        std::wstring ini;
        auto AddMedia = [&ini](const wchar_t* name, const std::wstring& state,
                               const wchar_t* field) {
            ini += L"[";
            ini += name;
            ini += L"]\nMeasure=Media\nStateFile=";
            ini += state;
            ini += L"\nField=";
            ini += field;
            ini += L"\n\n";
        };
        AddMedia(L"MediaTitle",     freshJson,  L"Title");
        AddMedia(L"MediaArtist",    freshJson,  L"Artist");
        AddMedia(L"MediaAlbum",     freshJson,  L"Album");
        AddMedia(L"MediaState",     freshJson,  L"State");
        AddMedia(L"MediaLower",     lowerJson,  L"State");
        AddMedia(L"MediaProgress",  freshJson,  L"Progress");
        AddMedia(L"MediaUnknown",   freshJson,  L"Whatever");     // 未知 Field → 回落 Title
        AddMedia(L"MediaAbsent",    absentJson, L"Title");
        AddMedia(L"MediaBadJson",   badJson,    L"Title");
        AddMedia(L"MediaErrorBody", errJson,    L"Title");
        AddMedia(L"MediaAbsentState", absentJson, L"State");      // 空形态 = Stopped
        AddMedia(L"MediaFallback",  mutJson,    L"Title");

        const bool iniOk = WriteUtf8File(mediaIni, WideToUtf8(ini));
        log.Record(filesOk && iniOk, "media-files-write",
                   "seed fresh/lower/bad/error/mutable + smoke_media.ini @ " + WideToUtf8(exeDir));

        ConfigParser mp;
        const bool mpLoaded = iniOk && mp.LoadFile(mediaIni);

        if (mpLoaded) {
            auto Load = [&](const wchar_t* name) {
                MeasureMedia m(nullptr, name);
                m.Initialize(mp, mediaIni);
                m.Update();
                return std::pair<double, std::wstring>(
                    m.GetValue(), std::wstring(m.GetString() ? m.GetString() : L""));
            };
            auto StringIs = [&](const wchar_t* name, const wchar_t* expect, const char* label) {
                const auto r = Load(name);
                log.Record(r.second == expect, label,
                           "GetString=" + WideToUtf8(r.second) + " (expect " +
                           WideToUtf8(expect) + ")");
            };

            StringIs(L"MediaTitle",   L"Dawn Over the Harbor", "media-title");
            StringIs(L"MediaArtist",  L"RainDesk Ensemble",    "media-artist");
            StringIs(L"MediaAlbum",   L"Ambient Sessions",     "media-album");
            StringIs(L"MediaState",   L"Playing",              "media-state");
            StringIs(L"MediaLower",   L"Playing",              "media-state-normalize");
            StringIs(L"MediaUnknown", L"Dawn Over the Harbor", "media-field-unknown");

            // 进度：字符串是「已播 / 总长」的 M:SS，数值是 0..1 归一化比；
            // 且量程必须被补成 MaxValue=1，否则 MeterBar 因 span<=0 恒取 0。
            {
                MeasureMedia m(nullptr, L"MediaProgress");
                m.Initialize(mp, mediaIni);
                m.Update();
                const std::wstring s(m.GetString() ? m.GetString() : L"");
                const bool ok = s == L"1:18 / 4:05" &&
                                std::fabs(m.GetValue() - 78.0 / 245.0) < 1e-9 &&
                                m.GetMaxValue() == 1.0 && m.GetMinValue() == 0.0;
                log.Record(ok, "media-progress",
                           "value=" + std::to_string(m.GetValue()) +
                           " max=" + std::to_string(m.GetMaxValue()) +
                           " GetString=" + WideToUtf8(s) + " (expect 1:18 / 4:05, max=1)");
            }

            // 失败回退：快照缺失 / JSON 畸形 / 播放器错误体 —— 从未成功过，取该字段空形态。
            // 空形态必须是 "-" 而非空串：空串会让基类 GetString() 回退成裸数值 "0.0"。
            {
                const auto absent = Load(L"MediaAbsent");
                const bool ok = absent.second == L"-" && absent.first == 0.0;
                log.Record(ok, "media-absent-fallback",
                           "GetString=" + WideToUtf8(absent.second) +
                           " value=" + std::to_string(absent.first) + " (expect - / 0)");
            }
            {
                const auto bad = Load(L"MediaBadJson");
                log.Record(bad.second == L"-", "media-badjson-fallback",
                           "GetString=" + WideToUtf8(bad.second) + " (expect -)");
            }
            {
                const auto err = Load(L"MediaErrorBody");
                log.Record(err.second == L"-", "media-errorbody-fallback",
                           "GetString=" + WideToUtf8(err.second) + " (expect -)");
            }
            {
                // 状态字段的空形态是 Stopped 而非占位 "-"：无会话时「没有在播」才是正确语义
                const auto st = Load(L"MediaAbsentState");
                log.Record(st.second == L"Stopped", "media-absent-state",
                           "GetString=" + WideToUtf8(st.second) + " (expect Stopped)");
            }
            {
                // 成功一次后源文件变坏 → 保住上一次成功值，不打回 0/占位
                MeasureMedia m(nullptr, L"MediaFallback");
                m.Initialize(mp, mediaIni);
                m.Update();
                const std::wstring first(m.GetString() ? m.GetString() : L"");
                const double firstValue = m.GetValue();

                WriteUtf8File(mutJson, "{ broken now");
                m.Update();
                const std::wstring second(m.GetString() ? m.GetString() : L"");
                const bool ok = first == L"Dawn Over the Harbor" && second == first &&
                                std::fabs(m.GetValue() - firstValue) < 1e-9;
                log.Record(ok, "media-keep-last-on-failure",
                           "before=" + WideToUtf8(first) + " after=" + WideToUtf8(second) +
                           " value=" + std::to_string(m.GetValue()) + " (expect 保持上一次)");
            }

            // 控制通道映射：WM_APPCOMMAND 的系统媒体指令固定四条（播放/暂停合一）
            {
                auto MapsTo = [](const wchar_t* cmd, WPARAM expect) {
                    WPARAM got = 0;
                    return MeasureMedia::ResolveAppCommand(cmd, got) && got == expect;
                };
                const bool ok =
                    MapsTo(L"Play", APPCOMMAND_MEDIA_PLAY_PAUSE) &&
                    MapsTo(L"pause", APPCOMMAND_MEDIA_PLAY_PAUSE) &&
                    MapsTo(L"Toggle", APPCOMMAND_MEDIA_PLAY_PAUSE) &&
                    MapsTo(L"NextTrack", APPCOMMAND_MEDIA_NEXTTRACK) &&
                    MapsTo(L"prev", APPCOMMAND_MEDIA_PREVIOUSTRACK) &&
                    MapsTo(L"Stop", APPCOMMAND_MEDIA_STOP);
                log.Record(ok, "media-appcommand-map",
                           ok ? "Play/Pause/Toggle + Next/Prev/Stop 映射正确"
                              : "映射不符（大小写不敏感语义被破坏）");

                WPARAM ignored = 0;
                log.Record(!MeasureMedia::ResolveAppCommand(L"Bogus", ignored),
                           "media-appcommand-unknown",
                           "未知命令返回 false（Command() 据此静默忽略）");
            }

            // 未识别命令：Command() 必须无副作用（不广播、不崩溃、读数不变）
            {
                MeasureMedia m(nullptr, L"MediaTitle");
                m.Initialize(mp, mediaIni);
                m.Update();
                m.Command(L"DefinitelyNotAMediaCommand");
                const std::wstring s(m.GetString() ? m.GetString() : L"");
                log.Record(s == L"Dawn Over the Harbor", "media-command-unknown-noop",
                           "GetString=" + WideToUtf8(s) + " (未知命令后读数不变)");
            }
        }
    }

    // ---------- 3.10) D25：MeasureAudio 频谱离线骨架 ----------
    // 与 §3.9 同一套路：快照与配置落在 exe 目录，AudioFile 用绝对路径绕开皮肤上下文
    // （皮肤内的相对路径解析由 Tests/test_skin_load.cpp 覆盖）。
    // 分两层验证：① Analyze() 直调（纯算法，不碰文件）；② Measure 全链路（解析 + 字段 + 回退）。
    {
        const std::wstring audioIni   = exeDir + L"smoke_audio.ini";
        const std::wstring toneJson   = exeDir + L"smoke_audio_tone.json";
        const std::wstring silentJson = exeDir + L"smoke_audio_silent.json";
        const std::wstring badJson    = exeDir + L"smoke_audio_bad.json";
        const std::wstring emptyJson  = exeDir + L"smoke_audio_empty.json";
        const std::wstring mutJson    = exeDir + L"smoke_audio_mutable.json";
        const std::wstring absentJson = exeDir + L"smoke_audio_absent.json";

        const double kPi = 3.14159265358979323846;
        const double kRate = 44100.0;

        // 生成一帧 PCM 快照：count 点单频正弦，保留 4 位小数（体积与精度够用）。
        auto MakeToneJson = [&](double freq, double amp, int count) {
            std::string s = "{\"sampleRate\":44100,\"samples\":[";
            for (int i = 0; i < count; ++i) {
                if (i) s += ',';
                char buf[32] = {};
                ::sprintf_s(buf, "%.4f", amp * std::sin(2.0 * kPi * freq * i / kRate));
                s += buf;
            }
            s += "]}";
            return s;
        };

        ::DeleteFileW(absentJson.c_str());   // 确保「快照缺失」分支真的缺文件
        const bool filesOk =
            WriteUtf8File(toneJson,   MakeToneJson(1000.0, 1.0, 1024)) &&
            WriteUtf8File(silentJson, MakeToneJson(1000.0, 0.0, 1024)) &&
            WriteUtf8File(badJson,    "{ this is not json") &&
            WriteUtf8File(emptyJson,  "{\"sampleRate\":44100,\"samples\":[]}") &&
            WriteUtf8File(mutJson,    MakeToneJson(1000.0, 1.0, 1024));

        std::wstring ini;
        auto AddAudio = [&ini](const wchar_t* name, const std::wstring& file, const wchar_t* field) {
            ini += L"[";
            ini += name;
            ini += L"]\nMeasure=Audio\nAudioFile=";
            ini += file;
            ini += L"\nField=";
            ini += field;
            ini += L"\n\n";
        };
        AddAudio(L"AudioToneBand5",      toneJson,   L"Band5");
        AddAudio(L"AudioToneBand1",      toneJson,   L"Band1");
        AddAudio(L"AudioToneBand5Lower", toneJson,   L"band5");   // 大小写不敏感
        AddAudio(L"AudioToneLevel",      toneJson,   L"Level");
        AddAudio(L"AudioTonePeak",       toneJson,   L"Peak");
        AddAudio(L"AudioUnknown",        toneJson,   L"Bogus");   // 未知 Field → 回落 Band1
        AddAudio(L"AudioSilent",         silentJson, L"Band5");
        AddAudio(L"AudioSilentLevel",    silentJson, L"Level");
        AddAudio(L"AudioBadJson",        badJson,    L"Band1");
        AddAudio(L"AudioEmpty",          emptyJson,  L"Band1");
        AddAudio(L"AudioAbsent",         absentJson, L"Band1");
        AddAudio(L"AudioFallback",       mutJson,    L"Level");
        // 显式量程优先于默认补的 MaxValue=1（换量纲时不被覆盖）
        ini += L"[AudioExplicitMax]\nMeasure=Audio\nAudioFile=";
        ini += toneJson;
        ini += L"\nField=Band5\nMaxValue=100\n\n";

        // ① 算法层：Analyze() 直调。断言只依赖定义（8 段几何级数划分 / 峰值 / 归一化电平），
        //    不依赖文件内容，因此能独立定位「算法错」还是「读盘/解析错」。
        {
            log.Record(MeasureAudio::kBandCount == 8 && MeasureAudio::kFftSize == 1024,
                       "audio-constants", "8 频段 / 1024 点窗（与 INI Field=Band1..Band8 对齐）");

            const int kN = MeasureAudio::kFftSize;
            std::vector<double> tone(kN);
            for (int i = 0; i < kN; ++i) tone[i] = std::sin(2.0 * kPi * 1000.0 * i / kRate);
            const std::vector<double> silence(kN, 0.0);

            const MeasureAudio::Frame tf = MeasureAudio::Analyze(tone.data(), tone.size(), kRate);
            const MeasureAudio::Frame sf = MeasureAudio::Analyze(silence.data(), silence.size(), kRate);

            // 满量程正弦：峰值≈1.0；RMS×√2 归一后电平≈1.0
            {
                const bool ok = std::fabs(tf.peak - 1.0) < 5e-3 && std::fabs(tf.level - 1.0) < 1e-2;
                log.Record(ok, "audio-analyze-tone-peak-level",
                           "peak=" + std::to_string(tf.peak) + " level=" + std::to_string(tf.level) +
                           " (expect ≈1.0 / ≈1.0)");
            }

            // 频段按几何级数平分 60Hz..12kHz：100/300/3000/9000Hz 分别落在 0/2/5/7 段
            {
                auto ArgMax = [](const MeasureAudio::Frame& f) {
                    std::size_t am = 0;
                    for (int b = 1; b < MeasureAudio::kBandCount; ++b) {
                        if (f.bands[static_cast<std::size_t>(b)] > f.bands[am]) am = static_cast<std::size_t>(b);
                    }
                    return am;
                };
                const double freqs[4]  = {100.0, 300.0, 3000.0, 9000.0};
                const std::size_t want[4] = {0, 2, 5, 7};
                bool mapOk = true;
                std::string detail;
                for (int t = 0; t < 4; ++t) {
                    std::vector<double> v(kN);
                    for (int i = 0; i < kN; ++i) v[i] = std::sin(2.0 * kPi * freqs[t] * i / kRate);
                    const MeasureAudio::Frame f = MeasureAudio::Analyze(v.data(), v.size(), kRate);
                    const std::size_t am = ArgMax(f);
                    detail += std::to_string(static_cast<int>(freqs[t])) + "Hz→" +
                              std::to_string(am) + "段 ";
                    if (am != want[t]) mapOk = false;
                }

                // 1000Hz 落在第 5 段（848.7..1645.9Hz），且段内取最强 bin 后接近满刻度
                const std::size_t am = ArgMax(tf);
                const bool toneOk = (am == 4) && tf.bands[4] > 0.8;
                log.Record(toneOk, "audio-analyze-tone-band",
                           "1000Hz→" + std::to_string(am) + "段 v=" + std::to_string(tf.bands[4]) +
                           " (expect 4 段 / >0.8)");
                log.Record(mapOk, "audio-analyze-band-mapping",
                           detail + "(expect 100→0 300→2 3000→5 9000→7)");
            }

            // 静音帧：三项全 0（而不是 NaN 或占位）
            {
                bool ok = sf.peak == 0.0 && sf.level == 0.0;
                for (int b = 0; b < MeasureAudio::kBandCount; ++b) {
                    if (sf.bands[static_cast<std::size_t>(b)] != 0.0) ok = false;
                }
                log.Record(ok, "audio-analyze-silence", "peak=0 level=0 各段=0");
            }

            // 短帧（不足一窗）：归一化基准取实际参与点数，电平不应被低估
            {
                const MeasureAudio::Frame hf = MeasureAudio::Analyze(tone.data(), 256, kRate);
                std::size_t am = 0;
                for (int b = 1; b < MeasureAudio::kBandCount; ++b) {
                    if (hf.bands[static_cast<std::size_t>(b)] > hf.bands[am]) am = static_cast<std::size_t>(b);
                }
                const bool ok = std::fabs(hf.level - 1.0) < 2e-2 && am == 4;
                log.Record(ok, "audio-analyze-short-frame",
                           "256点 level=" + std::to_string(hf.level) + " 最强段=" + std::to_string(am) +
                           " (expect ≈1.0 / 4 段)");
            }

            // 边界：空指针 / 零采样率不得产生 NaN 或越界（采样率非法时回落默认值）
            {
                const MeasureAudio::Frame nf = MeasureAudio::Analyze(nullptr, 0, 0.0);
                const MeasureAudio::Frame zf = MeasureAudio::Analyze(tone.data(), tone.size(), 0.0);
                const bool ok = nf.peak == 0.0 && nf.level == 0.0 && nf.bands[0] == 0.0 &&
                                std::isfinite(zf.level) && std::fabs(zf.level - 1.0) < 1e-2;
                log.Record(ok, "audio-analyze-guards",
                           "空帧全 0 且零采样率回落默认值（不产生 NaN）");
            }
        }

        const bool iniOk = WriteUtf8File(audioIni, WideToUtf8(ini));
        log.Record(filesOk && iniOk, "audio-files-write",
                   "seed tone/silent/bad/empty/mutable + smoke_audio.ini @ " + WideToUtf8(exeDir));

        // ② 全链路：Measure=Audio → ReadOptions → Update → GetValue/GetString
        ConfigParser ap;
        const bool apLoaded = iniOk && ap.LoadFile(audioIni);

        if (apLoaded) {
            auto Load = [&](const wchar_t* name) {
                MeasureAudio m(nullptr, name);
                m.Initialize(ap, audioIni);
                m.Update();
                return std::pair<double, std::wstring>(
                    m.GetValue(), std::wstring(m.GetString() ? m.GetString() : L""));
            };

            // 频段读数：值应为主能量（>0.8），字符串是百分比文本，量程被补成 0..1
            {
                MeasureAudio m(nullptr, L"AudioToneBand5");
                m.Initialize(ap, audioIni);
                m.Update();
                const std::wstring s(m.GetString() ? m.GetString() : L"");
                const bool ok = m.GetValue() > 0.8 && !s.empty() && s.back() == L'%' &&
                                m.GetMinValue() == 0.0 && m.GetMaxValue() == 1.0;
                log.Record(ok, "audio-band5",
                           "value=" + std::to_string(m.GetValue()) +
                           " max=" + std::to_string(m.GetMaxValue()) +
                           " GetString=" + WideToUtf8(s) + " (expect >0.8 / max=1 / 以%结尾)");
            }

            // Level / Peak 两个整帧字段（对应同一条 PCM 快照）
            {
                const auto lv = Load(L"AudioToneLevel");
                log.Record(std::fabs(lv.first - 1.0) < 1e-2, "audio-tone-level",
                           "value=" + std::to_string(lv.first) + " GetString=" + WideToUtf8(lv.second) +
                           " (expect ≈1.0)");
            }
            {
                const auto pk = Load(L"AudioTonePeak");
                log.Record(std::fabs(pk.first - 1.0) < 5e-3, "audio-tone-peak",
                           "value=" + std::to_string(pk.first) + " (expect ≈1.0)");
            }

            // Field= 大小写不敏感；未知 Field 回落 Band1（默认字段必须是能出条的那一个）
            {
                const auto lower   = Load(L"AudioToneBand5Lower");
                const auto upper   = Load(L"AudioToneBand5");
                const auto unknown = Load(L"AudioUnknown");
                const auto band1   = Load(L"AudioToneBand1");
                log.Record(lower.second == upper.second && !upper.second.empty(),
                           "audio-field-case-insensitive",
                           "band5=" + WideToUtf8(lower.second) + " Band5=" + WideToUtf8(upper.second));
                log.Record(unknown.second == band1.second && unknown.second != L"-",
                           "audio-field-unknown",
                           "Bogus=" + WideToUtf8(unknown.second) + " Band1=" + WideToUtf8(band1.second) +
                           " (expect 回落 Band1)");
            }

            // 显式 MaxValue= 不被默认值覆盖
            {
                MeasureAudio m(nullptr, L"AudioExplicitMax");
                m.Initialize(ap, audioIni);
                m.Update();
                log.Record(m.GetMaxValue() == 100.0 && m.GetValue() > 0.8, "audio-maxvalue-explicit",
                           "max=" + std::to_string(m.GetMaxValue()) + " value=" +
                           std::to_string(m.GetValue()) + " (expect max=100 保留)");
            }

            // 有效但静音 → "0%"（不是失败的 "-"）：两种情况对挂件的含义完全不同
            {
                const auto s  = Load(L"AudioSilent");
                const auto sl = Load(L"AudioSilentLevel");
                const bool ok = s.second == L"0%" && s.first == 0.0 && sl.second == L"0%";
                log.Record(ok, "audio-silent-is-zero-not-failure",
                           "band5=" + WideToUtf8(s.second) + " level=" + WideToUtf8(sl.second) +
                           " (expect 0%，与解析失败的 - 区分)");
            }

            // 失败回退：快照缺失 / JSON 畸形 / samples 为空 —— 从未成功过取占位 "-"
            // （不能留空串：基类 GetString() 会回退成裸数值 "0.0"）
            {
                const auto bad = Load(L"AudioBadJson");
                const auto emp = Load(L"AudioEmpty");
                const auto abs = Load(L"AudioAbsent");
                log.Record(bad.second == L"-" && bad.first == 0.0, "audio-badjson-fallback",
                           "GetString=" + WideToUtf8(bad.second) + " (expect -)");
                log.Record(emp.second == L"-", "audio-emptysamples-fallback",
                           "GetString=" + WideToUtf8(emp.second) + " (expect -)");
                log.Record(abs.second == L"-", "audio-absent-fallback",
                           "GetString=" + WideToUtf8(abs.second) + " (expect -)");
            }

            // 成功一次后快照变坏 → 保住上一次成功值，不打回占位
            {
                MeasureAudio m(nullptr, L"AudioFallback");
                m.Initialize(ap, audioIni);
                m.Update();
                const std::wstring first(m.GetString() ? m.GetString() : L"");
                const double firstValue = m.GetValue();

                WriteUtf8File(mutJson, "{ broken now");
                m.Update();
                const std::wstring second(m.GetString() ? m.GetString() : L"");
                const bool ok = !first.empty() && first != L"-" && second == first &&
                                std::fabs(m.GetValue() - firstValue) < 1e-9;
                log.Record(ok, "audio-keep-last-on-failure",
                           "before=" + WideToUtf8(first) + " after=" + WideToUtf8(second) +
                           " (expect 保持上一次)");
            }
        }
    }

    // ---------- 4) Common 批次：MathParser 公式求值 + StringUtil（上游直拷核心生效验证） ----------
    {
        const MathParser mathParser;
        double result = 0.0;
        const WCHAR* err = mathParser.CheckedParse(L"(2+3)*4+22/7", &result);
        // (2+3)*4 + 22/7 = 20 + 3.142857... = 23.142857...
        const bool mathOk = (err == nullptr) && (result > 23.14 && result < 23.15);
        log.Record(mathOk, "common-mathparser",
                   mathOk ? "formula=(2+3)*4+22/7 result=" + std::to_string(result)
                          : "parse failed");

        const bool strOk = StringUtil::EqualsIgnoreCase(L"MeasureTime", L"measuretime") &&
                           StringUtil::WidenUTF8("RainDeskPlus") == L"RainDeskPlus";
        log.Record(strOk, "common-stringutil",
                   strOk ? "EqualsIgnoreCase + WidenUTF8 ok" : "failed");
    }

    // ---------- 5) M4：Skin 加载 + 挂件窗口显示 ----------
    {
        const std::wstring skinIni = exeDir + L"smoke_skin.ini";
        const bool skinIniWritten = WriteUtf8File(skinIni, kSkinIni);
        log.Record(skinIniWritten, "skin-ini-write",
                   skinIniWritten ? WideToUtf8(skinIni) : "ofstream failed");

        // D20-21：皮肤的 CacheFile= 是相对路径，只在 @Resources 目录下放种子报文。
        // 若解析退回皮肤 INI 目录就找不到文件 → 天气值归零，故本项同时验证 @Resources 分支。
        const std::wstring weatherResDir = exeDir + L"ResCache\\";
        ::CreateDirectoryW(weatherResDir.c_str(), nullptr);
        const bool resCacheOk = WriteUtf8File(
            weatherResDir + L"smoke_weather_cache.json",
            "{\"main\":{\"temp\":23.5},\"name\":\"Beijing\","
            "\"weather\":[{\"main\":\"Clear\",\"description\":\"晴天\"}],"
            "\"dt\":1789531200}");

        Skin skin;
        skin.SetResourcesPath(weatherResDir);   // 相对 CacheFile 的解析基准
        const bool skinLoaded = skinIniWritten && resCacheOk && skin.Load(skinIni);
        // D20：CPU/Memory/Disk/Net/Weather 五 Measure，
        // String×2/Bar×2/Line 五 Meter（覆盖 MeasureWeather 与 @Resources 路径解析）
        const bool built = skinLoaded &&
                           skin.GetMeasures().size() == 5 &&
                           skin.GetMeters().size() == 5;
        log.Record(built, "skin-load", built ? "measures=5 meters=5" : "Load/build failed");

        const bool shown = built && skin.Show(hInstance, SW_SHOWNOACTIVATE);
        log.Record(shown, "skin-window-create",
                   shown ? "WS_POPUP + HwndRenderTarget ok" : "failed");

        const HWND hw = skin.GetWindow();
        const bool visible = hw && ::IsWindowVisible(hw);
        log.Record(visible, "skin-visible", visible ? "IsWindowVisible ok" : "not visible");

        // 消息循环 2.2s：全局 timer 到点 PostQuitMessage；皮肤自 timer 驱动帧。
        const UINT_PTR quitTimer = ::SetTimer(nullptr, 0, 2200, nullptr);
        MSG msg;
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == quitTimer) {
                ::KillTimer(nullptr, quitTimer);
                ::PostQuitMessage(0);
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        log.Record(skin.GetFrameCount() >= 3, "skin-frames",
                   "frames=" + std::to_string(skin.GetFrameCount()) + " (expect >=3)");
    }

    // ---------- 5) 骨架生命周期 API 不回归 ----------
    {
        auto& rainmeter = CRainmeter::GetInstance();
        const bool ok = rainmeter.Initialize(hInstance);
        log.Record(ok, "rainmeter-init", ok ? "CRainmeter::Initialize ok" : "failed");
        if (ok) rainmeter.Finalize();
    }

#ifdef RAINDOCK_BUILD_DOCK
    // ---------- 6) D16：Dock 接入真实 App（DD-11） ----------
    {
        const std::wstring skinsRoot = FindSkinsRoot();
        const std::wstring dockIni   = skinsRoot.empty()
            ? std::wstring()
            : skinsRoot + L"\\example\\Dock.ini";

        log.Record(!dockIni.empty(), "dock-config-path",
                   dockIni.empty() ? "未找到 Skins 目录（自 exe 上溯 5 级）"
                                   : WideToUtf8(dockIni));

        // D26-30：真实皮肤同样走主题系统——skin.ini 声明 [Theme] Name=Dark，
        // Themes\Dark.ini 提供与 D25 硬编码等价的取值，故取色应与旧基线完全一致（零视觉回归）。
        if (!skinsRoot.empty()) {
            ConfigParser skinCfg;
            const std::wstring skinIni = skinsRoot + L"\\example\\skin.ini";
            const bool skinOk   = skinCfg.LoadFile(skinIni);
            const std::wstring themeName = skinCfg.ReadString(L"Theme", L"Name", L"");
            const std::wstring barCpu    = skinCfg.ReadString(L"MeterCPUBar", L"BarColor", L"");
            const std::wstring fontColor = skinCfg.ReadString(L"MeterCPUText", L"FontColor", L"");
            log.Record(skinOk && themeName == L"Dark" && barCpu == L"90,170,255,255" &&
                           fontColor == L"255,255,255,255",
                       "theme-skin-dark-baseline",
                       "skin.ini 主题=" + WideToUtf8(themeName) + " BarColor=" + WideToUtf8(barCpu) +
                       " FontColor=" + WideToUtf8(fontColor) + " (Dark 基线)");
        }

        raindock::DockWindow dock;

#ifdef RAINDOCK_USE_DUILIB
        // Duilib 的 CPaintManagerUI 是进程级单例，建窗口前必须先 SetInstance
        // （与 Tests/test_dock_drop.cpp、test_dock_window.cpp 同源）。
        ::DuiLib::CPaintManagerUI::SetInstance(hInstance);
#endif

        const bool dockStarted = !dockIni.empty() && dock.Start(dockIni);
        log.Record(dockStarted, "dock-start",
                   dockStarted ? "DockWindow::Start ok（配置已加载）" : "Start failed");

        // D26-30：Dock 与挂件共用主题——面板色 / 透明度均来自 Themes\Dark.ini 的 [Colors]。
        if (dockStarted) {
            const DockBar& bar = dock.GetDockBar();
            log.Record(bar.GetThemeName() == L"Dark" && bar.GetPanelColor() == L"#40000000" &&
                           bar.GetTransparency() == 220,
                       "theme-dock-apply",
                       "Theme=" + WideToUtf8(bar.GetThemeName()) +
                       " PanelColor=" + WideToUtf8(bar.GetPanelColor()) +
                       " Transparency=" + std::to_string(bar.GetTransparency()));
        }

#ifdef RAINDOCK_USE_DUILIB
        if (dockStarted) {
            HWND hDock = dock.GetHWND();
            const bool visible = hDock != nullptr && ::IsWindowVisible(hDock) != FALSE;
            log.Record(visible, "dock-visible",
                       visible ? "Dock 窗口已显示（WS_EX_TOOLWINDOW + 分层透明）"
                               : "Dock 窗口不可见");

            // M4 皮肤窗口退出时可能残留 WM_QUIT，先清空队列再进常驻循环，
            // 否则下面的 PeekMessage 会立刻取到它而直接退出（常驻失效）。
            MSG msg{};
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) continue;
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }

            // 常驻：默认挂起直到 Dock 窗口被关闭；设了 RAINDOCK_SMOKE_MS 则到点退出。
            const DWORD timeoutMs = GetSmokeTimeoutMs();
            const DWORD startTick = ::GetTickCount();
            for (;;) {
                if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) break;
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                    continue;
                }
                if (hDock == nullptr || ::IsWindow(hDock) == FALSE) break;   // 窗口已关闭
                if (timeoutMs != 0 && ::GetTickCount() - startTick >= timeoutMs) break;

                // 阻塞等待新消息（无句柄 + 10ms 上限），避免忙等自旋。
                ::MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, 0);
            }
            log.Record(true, "dock-resident-exit",
                       timeoutMs != 0 ? "限时退出 " + std::to_string(timeoutMs) + "ms"
                                      : "常驻结束（Dock 窗口已关闭）");
        }
#endif

        dock.Stop();
        log.Record(dockStarted, "dock-stop",
                   dockStarted ? "Stop ok（Dock.ini + items.ini 已回写）" : "skipped");

        // D26-30：回写口径断言——[Theme] Name= 必须保留（否则重载后主题丢失），
        // 且 [Dock] 不再写 Transparency（透明度归主题管辖，写死会遮住主题）。
        if (dockStarted) {
            std::string dockText;
            {
                std::ifstream df(dockIni, std::ios::binary);
                if (df) {
                    df.seekg(0, std::ios::end);
                    const std::streamoff size = df.tellg();
                    df.seekg(0, std::ios::beg);
                    if (size > 0) {
                        dockText.resize(static_cast<size_t>(size));
                        df.read(&dockText[0], static_cast<std::streamsize>(size));
                    }
                }
            }
            const bool themeKept = dockText.find("[Theme]\r\nName=Dark") != std::string::npos;
            const bool noAlpha   = dockText.find("Transparency=") == std::string::npos;
            log.Record(themeKept && noAlpha, "theme-dock-save",
                       themeKept ? (noAlpha ? "[Theme] Name= 已保留，[Dock] 不写 Transparency"
                                            : "[Dock] 仍写 Transparency（会遮住主题）")
                                 : "Dock.ini 丢失 [Theme] Name=");
        }

#ifdef RAINDOCK_USE_DUILIB
        ::DuiLib::CPaintManagerUI::Term();
#endif
    }
#endif

    // ---------- 7) D26-30：皮肤 / 主题系统（挂件 + Dock 统一主题） ----------
    // 语义：主文件里的 [Theme] Name=<名> 会让同目录 Themes\<名>.ini 以「低优先级默认值」
    //       并入当前解析结果 —— 主文件显式写出的键优先，主题只补缺口；主题文件缺失时
    //       只告警降级，不阻断加载。切换主题 = 改 Name 后重载（对应 !Refresh）。
    // 夹具落在 exe 目录下，与 Skins\example 隔离，避免污染用户皮肤。
    {
        const std::wstring themeRoot = exeDir + L"smoke_themes\\";
        const std::wstring innerDir  = themeRoot + L"Themes";
        ::CreateDirectoryW(themeRoot.c_str(), nullptr);
        ::CreateDirectoryW(innerDir.c_str(), nullptr);

        const std::wstring mainIni    = themeRoot + L"main.ini";
        const std::wstring missingIni = themeRoot + L"missing.ini";
        const std::wstring themeA     = innerDir + L"\\SmokeA.ini";
        const std::wstring themeB     = innerDir + L"\\SmokeB.ini";

        // 主题 A：提供强调色 / 字号 / 面板色 / 透明度（全部为「默认值」性质）。
        const bool fixA = WriteUtf8File(themeA,
            "[Variables]\n"
            "Accent=11,22,33,255\n"
            "FontSizeBody=20\n"
            "\n"
            "[Colors]\n"
            "PanelColor=#11223344\n"
            "Transparency=180\n");
        // 主题 B：只换强调色与面板色，用于验证「改 Name 后重载即切换」。
        const bool fixB = WriteUtf8File(themeB,
            "[Variables]\n"
            "Accent=99,88,77,255\n"
            "FontSizeBody=20\n"
            "\n"
            "[Colors]\n"
            "PanelColor=#99887766\n"
            "Transparency=200\n");
        // 主文件：声明主题 A，并显式写出 FontSizeBody=13（应压过主题的 20）。
        const bool fixM = WriteUtf8File(mainIni,
            "[Theme]\n"
            "Name=SmokeA\n"
            "\n"
            "[Variables]\n"
            "FontSizeBody=13\n"
            "\n"
            "[MeterBar]\n"
            "BarColor=#Accent#\n");
        // 缺失主题的主文件：Name 指向不存在的主题，验证降级路径不阻断加载。
        const bool fixX = WriteUtf8File(missingIni,
            "[Theme]\n"
            "Name=DoesNotExist\n"
            "\n"
            "[Variables]\n"
            "Fallback=ok\n"
            "\n"
            "[MeterBar]\n"
            "BarColor=9,9,9,255\n");
        log.Record(fixA && fixB && fixM && fixX, "theme-fixture",
                   "临时主题夹具已写入 " + WideToUtf8(themeRoot));

        // 7.1 合并生效 + 变量展开贯通 + 主文件覆盖优先
        ConfigParser tp;
        const bool tpOk = tp.LoadFile(mainIni);
        log.Record(tpOk, "theme-load",
                   tpOk ? "LoadFile ok（[Theme] Name=SmokeA）" : "LoadFile failed");
        if (tpOk) {
            // 展开贯通：主文件写 #Accent#，读出的应是主题 A 提供的值。
            const std::wstring bar = tp.ReadString(L"MeterBar", L"BarColor", L"");
            log.Record(bar == L"11,22,33,255", "theme-merge-expand",
                       "BarColor(#Accent#)=" + WideToUtf8(bar) + " (expect 11,22,33,255)");

            // 主文件覆盖优先：同名键主文件 13、主题 20 → 取 13。
            const int fontSize = tp.ReadInt(L"Variables", L"FontSizeBody", -1);
            log.Record(fontSize == 13, "theme-main-wins",
                       "FontSizeBody=" + std::to_string(fontSize) + " (expect 13)");

            // 主题补齐缺口：[Colors] 仅主题提供 → 并入后可正常读取。
            const std::wstring panel = tp.ReadString(L"Colors", L"PanelColor", L"");
            const int alpha = tp.ReadInt(L"Colors", L"Transparency", -1);
            log.Record(panel == L"#11223344" && alpha == 180, "theme-fill",
                       "PanelColor=" + WideToUtf8(panel) +
                       " Transparency=" + std::to_string(alpha));

            // 7.2 !Refresh 切换语义：改写 [Theme] Name 后重新 LoadFile → 取色随主题变化。
            WriteUtf8File(mainIni,
                "[Theme]\n"
                "Name=SmokeB\n"
                "\n"
                "[Variables]\n"
                "FontSizeBody=13\n"
                "\n"
                "[MeterBar]\n"
                "BarColor=#Accent#\n");
            ConfigParser switched;
            const bool swOk = switched.LoadFile(mainIni);
            const std::wstring bar2   = switched.ReadString(L"MeterBar", L"BarColor", L"");
            const std::wstring panel2 = switched.ReadString(L"Colors", L"PanelColor", L"");
            log.Record(swOk && bar2 == L"99,88,77,255" && panel2 == L"#99887766",
                       "theme-switch",
                       "SmokeA→SmokeB：BarColor=" + WideToUtf8(bar2) +
                       " PanelColor=" + WideToUtf8(panel2));
        }

        // 7.3 主题缺失降级：LoadFile 仍成功；主文件键可用；主题本应提供的键回退调用方默认。
        ConfigParser mp;
        const bool mpOk = mp.LoadFile(missingIni);
        const std::wstring fallback = mp.ReadString(L"Variables", L"Fallback", L"");
        const std::wstring noPanel  = mp.ReadString(L"Colors", L"PanelColor", L"#none");
        log.Record(mpOk && fallback == L"ok" && noPanel == L"#none", "theme-missing-degrade",
                   "LoadFile=" + std::string(mpOk ? "ok" : "fail") +
                   " Fallback=" + WideToUtf8(fallback) +
                   " PanelColor=" + WideToUtf8(noPanel) + " (expect #none)");
    }

    // ---------- 8) D31-35：统一配置管理（ConfigParser 落盘 / 删除 / 裸读） ----------
    // 口径：SaveFile 输出 UTF-8 无 BOM + CRLF，段按出现顺序、段内键按插入顺序，
    //       首行注释经 SetHeaderComment 写回首部；LoadFileRaw 刻意不做主题合并。
    {
        const std::wstring cfgDir = exeDir + L"smoke_cfg\\";
        ::CreateDirectoryW(cfgDir.c_str(), nullptr);

        const std::wstring outIni = cfgDir + L"roundtrip.ini";
        // 段序 Zeta→Alpha 与键序 B→A 均与字典序相反，用于验证「保序」确实生效。
        {
            ConfigParser w;
            w.SetHeaderComment(L"; RainDeskPlus smoke cfg");
            w.SetValue(L"Zeta",  L"B", L"2");
            w.SetValue(L"Zeta",  L"A", L"1");
            w.SetValue(L"Alpha", L"K", L"v");
            const bool ok = w.SaveFile(outIni);
            log.Record(ok, "cfg-save-write", ok ? WideToUtf8(outIni) : "SaveFile failed");
        }

        // 8.1 落盘字节格式：首行注释 + 段序/键序 + CRLF（无 BOM 由首字节为 ';' 佐证）
        {
            std::string text;
            const bool readOk = ReadUtf8File(outIni, text);
            const std::string expect = "; RainDeskPlus smoke cfg\r\n"
                                       "[Zeta]\r\nB=2\r\nA=1\r\n"
                                       "\r\n[Alpha]\r\nK=v\r\n";
            log.Record(readOk && text == expect, "cfg-save-format",
                       "字节=" + text.substr(0, 96));
        }

        // 8.2 LoadFileRaw 往返：值/段序/首行注释一致；且先写入的脏数据被清空
        {
            ConfigParser r;
            r.SetValue(L"Zeta", L"A", L"dirty");
            const bool rOk = r.LoadFileRaw(outIni);
            const std::wstring za = r.ReadString(L"Zeta",  L"A", L"");
            const std::wstring zb = r.ReadString(L"Zeta",  L"B", L"");
            const std::wstring ak = r.ReadString(L"Alpha", L"K", L"");
            const std::wstring hdr = r.GetHeaderComment();
            const std::vector<std::wstring>& secs = r.GetSections();
            const bool orderOk = secs.size() == 2 && secs[0] == L"Zeta" && secs[1] == L"Alpha";
            log.Record(rOk && za == L"1" && zb == L"2" && ak == L"v" && orderOk &&
                       hdr == L"; RainDeskPlus smoke cfg",
                       "cfg-load-raw",
                       "Zeta.A=" + WideToUtf8(za) + " Zeta.B=" + WideToUtf8(zb) +
                       " Alpha.K=" + WideToUtf8(ak) + " 段序=" +
                       (orderOk ? "Zeta,Alpha" : "异常"));

            // 8.3 「读→改→写」：只改目标键，段序/键序/首行注释原样保留
            r.SetValue(L"Zeta", L"A", L"9");
            const std::wstring rewritten = cfgDir + L"rewrite.ini";
            const bool wOk = r.SaveFile(rewritten);
            std::string rtext;
            ReadUtf8File(rewritten, rtext);
            const std::string rexpect = "; RainDeskPlus smoke cfg\r\n"
                                        "[Zeta]\r\nB=2\r\nA=9\r\n"
                                        "\r\n[Alpha]\r\nK=v\r\n";
            log.Record(wOk && rtext == rexpect, "cfg-rewrite-preserve",
                       "字节=" + rtext.substr(0, 96));
        }

        // 8.4 RemoveValue：删键后该段变空则整段移除，落盘不留空段
        {
            ConfigParser d;
            d.LoadFileRaw(outIni);
            const bool delK = d.RemoveValue(L"Alpha", L"K");   // 唯一键 → 段随之消失
            const bool delB = d.RemoveValue(L"Zeta",  L"B");
            const bool delMiss = d.RemoveValue(L"Zeta", L"NotExist");
            d.SetValue(L"Zeta", L"A", L"1");
            const std::wstring delIni = cfgDir + L"del.ini";
            d.SaveFile(delIni);
            std::string dtext;
            ReadUtf8File(delIni, dtext);
            const std::string dexpect = "; RainDeskPlus smoke cfg\r\n[Zeta]\r\nA=1\r\n";
            log.Record(delK && delB && !delMiss && d.GetSections().size() == 1 &&
                       dtext == dexpect,
                       "cfg-remove-value",
                       "段数=" + std::to_string(d.GetSections().size()) + " 字节=" + dtext);

            // 8.5 RemoveSection：删段返回 true，重复删返回 false
            const bool delSec   = d.RemoveSection(L"Zeta");
            const bool delAgain = d.RemoveSection(L"Zeta");
            log.Record(delSec && !delAgain && d.GetSections().empty(), "cfg-remove-section",
                       std::string("首删=") + (delSec ? "true" : "false") +
                       " 重删=" + (delAgain ? "true" : "false"));
        }

        // 8.6 LoadFileRaw 不做主题合并：主文件缺 [Colors] 时应回退调用方默认，
        //     而 LoadFile（同文件）经主题并入可以读到值——这正是不污染主文件的前提。
        {
            const std::wstring thDir = cfgDir + L"Themes";
            ::CreateDirectoryW(thDir.c_str(), nullptr);
            const bool f1 = WriteUtf8File(thDir + L"\\SmokeRaw.ini",
                "[Colors]\r\nPanelColor=#abcdef\r\nTransparency=42\r\n");
            const std::wstring themed = cfgDir + L"themed.ini";
            const bool f2 = WriteUtf8File(themed,
                "[Theme]\r\nName=SmokeRaw\r\n\r\n[Variables]\r\nFallback=ok\r\n");

            ConfigParser mrg, raw;
            const bool mOk = mrg.LoadFile(themed);
            const bool rOk = raw.LoadFileRaw(themed);
            const std::wstring mv = mrg.ReadString(L"Colors", L"PanelColor", L"#none");
            const std::wstring rv = raw.ReadString(L"Colors", L"PanelColor", L"#none");
            const std::wstring rfb = raw.ReadString(L"Variables", L"Fallback", L"");
            log.Record(f1 && f2 && mOk && rOk && mv == L"#abcdef" && rv == L"#none" &&
                       rfb == L"ok",
                       "cfg-load-raw-no-merge",
                       "LoadFile→" + WideToUtf8(mv) + " LoadFileRaw→" + WideToUtf8(rv) +
                       " (expect #abcdef / #none)");
        }
    }

    // ---------- 10) D36-40：集成测试闭环 + 性能优化确定性断言 ----------
    // 口径：性能效果一律用「确定性计数」而非墙钟时间断言，便于纳入两线常态化回归：
    //   #1 正则编译次数（pcre16_compile 应恒为 1）；
    //   #2 脏检查跳过的重绘帧数（静态画面 renders 应恒为 0）。
    // 计数均为累计量，且 Skin::Load 不复位渲染计数，故一律取「前后差值」。
    {
        // 10.1 真实皮肤端到端：Skins\example\skin.ini 全量构建。
        //      Load 覆盖 ConfigParser 主题合并 + Measure/Meter 工厂 + MeasureName 绑定全链路；
        //      该皮肤实测 21 个 Measure、28 个 Meter（含 Media/Audio 等磁盘态数据源）。
#ifdef RAINDOCK_BUILD_DOCK
        {
            const std::wstring skinsRoot = FindSkinsRoot();
            const std::wstring realIni = skinsRoot.empty()
                ? std::wstring()
                : skinsRoot + L"\\example\\skin.ini";

            Skin realSkin;
            const bool realOk = !realIni.empty() && realSkin.Load(realIni);
            const size_t nMeasures = realSkin.GetMeasures().size();
            const size_t nMeters   = realSkin.GetMeters().size();
            log.Record(realOk && nMeasures == 21 && nMeters == 28, "e2e-real-skin",
                       realOk ? "measures=" + std::to_string(nMeasures) +
                                " meters=" + std::to_string(nMeters) + " (expect 21/28)"
                              : "Load 失败：" + WideToUtf8(realIni));
        }
#endif

        // 10.2 性能项 #1：IfMatch 正则编译缓存。
        //      夹具的 IfMatch 表达式恒定，Load 阶段只解析选项、不编译正则；
        //      随后连续 N 次 Update 都应命中同一份缓存 → pcre16_compile 恒为 1 次。
        {
            const std::wstring ifIni = exeDir + L"smoke_ifmatch.ini";
            const bool ifWritten = WriteUtf8File(ifIni, kIfMatchIni);

            Skin ifSkin;
            const bool ifLoaded = ifWritten && ifSkin.Load(ifIni);

            Pcre::ResetCompileCount();
            const int kUpdates = 25;
            for (int i = 0; i < kUpdates; ++i) {
                ifSkin.Update();
            }
            const uint64_t compiles = Pcre::GetCompileCount();
            log.Record(ifLoaded && compiles == 1, "perf-ifmatch-compile-once",
                       ifLoaded ? "updates=" + std::to_string(kUpdates) +
                                  " pcre16_compile=" + std::to_string(compiles) + " (expect 1)"
                                : "Load 失败：" + WideToUtf8(ifIni));
        }

        // 10.3 性能项 #2：脏检查（静态画面 → 跳过 Clear+Render）。
        //      夹具只含离线 Weather Measure（固定缓存报文 → 每帧同值同串）与一个 String
        //      Meter（文本相同 → SetText 早退 → 几何不变），故视觉指纹逐帧恒定：
        //      除 Show 首帧外不应再有任何实际绘制，每个定时器帧都应被跳过。
        {
            const std::wstring stableIni = exeDir + L"smoke_stable.ini";
            const bool stableWritten = WriteUtf8File(stableIni, kStableIni);

            Skin stable;
            stable.SetResourcesPath(exeDir + L"ResCache\\");   // 复用区段 5) 写入的种子报文
            const bool stableLoaded = stableWritten && stable.Load(stableIni);
            const bool stableShown  = stableLoaded && stable.Show(hInstance, SW_SHOWNOACTIVATE);

            uint32_t frames = 0;
            uint32_t renders = 0;
            uint32_t skipped = 0;
            if (stableShown) {
                const uint32_t r0 = stable.GetRenderCount();
                const uint32_t s0 = stable.GetSkippedRenderCount();
                const uint32_t f0 = stable.GetFrameCount();

                // 限时 1.5s 消息循环：皮肤自 timer 驱动帧（Update=100 → 约 14 帧）。
                // 用 PeekMessage + 限时而非 GetMessage + PostQuitMessage，避免队列中
                // 残留 WM_QUIT（区段 6) 常驻循环退出时可能留下）导致循环提前结束。
                const DWORD startTick = ::GetTickCount();
                MSG msg{};
                while (::GetTickCount() - startTick < 1500) {
                    if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        if (msg.message == WM_QUIT) continue;
                        ::TranslateMessage(&msg);
                        ::DispatchMessageW(&msg);
                        continue;
                    }
                    ::MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, 0);
                }

                frames  = stable.GetFrameCount() - f0;
                renders = stable.GetRenderCount() - r0;
                skipped = stable.GetSkippedRenderCount() - s0;
            }
            log.Record(stableShown && frames >= 3 && renders == 0 && skipped == frames,
                       "perf-dirty-skip-render",
                       "frames=" + std::to_string(frames) +
                       " renders=" + std::to_string(renders) +
                       " skipped=" + std::to_string(skipped) +
                       " (expect renders=0, skipped=frames)");

            // 10.4 !Redraw 经 Bang 路径强制重绘：即便画面无变化也必须实际绘制一帧。
            //      需先 Initialize 让 CRainmeter 装上 CommandHandler，否则 Bang 是 no-op。
            if (stableShown) {
                auto& rainmeter = CRainmeter::GetInstance();
                const bool rmOk = rainmeter.Initialize(hInstance);

                const uint32_t beforeBang = stable.GetRenderCount();
                stable.DoBang(L"!Redraw");
                const uint32_t afterBang = stable.GetRenderCount();

                log.Record(rmOk && afterBang == beforeBang + 1, "perf-bang-redraw",
                           "renders " + std::to_string(beforeBang) + "→" +
                           std::to_string(afterBang) + " (expect +1)");
                if (rmOk) rainmeter.Finalize();
            }
        }

        // 10.5 端到端主题切换：同一份皮肤体，改 [Theme] Name 后 Reload（等价 !Refresh）
        //      → 经变量展开后 MeterString::GetText() 随主题变化。
        {
            const std::wstring thRoot = exeDir + L"smoke_e2e_themes\\";
            const std::wstring thDir  = thRoot + L"Themes";
            ::CreateDirectoryW(thRoot.c_str(), nullptr);
            ::CreateDirectoryW(thDir.c_str(), nullptr);

            const bool fDark  = WriteUtf8File(thDir + L"\\SmokeDark.ini",
                                              "[Variables]\nThemeLabel=SMOKE-DARK\n");
            const bool fLight = WriteUtf8File(thDir + L"\\SmokeLight.ini",
                                              "[Variables]\nThemeLabel=SMOKE-LIGHT\n");
            const std::wstring bodyIni = thRoot + L"body.ini";
            const bool fBody  = WriteUtf8File(bodyIni, kThemeBodyDark);

            // 取第一个 Meter 的文本（夹具只含一个 MeterString）。
            auto firstMeterText = [](const Skin& s) -> std::wstring {
                if (s.GetMeters().empty()) return std::wstring();
                auto* ms = dynamic_cast<MeterString*>(s.GetMeters()[0].get());
                return ms ? ms->GetText() : std::wstring();
            };

            Skin thSkin;
            const bool darkOk   = fDark && fLight && fBody && thSkin.Load(bodyIni);
            const std::wstring darkText = firstMeterText(thSkin);

            // 切主题 = 改 [Theme] Name 后重载。
            const bool fSwitch   = WriteUtf8File(bodyIni, kThemeBodyLight);
            const bool reloadOk  = fSwitch && thSkin.Reload();
            const std::wstring lightText = firstMeterText(thSkin);

            log.Record(darkOk && darkText == L"SMOKE-DARK" &&
                       reloadOk && lightText == L"SMOKE-LIGHT",
                       "e2e-theme-switch-reload",
                       "Dark→" + WideToUtf8(darkText) + " Light→" + WideToUtf8(lightText) +
                       " (expect SMOKE-DARK / SMOKE-LIGHT)");
            log.Record(thSkin.GetIniPath() == bodyIni, "e2e-reload-ini-path",
                       "GetIniPath=" + WideToUtf8(thSkin.GetIniPath()));
        }
    }

    // ---------- 9) 汇总落盘 ----------
    char summary[64];
    std::snprintf(summary, sizeof(summary), "SMOKE SUMMARY PASS=%d FAIL=%d",
                  log.pass, log.fail);
    log.text += summary;
    log.text += "\n";

    const bool outWritten = WriteUtf8File(outPath, log.text);
    if (!outWritten) {
        std::fprintf(stderr, "[Smoke] cannot write %ls\n", outPath.c_str());
        Canvas::FinalizeAll();
        ::CoUninitialize();
        return 1;
    }
    Canvas::FinalizeAll();
    ::CoUninitialize();
    return (log.fail == 0) ? 0 : 1;
}
