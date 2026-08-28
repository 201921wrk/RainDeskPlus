/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Dock 容器。详见 docs/DOCK_DESIGN.md §2.2。
 * 骨架阶段：不继承 Duilib 窗口基类（RAINDOCK_USE_DUILIB=OFF 时为普通类）。
 */
#ifndef RAINDOCK_DOCK_DOCK_BAR_H_
#define RAINDOCK_DOCK_DOCK_BAR_H_

#include "DockItem.h"
#include "DockMagnify.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace raindock {

enum class DockPosition { Top, Bottom, Left, Right };

class DockBar
{
public:
    DockBar() = default;
    ~DockBar() = default;

    // 项管理
    void AddItem(const DockItem& item);
    void RemoveItem(const std::wstring& id);
    void ClearItems();
    const std::vector<DockItem>& GetItems() const { return m_Items; }

    // 交互
    void OnMouseMove(const POINT& pt);
    void OnMouseLeave();
    void OnItemClick(const std::wstring& id);

    // 配置
    void SetIconSize(int size);          // 16..128 px
    void SetTransparency(int alpha);     // 0..255
    void SetPosition(DockPosition pos);
    void SetAutoHide(bool enable);

    int            GetIconSize()     const { return m_IconSize; }
    int            GetTransparency() const { return m_Transparency; }
    DockPosition   GetPosition()     const { return m_Position; }
    bool           GetAutoHide()     const { return m_AutoHide; }
    const DockMagnify& GetMagnify() const { return m_Magnify; }

    // 窗口/任务管理
    void ShowRunningApps();
    void MinimizeToDock(HWND hWnd);

private:
    std::vector<DockItem> m_Items;
    int           m_IconSize = 48;
    int           m_Transparency = 220;
    DockPosition  m_Position = DockPosition::Bottom;
    bool          m_AutoHide = false;
    DockMagnify   m_Magnify;
    POINT         m_LastMouse = {0, 0};
};

}  // namespace raindock

#endif  // RAINDOCK_DOCK_DOCK_BAR_H_
