/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Skin.cpp（皮肤容器 + 挂件窗口宿主）。
 *
 * M4 实现要点：
 *   - Measure 工厂：Time/CPU/Memory/Net（M1/M2 已真实现者）
 *   - Meter 工厂：String（M3 已真实现者）
 *   - 窗口：WS_POPUP 挂件风格，GWLP_USERDATA 绑定 Skin*，WM_TIMER 驱动帧
 *   - 渲染：D2D HwndRenderTarget（SOFTWARE，96 DPI，与 M3 离屏度量一致）
 */
#include "Skin.h"

#include <d2d1.h>
#include <d2d1helper.h>

#include "Canvas.h"
#include "ConfigParser.h"
#include "Measure.h"
#include "Meter.h"
#include "MeasureTime.h"
#include "MeasureCPU.h"
#include "MeasureMemory.h"
#include "MeasureNet.h"
#include "MeterString.h"

// Batch-2 adapters: need full types of MathParser (stack allocator & Parse)
// and ::Mouse (constructor signature Mouse(Skin*, Meter*)).  Included here
// rather than in Skin.h to keep header include-graph small.
#include "../Common/MathParser.h"
#include "Mouse.h"

namespace raindock {

namespace {

const wchar_t* const kWindowClass = L"RainDeskPlus_SkinWnd";
const D2D1_COLOR_F kBackgroundColor = {0.07f, 0.07f, 0.09f, 1.0f};

std::unique_ptr<Measure> CreateMeasure(const std::wstring& type, Skin* skin, const std::wstring& name)
{
    if (type == L"Time")   return std::make_unique<MeasureTime>(skin, name);
    if (type == L"CPU")    return std::make_unique<MeasureCPU>(skin, name);
    if (type == L"Memory") return std::make_unique<MeasureMemory>(skin, name);
    if (type == L"Net")    return std::make_unique<MeasureNet>(skin, name);
    return nullptr;   // Plugin/RSS/Registry 等：M5+ 按需接入
}

std::unique_ptr<Meter> CreateMeter(const std::wstring& type)
{
    if (type == L"String") return std::make_unique<MeterString>();
    return nullptr;   // Image/Bar/Line/Roundline：M5+ 接入
}

}  // namespace

Skin::Skin()
    : m_Parser(std::make_unique<ConfigParser>())
{
    // Batch-2: MathParser is heap-allocated (see comment in Skin.h).
    m_MathParser = new MathParser();
}

Skin::~Skin()
{
    m_Meters.clear();
    m_Measures.clear();
    if (m_RT)     { m_RT->Release();     m_RT = nullptr; }
    if (m_Window) { DestroyWindow(m_Window); m_Window = nullptr; }
    // Batch-2 cleanup: destroy ::Mouse instance before MathParser.
    // Note: can't call delete directly on incomplete type, but since we
    // include Mouse.h above, the full dtor is visible here.
    delete m_MousePtr;   m_MousePtr = nullptr;
    delete m_MathParser; m_MathParser = nullptr;
}

// ===========================================================================
// Batch-2 upstream adapter: lazy Mouse instance + per-skin MathParser
// ===========================================================================
::Mouse& Skin::GetMouse()
{
    if (!m_MousePtr) {
        // Cast `this` (raindock::Skin*) to the global-scope `Skin*` expected
        // by the upstream ::Mouse(Skin*, Meter* = nullptr) ctor.  The two
        // types are logically the same object (we use the using-decl in
        // Mouse.h), so the reinterpret_cast is both well-defined and points
        // to the same address.  It's only needed because C++ treats
        // `namespace raindock { class Skin; }` and a hypothetical global
        // `class Skin;` as distinct types even when a `using` decl aliases.
        auto* self = reinterpret_cast<Skin*>(this);
        m_MousePtr = new ::Mouse(self, nullptr);
    }
    return *m_MousePtr;
}

bool Skin::Load(const std::wstring& iniPath)
{
    if (!m_Parser->LoadFile(iniPath)) return false;

    m_UpdateInterval = static_cast<uint32_t>(
        m_Parser->ReadInt(L"Rainmeter", L"Update", 1000));
    if (m_UpdateInterval == 0) m_UpdateInterval = 1000;

    // 第一遍：Measure（Meter 绑定时需要其字符串初值，故先全部就位）。
    for (const auto& section : m_Parser->GetSections()) {
        const std::wstring type = m_Parser->ReadString(section, L"Measure", L"");
        if (type.empty()) continue;
        auto m = CreateMeasure(type, this, section);
        if (!m) continue;
        m->Initialize(*m_Parser, iniPath);
        m_Measures.push_back(std::move(m));
    }

    // 第二遍：Meter + MeasureName 绑定。
    for (const auto& section : m_Parser->GetSections()) {
        const std::wstring type = m_Parser->ReadString(section, L"Meter", L"");
        if (type.empty()) continue;
        auto t = CreateMeter(type);
        if (!t) continue;
        t->SetName(section);
        Measure* bound = nullptr;
        const std::wstring mname = m_Parser->ReadString(section, L"MeasureName", L"");
        if (!mname.empty()) {
            for (auto& m : m_Measures) {
                if (m->GetName() == mname) { bound = m.get(); break; }
            }
        }
        t->Initialize(*m_Parser, bound);
        m_Meters.push_back(std::move(t));
    }

    return !m_Meters.empty();
}

void Skin::Update()
{
    for (auto& m : m_Measures) m->Update();
    for (auto& t : m_Meters)  t->Update();
}

void Skin::Render(ID2D1RenderTarget* rt)
{
    if (!rt) return;
    for (auto& t : m_Meters) t->Draw(rt);
}

void Skin::DoBang(const std::wstring& /*bang*/)
{
    // TODO(M5): 由 CRainmeter -> CommandHandler 统一分发，Skin 仅持有上下文。
}

bool Skin::Show(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance = hInstance ? hInstance : GetModuleHandleW(nullptr);
    if (!m_Window) {
        if (!CreateWindowAndTarget(nCmdShow)) return false;
    } else {
        ShowWindow(m_Window, nCmdShow);
    }
    ::SetTimer(m_Window, 1, m_UpdateInterval, nullptr);
    RenderFrame();   // 首帧
    return m_Window && m_RT;
}

bool Skin::CreateWindowAndTarget(int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    // 重复注册无害（同参），忽略失败。
    ::RegisterClassExW(&wc);

    // AutoSize：meters 包围盒 + 边距（下限 120x50）。
    int w = 120, h = 50;
    for (auto& t : m_Meters) {
        const int rw = t->GetX() + t->GetWidth();
        const int rh = t->GetY() + t->GetHeight();
        if (rw > w - 10) w = rw + 10;
        if (rh > h - 10) h = rh + 10;
    }

    m_Window = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                 kWindowClass, L"RainDeskPlus",
                                 WS_POPUP,
                                 CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                                 nullptr, nullptr, m_hInstance, this);
    if (!m_Window) return false;

    // D2D HwndRenderTarget（SOFTWARE：与 M3 离屏同为软件光栅，DPI 显式 96）。
    RECT rc = {};
    ::GetClientRect(m_Window, &rc);
    const D2D1_SIZE_U size = { static_cast<UINT32>(rc.right - rc.left),
                               static_cast<UINT32>(rc.bottom - rc.top) };
    D2D1_RENDER_TARGET_PROPERTIES props = {};
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;

    ID2D1Factory* d2d = Canvas::GetD2DFactory();
    if (!d2d) return false;
    HRESULT hr = d2d->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(m_Window, size),
        &m_RT);
    if (FAILED(hr) || !m_RT) { m_Window = nullptr; return false; }

    ::ShowWindow(m_Window, nCmdShow);
    return true;
}

void Skin::RenderFrame()
{
    if (!m_RT) return;
    Update();
    m_RT->BeginDraw();
    m_RT->Clear(kBackgroundColor);
    Render(m_RT);
    if (SUCCEEDED(m_RT->EndDraw())) ++m_Frames;
}

LRESULT CALLBACK Skin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Skin* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Skin*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Skin*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_TIMER:
        if (self && wParam == 1) self->RenderFrame();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hWnd, &ps);
        ::EndPaint(hWnd, &ps);
        return 0;   // 帧内容由 WM_TIMER 维护
    }
    case WM_DESTROY:
        if (self) { ::KillTimer(hWnd, 1); self->SetWindow(nullptr); }
        return 0;
    default:
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

}  // namespace raindock
