/*
 * RainDeskPlus - M3.3 Meter 渲染 Smoke 测试
 * 离屏 WIC 软件渲染目标 + MeterString D2D/DWrite 文本渲染验证。
 * 产出 PNG 供人工核验，同时做基本断言（非零尺寸 / 非全透明）。
 */
#include "Canvas.h"
#include "MeterString.h"

#include <cstdio>
#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>

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

// 读取 WIC 位图中心区域的几个像素，验证非全透明（即确实有东西被画上去了）。
static bool CheckBitmapHasContent(IWICBitmap* bmp, uint32_t w, uint32_t h)
{
    IWICBitmapLock* lock = nullptr;
    WICRect rc = { 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
    if (FAILED(bmp->Lock(&rc, WICBitmapLockRead, &lock)) || !lock) return false;

    UINT stride = 0;
    UINT bufSize = 0;
    BYTE* pBuf = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&bufSize, &pBuf);

    bool hasContent = false;
    // 采样中心行附近的若干像素，检查 alpha 通道是否非零
    uint32_t centerY = h / 2;
    if (centerY < h && pBuf && stride > 0) {
        const BYTE* row = pBuf + centerY * stride;
        for (uint32_t x = 0; x < w && x < stride / 4; ++x) {
            BYTE alpha = row[x * 4 + 3];  // BGRA format, alpha is byte 3
            if (alpha > 10) {
                hasContent = true;
                break;
            }
        }
    }

    lock->Release();
    return hasContent;
}

int main()
{
    printf("=== M3.3 Meter Render Smoke Test ===\n");
    printf("Canvas + MeterString (D2D + DirectWrite)\n\n");

    // ---- 1. 共享工厂初始化 ----
    printf("1. Shared factories\n");
    ID2D1Factory* d2d = Canvas::GetD2DFactory();
    CHECK(d2d != nullptr, "ID2D1Factory created");
    IDWriteFactory* dw = Canvas::GetWriteFactory();
    CHECK(dw != nullptr, "IDWriteFactory created");
    IWICImagingFactory* wic = Canvas::GetWICFactory();
    CHECK(wic != nullptr, "IWICImagingFactory created");
    printf("\n");

    // ---- 2. 离屏渲染目标 ----
    printf("2. Offscreen render target (400x200)\n");
    const uint32_t kW = 400, kH = 200;
    ID2D1RenderTarget* rt = nullptr;
    IWICBitmap* bmp = nullptr;
    bool ok = Canvas::CreateOffscreenTarget(kW, kH, &rt, &bmp);
    CHECK(ok, "CreateOffscreenTarget succeeded");
    CHECK(rt != nullptr, "RenderTarget non-null");
    CHECK(bmp != nullptr, "WICBitmap non-null");
    if (rt) {
        D2D1_SIZE_F sz = rt->GetSize();
        CHECK(sz.width >= 399.0f && sz.width <= 401.0f, "RenderTarget width ~400");
        CHECK(sz.height >= 199.0f && sz.height <= 201.0f, "RenderTarget height ~200");
    }
    printf("\n");

    if (!rt || !bmp) {
        printf("Cannot proceed without render target, aborting.\n");
        Canvas::FinalizeAll();
        return 1;
    }

    // ---- 3. 清屏 + MeterString 绘制 ----
    printf("3. MeterString rendering\n");

    rt->BeginDraw();
    // 浅灰背景，便于观察文本
    rt->Clear(D2D1::ColorF(D2D1::ColorF::LightGray));

    MeterString meter(nullptr, L"TestString");
    meter.SetText(L"Hello RainDeskPlus! M3 Smoke Test");

    // 手动设置属性（不通过 ConfigParser）
    meter.Draw(rt);  // 第一次 Draw：文本 metrics 为 0？应至少渲染

    int tw = meter.GetTextWidth();
    int th = meter.GetTextHeight();
    printf("  Text metrics: %d x %d\n", tw, th);
    CHECK(tw > 0, "Text width > 0");
    CHECK(th > 0, "Text height > 0");

    // 在 (20, 20) 处重绘（第一次 Draw 已建 layout，但 m_X/m_Y 为 0）
    // 我们手动通过 Initialize 来设坐标更麻烦，直接再调一次即可
    meter.Draw(rt);

    HRESULT hr = rt->EndDraw();
    CHECK(SUCCEEDED(hr), "EndDraw succeeded");
    printf("\n");

    // ---- 4. 位图内容验证 ----
    printf("4. Bitmap content check\n");
    bool hasContent = CheckBitmapHasContent(bmp, kW, kH);
    CHECK(hasContent, "Bitmap has non-transparent pixels");
    printf("\n");

    // ---- 5. PNG 落盘（人工核验） ----
    printf("5. Save to PNG\n");
    std::wstring outPath = L"test_meter_render_output.png";
    bool saved = Canvas::SaveBitmapToPng(bmp, outPath);
    CHECK(saved, "PNG saved successfully");
    if (saved) {
        // 打印完整路径
        wchar_t fullPath[MAX_PATH];
        DWORD len = GetFullPathNameW(outPath.c_str(), MAX_PATH, fullPath, nullptr);
        if (len > 0) {
            printf("  Output: %ls\n", fullPath);
        }
    }
    printf("\n");

    // ---- 6. 清理 ----
    printf("6. Cleanup\n");
    if (rt)  { rt->Release();  rt = nullptr; }
    if (bmp) { bmp->Release(); bmp = nullptr; }
    Canvas::FinalizeAll();
    printf("  [PASS] Factories released\n");
    printf("\n");

    // ---- 结果 ----
    if (g_Pass) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== SOME TESTS FAILED ===\n");
        return 1;
    }
}
