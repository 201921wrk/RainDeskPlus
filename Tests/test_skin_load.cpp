/*
 * RainDeskPlus - M4 Skin 加载 Smoke 测试
 * 验证 Skin::Load 能正确解析 skin.ini 并实例化 Measure/Meter，
 * 且 Meter 能正确绑定 Measure 并渲染。
 * D26-30 追加：skin.ini 的 [Theme] Name= 主题合并（内置 Dark 基线）取的正是
 * D25 之前的硬编码配色，故主题化后应零视觉回归。
 */
#include "Skin.h"
#include "Canvas.h"
#include "ConfigParser.h"
#include "MeterString.h"
#include "MeterBar.h"
#include "MeterLine.h"
#include "Measure.h"

#include <algorithm>
#include <cstdio>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace raindock;

static bool g_Pass = true;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s\n", msg); \
        g_Pass = false; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while (0)

int main()
{
    printf("=== M4 Skin Loading Smoke Test ===\n\n");

    // ---- 1. 加载示例皮肤 ----
    printf("1. Load example skin\n");
    Skin skin;
    bool ok = skin.Load(L"Skins/example/skin.ini");

    if (!ok) {
        // 从 exe 位置推导项目根目录的绝对路径
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring exePath(path);
        size_t pos = exePath.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            std::wstring absPath = exePath.substr(0, pos + 1) + L"..\\..\\..\\Skins\\example\\skin.ini";
            ok = skin.Load(absPath);
        }
    }
    CHECK(ok, "Skin::Load succeeded");
    if (!ok) {
        printf("Cannot load skin, aborting.\n");
        Canvas::FinalizeAll();
        return 1;
    }
    printf("\n");

    // ---- 2. 验证 Measure ----
    printf("2. Measure verification\n");
    const auto& measures = skin.GetMeasures();
    CHECK(measures.size() == 21, "21 measures created (CPU/Memory/Disk/Net/Weather/Clock/Date/Weekday/Calendar/Media x4/Audio x8)");
    if (measures.size() == 21) {
        CHECK(wcscmp(measures[0]->GetName(), L"MeasureCPU") == 0, "measure[0] is MeasureCPU");
        CHECK(wcscmp(measures[1]->GetName(), L"MeasureMemory") == 0, "measure[1] is MeasureMemory");
        CHECK(wcscmp(measures[2]->GetName(), L"MeasureDisk") == 0, "measure[2] is MeasureDisk");
        CHECK(wcscmp(measures[3]->GetName(), L"MeasureNet") == 0, "measure[3] is MeasureNet");
        // D20：Weather 段由 Skin 工厂实例化为 MeasureWeather
        CHECK(wcscmp(measures[4]->GetName(), L"MeasureWeather") == 0, "measure[4] is MeasureWeather");
        // D22：Time 段 → MeasureTime（时钟/日期/星期三个实例）
        CHECK(wcscmp(measures[5]->GetName(), L"MeasureClock") == 0, "measure[5] is MeasureClock");
        CHECK(wcscmp(measures[6]->GetName(), L"MeasureDate") == 0, "measure[6] is MeasureDate");
        CHECK(wcscmp(measures[7]->GetName(), L"MeasureWeekday") == 0, "measure[7] is MeasureWeekday");
        CHECK(wcscmp(measures[8]->GetName(), L"MeasureCalendar") == 0, "measure[8] is MeasureCalendar");
        // D24：Media 段 → MeasureMedia（Title/Artist/State/Progress 四个实例）
        CHECK(wcscmp(measures[9]->GetName(), L"MeasureMedia") == 0, "measure[9] is MeasureMedia(Title)");
        CHECK(wcscmp(measures[10]->GetName(), L"MeasureMediaArtist") == 0, "measure[10] is MeasureMedia(Artist)");
        CHECK(wcscmp(measures[11]->GetName(), L"MeasureMediaState") == 0, "measure[11] is MeasureMedia(State)");
        CHECK(wcscmp(measures[12]->GetName(), L"MeasureMediaProgress") == 0, "measure[12] is MeasureMedia(Progress)");
        // D25：Audio 段 → MeasureAudio（8 个频段各一个实例）
        CHECK(wcscmp(measures[13]->GetName(), L"MeasureAudioBand1") == 0, "measure[13] is MeasureAudio(Band1)");
        CHECK(wcscmp(measures[14]->GetName(), L"MeasureAudioBand2") == 0, "measure[14] is MeasureAudio(Band2)");
        CHECK(wcscmp(measures[15]->GetName(), L"MeasureAudioBand3") == 0, "measure[15] is MeasureAudio(Band3)");
        CHECK(wcscmp(measures[16]->GetName(), L"MeasureAudioBand4") == 0, "measure[16] is MeasureAudio(Band4)");
        CHECK(wcscmp(measures[17]->GetName(), L"MeasureAudioBand5") == 0, "measure[17] is MeasureAudio(Band5)");
        CHECK(wcscmp(measures[18]->GetName(), L"MeasureAudioBand6") == 0, "measure[18] is MeasureAudio(Band6)");
        CHECK(wcscmp(measures[19]->GetName(), L"MeasureAudioBand7") == 0, "measure[19] is MeasureAudio(Band7)");
        CHECK(wcscmp(measures[20]->GetName(), L"MeasureAudioBand8") == 0, "measure[20] is MeasureAudio(Band8)");
    }
    if (!measures.empty()) {
        const auto& m = measures[0];
        m->Update();
        double val = m->GetValue();
        printf("  CPU value: %.1f%%\n", val);
        CHECK(val >= 0.0 && val <= 100.0, "CPU value in [0, 100]");
        const wchar_t* str = m->GetString();
        printf("  CPU string: %ls\n", str ? str : L"(null)");
        CHECK(str != nullptr && wcslen(str) > 0, "CPU string non-empty");

        // D18：磁盘 Measure —— 剩余容量落在 (0, 总容量]，且给出人类可读串
        for (const auto& dm : measures) {
            if (wcscmp(dm->GetName(), L"MeasureDisk") != 0) continue;
            dm->Update();
            const double freeBytes = dm->GetValue();
            const double totalBytes = dm->GetMaxValue();
            const wchar_t* dstr = dm->GetString();
            printf("  Disk free=%.0f total=%.0f string=%ls\n",
                   freeBytes, totalBytes, dstr ? dstr : L"(null)");
            CHECK(totalBytes > 0.0 && freeBytes > 0.0 && freeBytes <= totalBytes,
                  "disk free in (0, total]");
            CHECK(dstr != nullptr && wcslen(dstr) > 0, "disk string non-empty");
        }

        // D22：日历 Measure —— 整月网格（表头 + 6 行，靠 '\n' 分隔）
        for (const auto& cm : measures) {
            if (wcscmp(cm->GetName(), L"MeasureCalendar") != 0) continue;
            cm->Update();
            const wchar_t* cstr = cm->GetString();
            const std::wstring grid(cstr ? cstr : L"");
            const size_t lines = grid.empty() ? 0 : 1 + static_cast<size_t>(
                std::count(grid.begin(), grid.end(), L'\n'));
            printf("  Calendar days=%.0f lines=%zu\n", cm->GetValue(), lines);
            CHECK(cm->GetValue() >= 28.0 && cm->GetValue() <= 31.0, "calendar value = days in month");
            CHECK(lines == 7, "calendar grid = header + 6 week rows");
        }
    }
    printf("\n");

    // ---- 3. 验证 Meter ----
    printf("3. Meter verification\n");
    const auto& meters = skin.GetMeters();
    CHECK(meters.size() == 28, "28 meters created (15 String + 12 Bar + 1 Line)");
    if (!meters.empty()) {
        const auto& meter = meters[0];
        CHECK(wcscmp(meter->GetName(), L"MeterCPUText") == 0, "Meter name is MeterCPUText");
        printf("  Meter position: (%d, %d)\n", meter->GetX(), meter->GetY());
        CHECK(meter->GetX() == 20, "Meter X = 20");
        CHECK(meter->GetY() == 20, "Meter Y = 20");

        // 调用 Update 驱动 Measure 刷新
        skin.Update();

        // MeterString 有文本度量
        auto* ms = dynamic_cast<MeterString*>(meter.get());
        if (ms) {
            printf("  Text: %ls\n", ms->GetText().c_str());
            printf("  Text size: %d x %d\n", ms->GetTextWidth(), ms->GetTextHeight());
            CHECK(ms->GetTextWidth() > 0, "MeterString text width > 0");
            CHECK(ms->GetTextHeight() > 0, "MeterString text height > 0");
        } else {
            CHECK(false, "Meter is not MeterString");
        }
    }

    // D18：Bar / Line 已由 Skin 工厂按 Meter= 实例化（按段名定位后做类型校验）
    {
        Meter* barMeter = nullptr;
        Meter* lineMeter = nullptr;
        Meter* audioBandMeter = nullptr;
        for (const auto& mt : meters) {
            if (wcscmp(mt->GetName(), L"MeterCPUBar") == 0)  barMeter  = mt.get();
            if (wcscmp(mt->GetName(), L"MeterNetLine") == 0) lineMeter = mt.get();
            // D25：频谱挂件复用 MeterBar，只是换成竖向
            if (wcscmp(mt->GetName(), L"MeterAudioBand1") == 0) audioBandMeter = mt.get();
        }
        CHECK(dynamic_cast<MeterBar*>(barMeter) != nullptr, "MeterCPUBar is a MeterBar");
        CHECK(dynamic_cast<MeterLine*>(lineMeter) != nullptr, "MeterNetLine is a MeterLine");
        CHECK(dynamic_cast<MeterBar*>(audioBandMeter) != nullptr, "MeterAudioBand1 is a MeterBar");
    }
    printf("\n");

    // ---- 4. 主题系统（D26-30）----
    printf("4. Theme merge\n");
    {
        // 与加载皮肤用同一套路径解析，保证断言的是真正生效的那个 skin.ini。
        std::wstring skinIni = L"Skins/example/skin.ini";
        if (::GetFileAttributesW(skinIni.c_str()) == INVALID_FILE_ATTRIBUTES) {
            wchar_t path[MAX_PATH] = {};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            const std::wstring exePath(path);
            const size_t pos = exePath.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
                skinIni = exePath.substr(0, pos + 1) + L"..\\..\\..\\Skins\\example\\skin.ini";
        }
        printf("  skin.ini = %ls\n", skinIni.c_str());

        ConfigParser cfg;
        const bool cfgOk = cfg.LoadFile(skinIni);
        CHECK(cfgOk, "skin.ini 经 ConfigParser 加载（含主题合并）");

        const std::wstring themeName = cfg.ReadString(L"Theme", L"Name", L"");
        CHECK(themeName == L"Dark", "[Theme] Name=Dark 声明可读");

        // 下列变量 skin.ini 自身未定义，只可能来自 Themes\Dark.ini —— 读得出即证明合并生效。
        const std::wstring cpuBar  = cfg.ReadString(L"MeterCPUBar",   L"BarColor",  L"");
        const std::wstring cpuFont = cfg.ReadString(L"MeterCPUText",  L"FontColor", L"");
        const std::wstring band1   = cfg.ReadString(L"MeterAudioBand1", L"BarColor", L"");
        printf("  MeterCPUBar.BarColor=%ls  MeterCPUText.FontColor=%ls  Band1=%ls\n",
               cpuBar.c_str(), cpuFont.c_str(), band1.c_str());
        CHECK(cpuBar == L"90,170,255,255", "变量展开贯通：BarColor(#AccentCPU#) = Dark 基线");
        CHECK(cpuFont == L"255,255,255,255", "FontColor(#Color#) = Dark 基线");
        CHECK(band1 == L"90,200,255,255", "音频频段取色来自主题（Band1）");

        // 皮肤自有变量仍以本文件为准（主题未提供该键，不构成覆盖）。
        CHECK(cfg.ReadString(L"Variables", L"FontFace", L"") == L"Segoe UI",
              "皮肤自有变量保持本文件取值（FontFace）");

        // Dock 与挂件共用同一份主题：挂件皮肤也能读到 Dock 的 [Colors]。
        CHECK(cfg.ReadString(L"Colors", L"PanelColor", L"") == L"#40000000" &&
                  cfg.ReadInt(L"Colors", L"Transparency", -1) == 220,
              "Dock [Colors]（面板色/透明度）同源并入");
    }
    printf("\n");

    // ---- 5. 完整渲染测试 ----
    printf("5. Full skin render\n");
    // D22：皮肤纵向堆叠至日历网格（Y=366 + 7 行 ≈ 121）；D24 媒体挂件续接至 Y=584；
    // D25 频谱条阵列再续接 60px（Y=608..668），离屏高度随之放到 700 以免底部被裁。
    const uint32_t kW = 300, kH = 700;
    ID2D1RenderTarget* rt = nullptr;
    IWICBitmap* bmp = nullptr;
    bool rtOk = Canvas::CreateOffscreenTarget(kW, kH, &rt, &bmp);
    CHECK(rtOk, "Offscreen render target created");
    if (rtOk && rt) {
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(D2D1::ColorF::DarkSlateGray));

        // 完整渲染循环：Update + Render
        skin.Update();
        skin.Render(rt);

        HRESULT hr = rt->EndDraw();
        CHECK(SUCCEEDED(hr), "EndDraw succeeded");

        if (SUCCEEDED(hr) && bmp) {
            std::wstring outPath = L"test_skin_render_output.png";
            bool saved = Canvas::SaveBitmapToPng(bmp, outPath);
            CHECK(saved, "PNG saved");
            if (saved) {
                wchar_t fullPath[MAX_PATH];
                DWORD len = GetFullPathNameW(outPath.c_str(), MAX_PATH, fullPath, nullptr);
                if (len > 0) printf("  Output: %ls\n", fullPath);
            }
        }

        rt->Release();
        if (bmp) bmp->Release();
    }
    printf("\n");

    // ---- 6. 清理 ----
    printf("6. Cleanup\n");
    Canvas::FinalizeAll();
    printf("  [PASS] Factories released\n");
    printf("\n");

    if (g_Pass) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== SOME TESTS FAILED ===\n");
        return 1;
    }
}
