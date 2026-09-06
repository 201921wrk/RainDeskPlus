/*
 * RainDeskPlus - Desktop beautification platform
 * Based on Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * M3 Smoke 验证入口：
 *   1) 在 exe 目录生成 smoke.ini（Time/CPU/Memory/Net 四 Measure + MeterText + Variables）
 *   2) ConfigParser 加载，断言段序 / 变量展开 / 数值读取
 *   3) 各 Measure Initialize → Update×3（间隔采样，让差分类 Measure 产出真值）
 *   4) MeterString 经 Direct2D/DirectWrite 离屏渲染文本：
 *      WIC 位图像素统计 → PNG 落盘（smoke_text.png 人工核验）
 *   5) 断言结果写入 smoke_result.txt（GUI 子系统无 stdout，落盘供脚本断言）
 *   6) 全部通过返回 0，任一失败返回 1
 *
 * Build: 链接 RainDeskPlusCore (+RainDeskPlusDock)。
 */
#include <cstdio>
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
#include "MeasureCPU.h"
#include "MeasureMemory.h"
#include "MeasureNet.h"
#include "Rainmeter.h"
#include "MeterString.h"
#include "Canvas.h"
#include "Skin.h"
#include "MathParser.h"
#include "StringUtil.h"

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
    "[MeterText]\n"
    "Meter=String\n"
    "X=10\n"
    "Y=10\n"
    "FontFace=#FontFace#\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=RainDeskPlus M3 | %1\n";

const char* kSkinIni =
    "; RainDeskPlus M4 Smoke 皮肤（Time 挂件）\n"
    "[Rainmeter]\n"
    "Update=500\n"
    "\n"
    "[Variables]\n"
    "FontFace=Segoe UI\n"
    "\n"
    "[MeasureTime]\n"
    "Measure=Time\n"
    "Format=%Y-%m-%d %H:%M:%S\n"
    "FontFace=#FontFace#\n"
    "\n"
    "[MeterText]\n"
    "Meter=String\n"
    "MeasureName=MeasureTime\n"
    "X=10\n"
    "Y=8\n"
    "FontFace=#FontFace#\n"
    "FontSize=16\n"
    "FontColor=255,255,255,255\n"
    "Text=RainDeskPlus M4 | %1\n";

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
        bool orderOk = (sections.size() >= 5) &&
                       (sections[0] == L"Variables") &&
                       (sections[1] == L"MeasureTime") &&
                       (sections[2] == L"MeasureCPU") &&
                       (sections[3] == L"MeasureMemory") &&
                       (sections[4] == L"MeasureNet");
        log.Record(orderOk, "parser-section-order", "first5=Variables,Time,CPU,Memory,Net");

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

        Skin skin;
        const bool skinLoaded = skinIniWritten && skin.Load(skinIni);
        const bool built = skinLoaded &&
                           skin.GetMeasures().size() == 1 &&
                           skin.GetMeters().size() == 1;
        log.Record(built, "skin-load", built ? "measures=1 meters=1" : "Load/build failed");

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

    // ---------- 7) 汇总落盘 ----------
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
