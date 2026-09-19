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

#include <cstring>

#include <d2d1.h>
#include <d2d1helper.h>

#include "Canvas.h"
#include "ConfigParser.h"
#include "Measure.h"
#include "Meter.h"
#include "MeasureTime.h"
#include "MeasureCalendar.h"
#include "MeasureCPU.h"
#include "MeasureMemory.h"
#include "MeasureNet.h"
#include "MeasureDisk.h"
#include "MeasureWeather.h"
#include "MeasureMedia.h"
#include "MeasureAudio.h"
#include "MeterString.h"
#include "MeterBar.h"
#include "MeterLine.h"

// Batch-2 adapters: need full types of MathParser (stack allocator & Parse)
// and ::Mouse (constructor signature Mouse(Skin*, Meter*)).  Included here
// rather than in Skin.h to keep header include-graph small.
#include "../Common/MathParser.h"
#include "Mouse.h"

// B3 鼠标派发：鼠标动作命令统一交给 CRainmeter -> CommandHandler 执行。
#include "Rainmeter.h"

#include <windowsx.h>   // GET_X_LPARAM / GET_WHEEL_DELTA_WPARAM / GET_XBUTTON_WPARAM

namespace raindock {

namespace {

const wchar_t* const kWindowClass = L"RainDeskPlus_SkinWnd";
const D2D1_COLOR_F kBackgroundColor = {0.07f, 0.07f, 0.09f, 1.0f};

std::unique_ptr<Measure> CreateMeasure(const std::wstring& type, Skin* skin, const std::wstring& name)
{
    if (type == L"Time")   return std::make_unique<MeasureTime>(skin, name.c_str());
    if (type == L"Calendar") return std::make_unique<MeasureCalendar>(skin, name.c_str());
    if (type == L"CPU")    return std::make_unique<MeasureCPU>(skin, name.c_str());
    if (type == L"Memory") return std::make_unique<MeasureMemory>(skin, name.c_str());
    if (type == L"Net")    return std::make_unique<MeasureNet>(skin, name.c_str());
    if (type == L"Disk")   return std::make_unique<MeasureDisk>(skin, name.c_str());
    if (type == L"Weather") return std::make_unique<MeasureWeather>(skin, name.c_str());
    if (type == L"Media")  return std::make_unique<MeasureMedia>(skin, name.c_str());
    if (type == L"Audio")  return std::make_unique<MeasureAudio>(skin, name.c_str());
    return nullptr;   // Plugin/RSS/Registry 等：M5+ 按需接入
}

std::unique_ptr<Meter> CreateMeter(const std::wstring& type, Skin* skin, const WCHAR* name)
{
    if (type == L"String") return std::make_unique<MeterString>(skin, name);
    if (type == L"Bar")    return std::make_unique<MeterBar>(skin, name);
    if (type == L"Line")   return std::make_unique<MeterLine>(skin, name);
    return nullptr;   // Image/Roundline/Histogram：M5+ 接入
}

// Win32 鼠标消息 → 上游 ::Mouse 的 MOUSEACTION 枚举（按钮/滚轮部分）。
// 悬停/离开（MOUSE_OVER/MOUSE_LEAVE）由 HandleMouseMessage 单独处理。
MOUSEACTION MouseActionForMessage(UINT msg, WPARAM wParam)
{
    switch (msg) {
    case WM_LBUTTONDOWN:   return MOUSE_LMB_DOWN;
    case WM_LBUTTONUP:     return MOUSE_LMB_UP;
    case WM_LBUTTONDBLCLK: return MOUSE_LMB_DBLCLK;
    case WM_MBUTTONDOWN:   return MOUSE_MMB_DOWN;
    case WM_MBUTTONUP:     return MOUSE_MMB_UP;
    case WM_MBUTTONDBLCLK: return MOUSE_MMB_DBLCLK;
    case WM_RBUTTONDOWN:   return MOUSE_RMB_DOWN;
    case WM_RBUTTONUP:     return MOUSE_RMB_UP;
    case WM_RBUTTONDBLCLK: return MOUSE_RMB_DBLCLK;
    case WM_XBUTTONDOWN:
        return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MOUSE_X1MB_DOWN : MOUSE_X2MB_DOWN;
    case WM_XBUTTONUP:
        return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MOUSE_X1MB_UP : MOUSE_X2MB_UP;
    case WM_XBUTTONDBLCLK:
        return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MOUSE_X1MB_DBLCLK : MOUSE_X2MB_DBLCLK;
    case WM_MOUSEWHEEL:
        return (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? MOUSE_MW_UP : MOUSE_MW_DOWN;
    case WM_MOUSEHWHEEL:
        return (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? MOUSE_MW_RIGHT : MOUSE_MW_LEFT;
    default:
        return MOUSEACTION_NONE;
    }
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
    m_IniPath = iniPath;

    // Reload 场景：清空旧 Measure/Meter 后按 INI 重建（Meter 先于 Measure
    // 释放，避免 Meter 析构时解引用已释放的 Measure 指针）。
    m_Meters.clear();
    m_Measures.clear();
    // 容器清空后旧的悬停指针立即失效，必须复位；否则后续 WM_MOUSEMOVE /
    // WM_MOUSELEAVE 会解引用已释放的 Meter（典型触发路径：鼠标动作 Bang 为
    // !Refresh，或 OnUpdateAction 内含 !Refresh）。
    m_MouseOverMeter = nullptr;
    m_TrackingMouseLeave = false;

    m_UpdateInterval = static_cast<uint32_t>(
        m_Parser->ReadInt(L"Rainmeter", L"Update", 1000));
    if (m_UpdateInterval == 0) m_UpdateInterval = 1000;

    // D36-40 性能项 #2：重建后视觉状态全新，下一帧必须重绘；指纹基准一并复位。
    m_Dirty = true;
    m_LastFingerprint = 0;

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
        auto t = CreateMeter(type, this, section.c_str());
        if (!t) continue;
        Measure* bound = nullptr;
        const std::wstring mname = m_Parser->ReadString(section, L"MeasureName", L"");
        if (!mname.empty()) {
            for (auto& m : m_Measures) {
                if (wcscmp(m->GetName(), mname.c_str()) == 0) { bound = m.get(); break; }
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

void Skin::DoBang(const std::wstring& bang)
{
    if (bang.empty()) return;
    // D36-40 性能项 #2：Bang 可能改变无法纳入视觉指纹的状态（如颜色/字体等
    // Meter 选项），故先标记强制重绘，避免脏检查吞掉这类变化。若该 Bang 自身
    // 已触发一次重绘（如 !Redraw），这次重绘会把标记清掉，不会多渲染一帧。
    m_Dirty = true;
    CRainmeter::GetInstance().ExecuteCommand(bang, this);
}

void Skin::Redraw()
{
    // !Redraw 语义是「立即重绘一帧」，必须绕过脏检查。
    m_Dirty = true;
    RenderFrame();
}

void Skin::Hide()
{
    if (m_Window) ::ShowWindow(m_Window, SW_HIDE);
}

void Skin::Show()
{
    if (m_Window) ::ShowWindow(m_Window, SW_SHOWNA);
    m_Dirty = true;   // 重新显示后内容可能已被系统丢弃，强制重绘
}

void Skin::Toggle()
{
    if (!m_Window) return;
    if (::IsWindowVisible(m_Window)) Hide(); else Show();
}

bool Skin::Reload()
{
    if (m_IniPath.empty()) return false;
    return Load(m_IniPath);
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

uint64_t Skin::ComputeVisualFingerprint()
{
    // FNV-1a 64：把「会影响画面」的状态压成一个 64 位指纹。
    // 覆盖粒度刻意取保守侧——宁可多渲染一帧，也不漏渲染：
    //   - Measure：GetValue() 的位模式 + GetString() 文本（Meter 的绘制都源于此）；
    //   - Meter：位置/尺寸/可见性（Bang 或选项变更会改变这些）。
    // 未覆盖的状态（颜色、字体等）由 DoBang/Redraw/Load 的强制置脏兜底。
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](uint64_t v) { hash ^= v; hash *= kFnvPrime; };

    for (auto& m : m_Measures)
    {
        const double value = m->GetValue();
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "fingerprint expects 64-bit double");
        std::memcpy(&bits, &value, sizeof(bits));
        mix(bits);

        for (const wchar_t* s = m->GetString(); s && *s; ++s)
        {
            mix(static_cast<uint64_t>(static_cast<uint16_t>(*s)));
        }
    }

    for (auto& t : m_Meters)
    {
        mix(static_cast<uint64_t>(static_cast<uint32_t>(t->GetX())));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(t->GetY())));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(t->GetWidth())));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(t->GetHeight())));
        mix(t->GetHidden() ? 1ull : 0ull);
    }

    return hash;
}

void Skin::RenderFrame()
{
    if (!m_RT) return;
    // 动作 Bang（!Redraw）会在同一次调用栈内再次进入 RenderFrame：既造成
    // BeginDraw/EndDraw 嵌套（D2D 会返回 D2DERR_WRONG_STATE），又让 Update 链
    // 无限递归。守卫在此截断，详见 D10 审查 #3。
    if (m_InRenderFrame) return;
    m_InRenderFrame = true;

    // Measure/Meter 采样照常推进（脏检查只省绘制，不省数据更新）。
    Update();

    ++m_Frames;

    // D36-40 性能项 #2：视觉状态未变化时跳过 Clear+Render。
    const uint64_t fingerprint = ComputeVisualFingerprint();
    if (!m_Dirty && fingerprint == m_LastFingerprint)
    {
        ++m_SkippedRenders;
        m_InRenderFrame = false;
        return;
    }

    m_LastFingerprint = fingerprint;
    m_Dirty = false;
    ++m_RenderPasses;

    m_RT->BeginDraw();
    m_RT->Clear(kBackgroundColor);
    Render(m_RT);
    m_RT->EndDraw();

    m_InRenderFrame = false;
}

Meter* Skin::FindMeterAtPoint(int x, int y) const
{
    // 倒序遍历：后绘制的 Meter 在上层，优先命中（与 Rainmeter 一致）。
    for (auto it = m_Meters.rbegin(); it != m_Meters.rend(); ++it) {
        Meter* meter = it->get();
        if (meter->GetHidden()) continue;
        const int w = meter->GetWidth();
        const int h = meter->GetHeight();
        if (w <= 0 || h <= 0) continue;
        if (x >= meter->GetX() && x < meter->GetX() + w &&
            y >= meter->GetY() && y < meter->GetY() + h) {
            return meter;
        }
    }
    return nullptr;
}

void Skin::HandleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    // ---- 悬停/离开：WM_MOUSEMOVE + WM_MOUSELEAVE 追踪 ----
    if (msg == WM_MOUSEMOVE) {
        if (!m_TrackingMouseLeave && m_Window) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_Window;
            ::TrackMouseEvent(&tme);
            m_TrackingMouseLeave = true;
        }

        Meter* meter = FindMeterAtPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (meter != m_MouseOverMeter) {
            std::wstring cmd;
            if (m_MouseOverMeter &&
                m_MouseOverMeter->GetMouse().GetActionCommand(MOUSE_LEAVE, cmd)) {
                // Bang 可能触发 !Refresh → Load() 清空 m_Meters，使 meter 与
                // m_MouseOverMeter 同时悬垂。先断开悬停指针，Bang 之后再按坐标
                // 重新做一次命中测试。
                m_MouseOverMeter = nullptr;
                DoBang(cmd);
                meter = FindMeterAtPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            m_MouseOverMeter = meter;
            if (meter && meter->GetMouse().GetActionCommand(MOUSE_OVER, cmd)) {
                DoBang(cmd);
                // 同上：MOUSE_OVER 的 Bang 也可能 Reload，重新解析以保持有效。
                m_MouseOverMeter = FindMeterAtPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
        }
        return;
    }

    if (msg == WM_MOUSELEAVE) {
        m_TrackingMouseLeave = false;
        if (m_MouseOverMeter) {
            Meter* hovered = m_MouseOverMeter;
            // 先取动作命令并断开悬停指针，再执行 Bang：Bang 可能 Reload 并释放
            // hovered 指向的 Meter。
            m_MouseOverMeter = nullptr;
            std::wstring cmd;
            if (hovered->GetMouse().GetActionCommand(MOUSE_LEAVE, cmd)) {
                DoBang(cmd);
            }
        }
        return;
    }

    // ---- 按钮/滚轮：命中测试后执行对应动作 ----
    const MOUSEACTION action = MouseActionForMessage(msg, wParam);
    if (action == MOUSEACTION_NONE) return;

    Meter* meter = FindMeterAtPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    if (!meter) return;

    std::wstring command;
    if (meter->GetMouse().GetActionCommand(action, command)) {
        DoBang(command);
    }
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
    // ===== B3 鼠标派发 =====
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (self) self->HandleMouseMessage(msg, wParam, lParam);
        return 0;
    default:
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

}  // namespace raindock
