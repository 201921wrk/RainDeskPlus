/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Canvas.cpp（渲染上下文实现，D2D 版）。
 */
#include "Canvas.h"

namespace raindock {

namespace {
ID2D1Factory*       g_D2DFactory   = nullptr;
IDWriteFactory*     g_WriteFactory = nullptr;
IWICImagingFactory* g_WICFactory   = nullptr;
}  // namespace

ID2D1Factory* Canvas::GetD2DFactory()
{
    if (!g_D2DFactory) {
        D2D1_FACTORY_OPTIONS opts = {};
        ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                            __uuidof(ID2D1Factory), &opts,
                            reinterpret_cast<void**>(&g_D2DFactory));
    }
    return g_D2DFactory;
}

IDWriteFactory* Canvas::GetWriteFactory()
{
    if (!g_WriteFactory) {
        ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(&g_WriteFactory));
    }
    return g_WriteFactory;
}

IWICImagingFactory* Canvas::GetWICFactory()
{
    if (!g_WICFactory) {
        // WIC 需要 COM 初始化。在单线程场景下先初始化 COM；
        // 若已由调用方初始化（返回 S_FALSE）也属正常。
        HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
            return nullptr;
        }
        ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&g_WICFactory));
    }
    return g_WICFactory;
}

bool Canvas::CreateOffscreenTarget(uint32_t width, uint32_t height,
                                   ID2D1RenderTarget** outRT,
                                   IWICBitmap** outBitmap)
{
    if (!outRT || !outBitmap) return false;
    *outRT = nullptr;
    *outBitmap = nullptr;

    IWICImagingFactory* wic = GetWICFactory();
    ID2D1Factory* d2d = GetD2DFactory();
    if (!wic || !d2d) return false;

    IWICBitmap* bmp = nullptr;
    HRESULT hr = wic->CreateBitmap(width, height,
                                   GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnDemand, &bmp);
    if (FAILED(hr) || !bmp) return false;

    // 显式 96 DPI：保证 DirectWrite DIP 度量与位图像素一一对应，
    // Smoke 断言不受系统 DPI 缩放影响。
    const D2D1_PIXEL_FORMAT pf = { DXGI_FORMAT_B8G8R8A8_UNORM,
                                   D2D1_ALPHA_MODE_PREMULTIPLIED };
    D2D1_RENDER_TARGET_PROPERTIES props = {};
    props.type        = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat = pf;
    props.dpiX        = 96.0f;
    props.dpiY        = 96.0f;
    props.usage       = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel    = D2D1_FEATURE_LEVEL_DEFAULT;

    ID2D1RenderTarget* rt = nullptr;
    hr = d2d->CreateWicBitmapRenderTarget(bmp, props, &rt);
    if (FAILED(hr) || !rt) {
        bmp->Release();
        return false;
    }

    *outRT = rt;
    *outBitmap = bmp;   // 移交 CreateBitmap 产生的引用计数
    return true;
}

bool Canvas::SaveBitmapToPng(IWICBitmap* bitmap, const std::wstring& path)
{
    IWICImagingFactory* wic = GetWICFactory();
    if (!wic || !bitmap || path.empty()) return false;

    IWICStream* stream = nullptr;
    if (FAILED(wic->CreateStream(&stream)) || !stream) return false;

    bool ok = false;
    if (SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        IWICBitmapEncoder* enc = nullptr;
        if (SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && enc) {
            if (SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache))) {
                IWICBitmapFrameEncode* frame = nullptr;
                if (SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && frame) {
                    if (SUCCEEDED(frame->Initialize(nullptr)) &&
                        SUCCEEDED(frame->WriteSource(bitmap, nullptr)) &&
                        SUCCEEDED(frame->Commit())) {
                        ok = SUCCEEDED(enc->Commit());
                    }
                    frame->Release();
                }
            }
            enc->Release();
        }
    }
    stream->Release();
    return ok;
}

void Canvas::FinalizeAll()
{
    if (g_D2DFactory)   { g_D2DFactory->Release();   g_D2DFactory = nullptr; }
    if (g_WriteFactory) { g_WriteFactory->Release(); g_WriteFactory = nullptr; }
    if (g_WICFactory)   { g_WICFactory->Release();   g_WICFactory = nullptr; }
}

}  // namespace raindock
