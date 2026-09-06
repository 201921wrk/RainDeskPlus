/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Dock 窗口：承载 DockBar 的 Duilib 窗口派生。
 * 详见 docs/DOCK_DESIGN.md §2.4。
 * 骨架阶段：RAINDOCK_USE_DUILIB=OFF 时继承占位基类，仅暴露 DockBar。
 */
#pragma once

#include "DockBar.h"
#include "DuilibWindowBase.h"   // UI/

namespace raindock {

class DockWindow : public DuilibWindowBase
{
public:
    DockBar& GetDockBar() { return m_DockBar; }
    const DockBar& GetDockBar() const { return m_DockBar; }

    // Duilib 生命周期回调（接入真实基类后变 override）
    void InitWindow() override;
    void Notify(void* uiMsg) override;
    void OnMouseMove(void* sender, void* param) override;

private:
    DockBar m_DockBar;
};

}  // namespace raindock
