/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterBar.cpp（进度条，D2D 重构版）。
 *
 * 本版实现：
 *   - Initialize 读 W / H / BarColor / SolidColor / BarOrientation
 *   - Update 把 Measure 值按 [MinValue, MaxValue] 归一化到 0..1
 *   - Draw 先铺底槽再铺填充矩形（竖向自下而上增长，对齐上游默认方向）
 * 上游的 BarImage / BarBorder / Flip 依赖 ImageCache（MODULE_EXTRACTION.md:172
 * 尚未提取），留待 Image 底座落地后再接入。
 */
#include "MeterBar.h"

#include "ConfigParser.h"
#include "Measure.h"

#include <d2d1helper.h>

#include <algorithm>

namespace raindock {

MeterBar::~MeterBar()
{
    if (m_BarBrush)   { m_BarBrush->Release();   m_BarBrush = nullptr; }
    if (m_TrackBrush) { m_TrackBrush->Release(); m_TrackBrush = nullptr; }
    if (m_BrushRT)    { m_BrushRT->Release();    m_BrushRT = nullptr; }
}

void MeterBar::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    m_Measure = measure;

    // Bar 无 AutoSize 语义（对比 MeterString 由文本 metrics 定尺寸）：
    // 尺寸只能显式给出，缺省保持 0，由 Draw 的卫语句跳过。
    m_W = parser.ReadInt(m_Name, L"W", m_W);
    m_H = parser.ReadInt(m_Name, L"H", m_H);

    m_BarColor = parser.ReadColor(m_Name, L"BarColor", m_BarColor);
    // 底槽沿用 Rainmeter 的 SolidColor 键：Meter 基类的 m_SolidColor 尚未接入读取，
    // 这里直接读同名键，避免新造一个与上游冲突的键名。
    m_TrackColor = parser.ReadColor(m_Name, L"SolidColor", m_TrackColor);

    const std::wstring orient = parser.ReadString(m_Name, L"BarOrientation", L"Horizontal");
    m_Orientation = (_wcsicmp(orient.c_str(), L"Vertical") == 0)
                  ? Orientation::Vertical
                  : Orientation::Horizontal;

    // 首帧前 m_Value 必须有效：Skin 是「先 Measure 后 Meter」驱动，但
    // MeterBar 可能被独立构造（Smoke 直构场景），不能依赖 Update() 一定被调过。
    Update();
}

void MeterBar::Update()
{
    Meter::Update();

    if (!m_Measure) {
        m_Value = 0.0;
        return;
    }

    // GetValue() 已按 [MinValue, MaxValue] 裁剪与反转，此处只做相对刻度换算。
    // 量程为 0 表示「未设置范围」（Measure.h:116-118）——此时无法得到相对值，
    // 取 0 而不是让条恒满；需要条形的 Measure 必须在 INI 给出 MaxValue。
    const double minValue = m_Measure->GetMinValue();
    const double span = m_Measure->GetMaxValue() - minValue;
    const double ratio = (span > 0.0) ? ((m_Measure->GetValue() - minValue) / span) : 0.0;
    m_Value = std::clamp(ratio, 0.0, 1.0);
}

void MeterBar::Draw(ID2D1RenderTarget* rt)
{
    if (!rt || m_W <= 0 || m_H <= 0) return;

    // 两个 brush 同生共死：任一失败即整体重建，避免留下半套状态。
    if (!m_BarBrush || m_BrushRT != rt) {
        if (m_BarBrush)   { m_BarBrush->Release();   m_BarBrush = nullptr; }
        if (m_TrackBrush) { m_TrackBrush->Release(); m_TrackBrush = nullptr; }
        if (m_BrushRT)    { m_BrushRT->Release();    m_BrushRT = nullptr; }
        if (FAILED(rt->CreateSolidColorBrush(m_BarColor, &m_BarBrush)) || !m_BarBrush) return;
        if (FAILED(rt->CreateSolidColorBrush(m_TrackColor, &m_TrackBrush)) || !m_TrackBrush) {
            m_BarBrush->Release();
            m_BarBrush = nullptr;
            return;
        }
        m_BrushRT = rt;
        m_BrushRT->AddRef();
    }

    const float left = static_cast<float>(m_X);
    const float top  = static_cast<float>(m_Y);
    const float w    = static_cast<float>(m_W);
    const float h    = static_cast<float>(m_H);

    // 先铺底槽：小比例时纯填充几乎不可见，底槽让「还差多少」可读。
    rt->FillRectangle(D2D1::RectF(left, top, left + w, top + h), m_TrackBrush);

    const float ratio = static_cast<float>(m_Value);
    if (ratio <= 0.0f) return;

    if (m_Orientation == Orientation::Vertical) {
        // 竖向自下而上增长（对齐上游 MeterBar 的默认方向）。
        const float barH = h * ratio;
        rt->FillRectangle(D2D1::RectF(left, top + h - barH, left + w, top + h), m_BarBrush);
    } else {
        rt->FillRectangle(D2D1::RectF(left, top, left + w * ratio, top + h), m_BarBrush);
    }
}

}  // namespace raindock
