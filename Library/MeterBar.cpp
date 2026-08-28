/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterBar.cpp（骨架阶段占位）。
 * TODO(Phase1): 提取 Rainmeter 实现，FillRectangle + 边框 DrawRectangle。
 */
#include "MeterBar.h"

#include "ConfigParser.h"
#include "Measure.h"

namespace raindock {

void MeterBar::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    // TODO(Phase1): 读取 [MeterXxx] BarColor / BarImage / Orientation。
    if (m_Measure) m_Value = m_Measure->GetValue();
}

void MeterBar::Draw(ID2D1RenderTarget* /*rt*/)
{
    // TODO(Phase1): 取 m_Value（经 maxValue 归一化），按 m_Orientation 计算填充矩形，
    //   CreateSolidColorBrush(m_BarColor) -> FillRectangle; 外框用 m_FillColor。
    // 骨架阶段无渲染。
}

}  // namespace raindock
