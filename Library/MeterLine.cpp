/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterLine.cpp（骨架阶段占位）。
 * TODO(Phase1): 提取 Rainmeter 实现：
 *   - 自动 min/max 缩放
 *   - 多线条 Measure 绑定
 *   - D2D FillRectangle 背景 + DrawLine 折线
 */
#include "MeterLine.h"

#include "ConfigParser.h"
#include "Measure.h"

namespace raindock {

void MeterLine::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    // TODO(Phase1): 读取 LineColor / LineCount / AutoScale / HorizontalShift。
    (void)parser;
    if (m_Measure) PushSample(m_Measure->GetValue());
}

void MeterLine::Update()
{
    if (m_Measure) PushSample(m_Measure->GetValue());
}

void MeterLine::PushSample(double v)
{
    m_Samples.push_back(v);
    if (m_Samples.size() > m_MaxSamples) m_Samples.pop_front();
}

void MeterLine::Draw(ID2D1RenderTarget* /*rt*/)
{
    // TODO(Phase1): 把 m_Samples 映射到 m_W×m_H，构造 D2D1::PathSegment，
    //   ID2D1Factory::CreatePathGeometry + rt::DrawGeometry。
    // 骨架阶段无渲染。
}

}  // namespace raindock
