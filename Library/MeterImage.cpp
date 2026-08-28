/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterImage.cpp（骨架阶段占位）。
 * TODO(Phase1): 提取 Rainmeter 实现：
 *   - IWICImagingFactory 解码 PNG/ICO/JPG -> IWICBitmapSource
 *   - ID2D1RenderTarget::CreateBitmapFromWicBitmap
 *   - 按 Path/MeasureName 动态切换（如电池图标按百分比选不同位图）
 *   - 支持 Container / Tile / Scale / Offset
 */
#include "MeterImage.h"

#include "ConfigParser.h"

namespace raindock {

void MeterImage::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    m_ImagePath = parser.ReadString(m_Name, L"ImagePath", L"");
    m_PreserveAspectRatio = parser.ReadBool(m_Name, L"PreserveAspectRatio", true);
    m_DrawRect = D2D1::RectF(0, 0, static_cast<float>(m_W), static_cast<float>(m_H));
}

void MeterImage::Draw(ID2D1RenderTarget* /*rt*/)
{
    // TODO(Phase1): rt->DrawBitmap(m_Bitmap, m_DrawRect, alpha, ...)
    // 骨架阶段无位图，跳过渲染。
}

}  // namespace raindock
