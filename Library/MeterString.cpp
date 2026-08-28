/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterString.cpp（骨架阶段占位）。
 * TODO(Phase1): 提取 Rainmeter 实现：
 *   - 共享 IDWriteFactory 单例创建 TextFormat/TextLayout
 *   - 按 StringAlign / Percentual / ClipString 处理布局
 *   - 用 ID2D1RenderTarget::DrawTextLayout 渲染
 */
#include "MeterString.h"

#include "Measure.h"
#include "ConfigParser.h"

namespace raindock {

void MeterString::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    // TODO(Phase1): 读 [MeterXxx] Text / FontFace / FontSize / FontColor /
    //   StringAlign / StringEffect / AntiAlias / ClipString。
    m_FontFamily = parser.ReadString(m_Name, L"FontFace", m_FontFamily);
    m_FontSize   = static_cast<float>(parser.ReadFloat(m_Name, L"FontSize", m_FontSize));
    if (m_Measure) m_Text = m_Measure->GetString();
}

void MeterString::Draw(ID2D1RenderTarget* /*rt*/)
{
    // TODO(Phase1): 构造/复用 m_TextLayout，rt->DrawTextLayout(origin, layout, brush)。
    // 骨架阶段无渲染。
}

}  // namespace raindock
