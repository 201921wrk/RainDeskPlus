/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Skin.h（皮肤容器 + 挂件窗口宿主）。
 *
 * M4：原生 Win32 弹窗（WS_POPUP）+ D2D HwndRenderTarget（软件类型）+
 *     WM_TIMER 驱动 Update/Render 循环。Duilib 窗口替换按 D5 计划另行接入。
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID2D1RenderTarget;
struct ID2D1HwndRenderTarget;

// Batch-2 forward decl: upstream Mouse class lives at global scope.
class Mouse;
// MathParser is declared in Common/MathParser.h at global scope.
class MathParser;

namespace raindock {

class Measure;
class Meter;
class ConfigParser;
class CommandHandler;

class Skin
{
public:
    Skin();
    ~Skin();
    Skin(const Skin&) = delete;
    Skin& operator=(const Skin&) = delete;

    // 加载皮肤 INI：[Rainmeter] 全局 + Measure*/Meter* 段工厂构建与绑定。
    bool Load(const std::wstring& iniPath);
    // 创建并显示挂件窗口（幂等：已创建则仅显示）。返回窗口+渲染目标是否就绪。
    bool Show(HINSTANCE hInstance, int nCmdShow);
    // 周期刷新（驱动所有 Measure.Update -> Meter.Update）。
    void Update();
    // 外部渲染目标渲染（离屏/测试路径）。
    void Render(ID2D1RenderTarget* rt);
    // 执行 Bang 命令。
    void DoBang(const std::wstring& bang);

    // ===== M5 Bang 支持方法 =====
    // 立即重绘一帧（对应 !Redraw）。
    void Redraw();
    // 窗口显隐控制（对应 !Hide / !Show / !Toggle）。
    void Hide();
    void Show();
    void Toggle();
    // 重新加载当前 INI（对应 !Refresh）。返回是否成功。
    bool Reload();
    // 当前 INI 路径（Load 后有效）。
    const std::wstring& GetIniPath() const { return m_IniPath; }

    HWND GetWindow() const { return m_Window; }
    void SetWindow(HWND hWnd) { m_Window = hWnd; }
    // WM_TIMER 驱动下已成功呈现的帧数（Smoke 断言用）。
    uint32_t GetFrameCount() const { return m_Frames; }

    const std::vector<std::unique_ptr<Measure>>& GetMeasures() const { return m_Measures; }
    const std::vector<std::unique_ptr<Meter>>&   GetMeters()  const { return m_Meters; }

    // ===== Batch-2 upstream compatibility =====
    // Default UpdateDivider from [Rainmeter] section.  Upstream
    // Section::ReadOptions falls back to this per-skin default.  For M4/M5
    // skeleton we keep 1 (update every tick); read from INI when
    // ConfigParser supports [Rainmeter] fully in Batch-3.
    int  GetDefaultUpdateDivider() const { return m_DefaultUpdateDivider; }
    void SetDefaultUpdateDivider(int v)  { m_DefaultUpdateDivider = v > 0 ? v : 1; }

    // Mouse subsystem bookkeeping.
    void SetHasMouseScrollAction()       { m_HasMouseScrollAction = true; }
    bool HasMouseScrollAction() const    { return m_HasMouseScrollAction; }
    // Returns a per-skin ::Mouse instance, created lazily on first call
    // (so headers that include Skin.h don't need the full Mouse.h type).
    ::Mouse& GetMouse();

    // Direct handles used by upstream parsers.
    ConfigParser& GetParser()            { return *m_Parser; }
    MathParser&   GetMathParser()        { return *m_MathParser; }

    // @Resources directory (absolute path ending with '\').  Upstream Mouse
    // uses this when loading custom .cur/.ani cursors.  M4 skeleton uses
    // Skins\<skin-name>\@Resources\ (may not exist — callers must tolerate
    // empty/missing path).
    const std::wstring& GetResourcesPath() const { return m_ResourcesPath; }
    void SetResourcesPath(std::wstring p) { m_ResourcesPath = std::move(p); }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateWindowAndTarget(int nCmdShow);
    // 更新 + 本帧渲染到窗口渲染目标（成功 EndDraw 才计帧）。
    void RenderFrame();

    // ===== B3 鼠标派发 =====
    // 命中测试：返回坐标 (x,y) 下的第一个可见 Meter（客户区坐标）。
    Meter* FindMeterAtPoint(int x, int y) const;
    // 鼠标消息 → Meter::Mouse 动作分发（按钮/滚轮/悬停/离开）。
    void HandleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    std::vector<std::unique_ptr<Measure>> m_Measures;
    std::vector<std::unique_ptr<Meter>>   m_Meters;
    std::unique_ptr<ConfigParser>         m_Parser;

    HWND                    m_Window = nullptr;
    ID2D1HwndRenderTarget*  m_RT = nullptr;
    HINSTANCE               m_hInstance = nullptr;
    uint32_t                m_UpdateInterval = 1000;
    uint32_t                m_Frames = 0;
    std::wstring            m_IniPath;   // 最近一次 Load 的 INI 路径（!Refresh 用）

    // ===== Batch-2 upstream compatibility members =====
    int                     m_DefaultUpdateDivider = 1;
    bool                    m_HasMouseScrollAction = false;
    std::wstring            m_ResourcesPath;       // may be empty
    // MathParser is a global-class object (no internal heap, default-ctor
    // is cheap).  Upstream IfActions uses it for IfCondition formula eval.
    MathParser*             m_MathParser = nullptr;   // heap-allocated to keep sizeof stable & avoid full MathParser.h in header.
    // Lazily-allocated global-scope Mouse instance (Batch-2).
    ::Mouse*                m_MousePtr = nullptr;

    // ===== B3 鼠标派发状态 =====
    Meter*                  m_MouseOverMeter = nullptr;   // 当前悬停的 Meter（不拥有）
    bool                    m_TrackingMouseLeave = false; // 是否已调用 TrackMouseEvent(TME_LEAVE)
};

}  // namespace raindock
