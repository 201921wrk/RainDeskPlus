/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Dock 窗口：承载 DockBar 的窗口派生，负责把平台事件翻译成 DockBar 调用。
 * 设计详见 docs/DOCK_DESIGN.md §2.4 / §4.4。
 *
 * 两条编译线共用同一份头文件：
 *   - RAINDOCK_USE_DUILIB 定义   → 继承真实的 DuiLib::WindowImplBase，重写其虚接口；
 *   - RAINDOCK_USE_DUILIB 未定义 → 继承占位基类，只暴露平台无关入口。
 * Duilib 类型（CDuiString / TNotifyUI / LRESULT 等）一律限定在宏内，
 * 保证关闭开关时该头文件对 Duilib 零依赖（DD-2 / DD-12）。
 */
#pragma once

#include <string>
#include <vector>

#include "DockBar.h"
#include "DuilibWindowBase.h"   // UI/

namespace raindock {

class DockWindow : public DuilibWindowBase
{
public:
    DockWindow();
    ~DockWindow() override;

    DockBar&       GetDockBar() { return m_DockBar; }
    const DockBar& GetDockBar() const { return m_DockBar; }

    // ---------- 平台无关入口（OFF 模式亦可测：仅加载数据，不建窗口） ----------
    bool Start(const std::wstring& iniPath);   // 读配置 → [ON] 建窗口 + 起动画定时器
    void Stop();                              // [ON] 停表 + Close()；始终 SaveConfig

    // DockBar 的转发入口（消息与测试都走这三个，避免 Duilib 类型外泄）
    void HandleMouseMove(const POINT& pt);
    void HandleMouseLeave();
    void HandleItemClick(const std::wstring& id);

    // ---------- 拖放加项（D15，§7.6.1 / DD-13） ----------
    // 拖入 N 个文件 → 返回实际新增（去重后）的项数；任一失败不影响其余。
    // 本入口平台无关（OFF 线亦可测）：只改内存模型；[ON] 额外补控件与重排。
    size_t HandleFilesDropped(const std::vector<std::wstring>& paths);

#ifdef RAINDOCK_USE_DUILIB
    // ---------- DuiLib::WindowImplBase 契约（真实 Duilib 分支） ----------
    // 注意：这些 override 只在 RAINDOCK_USE_DUILIB 下存在，OFF 模式不声明，
    //       因此不会与占位基类的接口冲突（DD-2）。
    DuiLib::CDuiString GetSkinFile() override;                          // 内联 XML（首字符 '<' 判定）
    LPCTSTR            GetWindowClassName() const override;             // _T("RainDeskPlusDockWnd")
    void               InitWindow() override;                           // 绑图标、起 60ms 动画表
    void               Notify(DuiLib::TNotifyUI& msg) override;         // item click / mouseleave
    LRESULT            OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;
    LRESULT            HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;  // 拦截 WM_TIMER
    void               OnFinalMessage(HWND hWnd) override;              // 解绑/清理（不得 delete this）

private:
    POINT    m_LastMouse{};
    UINT_PTR m_AnimTimer = 0;
#endif

private:
    DockBar      m_DockBar;
    std::wstring m_IniPath;   // Stop() 需要用它回写配置
};

}  // namespace raindock
