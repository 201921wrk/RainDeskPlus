/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 悬停放大算法实现。距离越近缩放越大，超出影响半径保持 1.0。
 */
#include "DockMagnify.h"

#include <cmath>

namespace raindock {

float DockMagnify::CalculateScale(const POINT& mousePos, const RECT& iconRect) const
{
    const float cx = (iconRect.left + iconRect.right) * 0.5f;
    const float cy = (iconRect.top + iconRect.bottom)  * 0.5f;

    const float dx = static_cast<float>(mousePos.x) - cx;
    const float dy = static_cast<float>(mousePos.y) - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist >= m_MaxRadius || m_MaxRadius <= 0.0f) return 1.0f;

    // 线性衰减：距离 0 -> m_MaxScale，距离 m_MaxRadius -> 1.0。
    // TODO(Phase2): 可替换为高斯/余弦衰减获得更平滑曲线。
    return 1.0f + (m_MaxScale - 1.0f) * (1.0f - dist / m_MaxRadius);
}

}  // namespace raindock
