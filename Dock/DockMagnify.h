/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 悬停放大效果算法。参考 RocketDock：以鼠标到图标中心的距离衰减。
 * 详见 docs/DOCK_DESIGN.md §4。
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // POINT, RECT

namespace raindock {

class DockMagnify
{
public:
    void  SetMaxScale(float s)  { m_MaxScale = s; }    // 例如 1.8
    void  SetMaxRadius(float r){ m_MaxRadius = r; }    // 例如 150.0f
    float GetMaxScale()  const { return m_MaxScale; }
    float GetMaxRadius() const { return m_MaxRadius; }

    // 计算某图标在鼠标位置下的缩放比（>=1.0）。
    float CalculateScale(const POINT& mousePos, const RECT& iconRect) const;

private:
    float m_MaxScale  = 1.8f;
    float m_MaxRadius = 150.0f;
};

}  // namespace raindock
