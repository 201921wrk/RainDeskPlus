/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterLine.cpp（折线图，D2D 重构版）。
 *
 * 本版实现：
 *   - Initialize 读 W / H / LineColor / LineWidth / LineCount / AutoScale
 *   - Update 从 Measure 取一个采样点入队（定长环形队列）
 *   - Draw 逐段 DrawLine，最新样本贴右边缘、历史向左排列
 *
 * 量程语义：
 *   AutoScale=1（默认）：纵轴取 [0, 采样峰值]，适合无界的 Measure（如网络速率，
 *     MaxValue=0 时 GetValue 不做裁剪，数值量纲跨度极大）。
 *   AutoScale=0：纵轴取 Measure 的 [MinValue, MaxValue]；量程非法时退回自动。
 *
 * 未接入（上游靠多 Measure + ImageCache 支撑）：多线绑定、HorizontalShift、
 * LineStart/LineEnd 等，待 Image 底座（MODULE_EXTRACTION.md:172）落地后再评估。
 */
#include "MeterLine.h"

#include "Canvas.h"
#include "ConfigParser.h"
#include "Measure.h"

#include <d2d1helper.h>

#include <algorithm>

namespace raindock {

MeterLine::~MeterLine()
{
    if (m_LineBrush) { m_LineBrush->Release(); m_LineBrush = nullptr; }
    if (m_Stroke)    { m_Stroke->Release();    m_Stroke = nullptr; }
    if (m_BrushRT)   { m_BrushRT->Release();   m_BrushRT = nullptr; }
}

void MeterLine::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    m_Measure = measure;

    // 与 MeterBar 同：折线图无 AutoSize，尺寸只能显式给出。
    m_W = parser.ReadInt(m_Name, L"W", m_W);
    m_H = parser.ReadInt(m_Name, L"H", m_H);

    m_LineColor = parser.ReadColor(m_Name, L"LineColor", m_LineColor);
    m_LineWidth = static_cast<float>(parser.ReadFloat(m_Name, L"LineWidth", m_LineWidth));
    m_AutoScale = parser.ReadBool(m_Name, L"AutoScale", m_AutoScale);

    // LineCount 是队列长度，至少要 2 个点才谈得上「折线」。
    const int lineCount = parser.ReadInt(m_Name, L"LineCount", static_cast<int>(m_MaxSamples));
    m_MaxSamples = static_cast<size_t>(lineCount > 1 ? lineCount : 2);

    // 几何参数可能变化，旧 StrokeStyle 必须失效后重建。
    if (m_Stroke) { m_Stroke->Release(); m_Stroke = nullptr; }

    m_Samples.clear();
    if (m_Measure) PushSample(m_Measure->GetValue());
}

void MeterLine::Update()
{
    Meter::Update();
    // 只取数，不驱动 Measure（Measure 由 Skin::Update 单点驱动，见 Meter.cpp:49-51）。
    if (m_Measure) PushSample(m_Measure->GetValue());
}

void MeterLine::PushSample(double v)
{
    m_Samples.push_back(v);
    if (m_Samples.size() > m_MaxSamples) m_Samples.pop_front();
}

void MeterLine::ComputeRange()
{
    if (!m_AutoScale && m_Measure) {
        const double mn = m_Measure->GetMinValue();
        const double mx = m_Measure->GetMaxValue();
        if (mx > mn) {
            m_Min = mn;
            m_Max = mx;
            return;
        }
        // 量程非法（MaxValue 未设置）→ 落到自动，否则整条线会被压成一条水平线。
    }

    // 自动量程以 0 为基线（速率/占用类指标从 0 起读才有意义）。
    double peak = 0.0;
    for (const double v : m_Samples) {
        if (v > peak) peak = v;
    }
    m_Min = 0.0;
    // 全零（如网络空闲）时给一个单位高度，避免除零导致坐标变成 inf。
    m_Max = (peak > 0.0) ? peak : 1.0;
}

void MeterLine::Draw(ID2D1RenderTarget* rt)
{
    if (!rt || m_W <= 0 || m_H <= 0) return;
    if (m_Samples.size() < 2) return;

    if (!m_LineBrush || m_BrushRT != rt) {
        if (m_LineBrush) { m_LineBrush->Release(); m_LineBrush = nullptr; }
        if (m_BrushRT)   { m_BrushRT->Release();   m_BrushRT = nullptr; }
        if (FAILED(rt->CreateSolidColorBrush(m_LineColor, &m_LineBrush)) || !m_LineBrush) return;
        m_BrushRT = rt;
        m_BrushRT->AddRef();
    }

    // 线宽由 StrokeStyle 承载；只在宽度非默认值时创建（默认 1.0f 可传 nullptr 走 D2D 默认）。
    if (!m_Stroke && m_LineWidth > 0.0f) {
        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap   = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        ID2D1Factory* factory = Canvas::GetD2DFactory();
        if (factory) {
            factory->CreateStrokeStyle(props, nullptr, 0, &m_Stroke);   // 失败则留 nullptr，走默认线型
        }
    }

    ComputeRange();

    const float left = static_cast<float>(m_X);
    const float top  = static_cast<float>(m_Y);
    const float w    = static_cast<float>(m_W);
    const float h    = static_cast<float>(m_H);
    const double span = m_Max - m_Min;

    // 最新样本贴右边缘，历史向左排；采样未满时左侧留白（曲线从右边生长），
    // 这样横轴步长恒定，不会因样本数变化而横向伸缩。
    const float stepX = (m_MaxSamples > 1)
                      ? (w / static_cast<float>(m_MaxSamples - 1))
                      : 0.0f;
    const float startX = left + w - stepX * static_cast<float>(m_Samples.size() - 1);

    D2D1_POINT_2F prev = D2D1::Point2F(startX, top + h);
    for (size_t i = 0; i < m_Samples.size(); ++i) {
        double ratio = (m_Samples[i] - m_Min) / span;
        ratio = std::clamp(ratio, 0.0, 1.0);
        const float x = startX + stepX * static_cast<float>(i);
        const float y = top + h - h * static_cast<float>(ratio);
        if (i > 0) rt->DrawLine(prev, D2D1::Point2F(x, y), m_LineBrush, m_LineWidth, m_Stroke);
        prev = D2D1::Point2F(x, y);
    }
}

}  // namespace raindock
