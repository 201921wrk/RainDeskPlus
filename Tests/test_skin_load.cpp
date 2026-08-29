/*
 * RainDeskPlus - M4 Skin 加载 Smoke 测试
 * 验证 Skin::Load 能正确解析 skin.ini 并实例化 Measure/Meter，
 * 且 Meter 能正确绑定 Measure 并渲染。
 */
#include "Skin.h"
#include "Canvas.h"
#include "MeterString.h"
#include "Measure.h"

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
    CHECK(measures.size() == 1, "1 measure created");
    if (!measures.empty()) {
        const auto& m = measures[0];
        CHECK(m->GetName() == L"MeasureCPU", "Measure name is MeasureCPU");
        m->Update();
        double val = m->GetValue();
        printf("  CPU value: %.1f%%\n", val);
        CHECK(val >= 0.0 && val <= 100.0, "CPU value in [0, 100]");
        const wchar_t* str = m->GetString();
        printf("  CPU string: %ls\n", str ? str : L"(null)");
        CHECK(str != nullptr && wcslen(str) > 0, "CPU string non-empty");
    }
    printf("\n");

    // ---- 3. 验证 Meter ----
    printf("3. Meter verification\n");
    const auto& meters = skin.GetMeters();
    CHECK(meters.size() == 1, "1 meter created");
    if (!meters.empty()) {
        const auto& meter = meters[0];
        CHECK(meter->GetName() == L"MeterCPUText", "Meter name is MeterCPUText");
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
    printf("\n");

    // ---- 4. 完整渲染测试 ----
    printf("4. Full skin render\n");
    const uint32_t kW = 300, kH = 100;
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

    // ---- 5. 清理 ----
    printf("5. Cleanup\n");
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
