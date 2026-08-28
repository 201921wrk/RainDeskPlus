/*
 * RainDeskPlus - Desktop beautification platform
 * UI 适配层（GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Duilib 窗口基类适配。
 * - RAINDOCK_USE_DUILIB=ON  : 别名到真实 Duilib 的 WindowImplBase。
 * - RAINDOCK_USE_DUILIB=OFF : 最小占位基类，使 Dock/Skin 可在未引入 Duilib 时编译。
 *
 * 接入真实 Duilib 时，替换 third_party/duilib 路径并改 include 即可。
 */
#ifndef RAINDOCK_UI_DUILIB_WINDOW_BASE_H_
#define RAINDOCK_UI_DUILIB_WINDOW_BASE_H_

#ifdef RAINDOCK_USE_DUILIB
// 真实 Duilib 头（路径依 vendor 版本调整）
#include "WindowImplBase.h"
using DuilibWindowBase = WindowImplBase;
#else
// ---- 占位基类（无 Duilib 时）----
class DuilibWindowBase
{
public:
    virtual ~DuilibWindowBase() = default;

    // 对齐 Duilib 生命周期回调（待接入真实基类后自动替换为 override）
    virtual void InitWindow() {}
    virtual void Notify(void* /*uiMsg*/) {}
    virtual void OnClick(void* /*sender*/) {}
    virtual void OnMouseMove(void* /*sender*/, void* /*param*/) {}

    // 最小窗口创建占位（真实 Duilib 提供 Create + XML 加载）
    bool Create() { return true; /* TODO(RAINDOCK_USE_DUILIB) */ }
    void Close()  { /* TODO */ }
};
#endif

#endif  // RAINDOCK_UI_DUILIB_WINDOW_BASE_H_
