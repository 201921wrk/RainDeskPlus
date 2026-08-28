/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * DockBar 实现。骨架阶段：项管理 + 点击启动已可用；
 * ShowRunningApps / MinimizeToDock 留 TODO（需 EnumWindows + QueryFullProcessImageName）。
 */
#include "DockBar.h"

#include <algorithm>
#include <shellapi.h>

namespace raindock {

void DockBar::AddItem(const DockItem& item)
{
    // 同 id 则替换。
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&](const DockItem& i){ return i.id == item.id; });
    if (it != m_Items.end()) *it = item;
    else                      m_Items.push_back(item);
}

void DockBar::RemoveItem(const std::wstring& id)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&](const DockItem& i){ return i.id == id; });
    if (it != m_Items.end()) m_Items.erase(it);
}

void DockBar::ClearItems()
{
    m_Items.clear();
}

void DockBar::OnMouseMove(const POINT& pt)
{
    m_LastMouse = pt;
    // TODO(Phase2): 遍历可见项，用 m_Magnify.CalculateScale 计算目标缩放，
    //   平滑逼近当前缩放，触发重绘。
}

void DockBar::OnMouseLeave()
{
    // TODO(Phase2): 所有项缩放回弹到 1.0。
    m_LastMouse = {0, 0};
}

void DockBar::OnItemClick(const std::wstring& id)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&](const DockItem& i){ return i.id == id; });
    if (it == m_Items.end()) return;

    if (it->isRunning && it->hWnd)
    {
        // 运行中：前置或最小化（切换）。
        // TODO(Phase2): 判断当前是否已前置，决定 ShowWindow 的 SW_RESTORE / SW_MINIMIZE。
        ::ShowWindow(it->hWnd, SW_RESTORE);
        ::SetForegroundWindow(it->hWnd);
        return;
    }

    // 未运行：启动。
    std::wstring params = it->arguments;
    ::ShellExecuteW(nullptr, L"open", it->targetPath.c_str(),
                    params.empty() ? nullptr : params.c_str(),
                    it->workingDir.empty() ? nullptr : it->workingDir.c_str(),
                    SW_SHOWNORMAL);
}

void DockBar::SetIconSize(int size)
{
    m_IconSize = (size < 16) ? 16 : (size > 128 ? 128 : size);
}

void DockBar::SetTransparency(int alpha)
{
    m_Transparency = (alpha < 0) ? 0 : (alpha > 255 ? 255 : alpha);
}

void DockBar::SetPosition(DockPosition pos) { m_Position = pos; }
void DockBar::SetAutoHide(bool enable)       { m_AutoHide = enable; }

void DockBar::ShowRunningApps()
{
    // TODO(Phase2): EnumWindows + IsWindowVisible + GetWindowLong(GW_OWNER)
    //   + QueryFullProcessImageName 匹配项的 targetPath，更新 isRunning / hWnd。
}

void DockBar::MinimizeToDock(HWND hWnd)
{
    // TODO(Phase2): ShowWindow(hWnd, SW_MINIMIZE)，并把 hWnd 关联到匹配的 DockItem。
    (void)hWnd;
}

}  // namespace raindock
