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
#ifndef RAINDOCK_LIBRARY_SKIN_H_
#define RAINDOCK_LIBRARY_SKIN_H_

#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID2D1RenderTarget;
struct ID2D1HwndRenderTarget;

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

    HWND GetWindow() const { return m_Window; }
    void SetWindow(HWND hWnd) { m_Window = hWnd; }
    // WM_TIMER 驱动下已成功呈现的帧数（Smoke 断言用）。
    uint32_t GetFrameCount() const { return m_Frames; }

    const std::vector<std::unique_ptr<Measure>>& GetMeasures() const { return m_Measures; }
    const std::vector<std::unique_ptr<Meter>>&   GetMeters()  const { return m_Meters; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateWindowAndTarget(int nCmdShow);
    // 更新 + 本帧渲染到窗口渲染目标（成功 EndDraw 才计帧）。
    void RenderFrame();

    std::vector<std::unique_ptr<Measure>> m_Measures;
    std::vector<std::unique_ptr<Meter>>   m_Meters;
    std::unique_ptr<ConfigParser>         m_Parser;

    HWND                    m_Window = nullptr;
    ID2D1HwndRenderTarget*  m_RT = nullptr;
    HINSTANCE               m_hInstance = nullptr;
    uint32_t                m_UpdateInterval = 1000;
    uint32_t                m_Frames = 0;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_SKIN_H_
