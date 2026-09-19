/*
 * RainDeskPlus - Desktop beautification platform
 * UI 适配层（GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Duilib 窗口基类适配。
 * - RAINDOCK_USE_DUILIB=ON  : 别名到真实 Duilib 的 DuiLib::WindowImplBase。
 * - RAINDOCK_USE_DUILIB=OFF : 最小占位基类，使 Dock/Skin 可在未引入 Duilib 时编译。
 */
#pragma once

#ifdef RAINDOCK_USE_DUILIB
// 真实 Duilib：UIlib.h 已包含 Utils/WinImplBase.h，且 WinImplBase.h 自身不含任何
// include（不自包含），故必须由 UIlib.h 提供上下文（见 docs/DOCK_DESIGN.md DD-3）。
#include "UIlib.h"
using DuilibWindowBase = DuiLib::WindowImplBase;
#else
// 占位基类：仅保留「可构造 + 可虚析构」，不再伪造 Duilib 回调签名
// （伪造签名会在打开开关时与真实 WindowImplBase 冲突）。
// DockWindow/Skin 的 Duilib override 由各自的 #ifdef 提供（DD-2）。
class DuilibWindowBase
{
public:
    virtual ~DuilibWindowBase() = default;
};
#endif
