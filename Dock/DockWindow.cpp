/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * DockWindow 实现。骨架阶段：转发事件到 DockBar；真实 Duilib 接入后
 * 在 InitWindow 中加载 Dock XML、注册控件事件。
 */
#include "DockWindow.h"

namespace raindock {

void DockWindow::InitWindow()
{
    // TODO(Phase2 RAINDOCK_USE_DUILIB): 加载 Dock XML 布局、设置透明/异形窗口属性、
    //   绑定 OnMouseMove / OnClick 事件。当前占位。
    m_DockBar.SetAutoHide(false);
}

void DockWindow::Notify(void* /*uiMsg*/)
{
    // TODO(Phase2): Duilib 消息路由到具体控件 -> OnItemClick。
}

void DockWindow::OnMouseMove(void* /*sender*/, void* /*param*/)
{
    // TODO(Phase2): 从 param 取出鼠标坐标，调用 m_DockBar.OnMouseMove(pt)。
}

}  // namespace raindock
