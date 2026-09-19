/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterLine.h（折线图，直接复用）。
 * D2D 版实现：环形采样队列 + 逐段 DrawLine，尺寸取自 INI 的 W=/H=。
 */
#pragma once

#include "Meter.h"

#include <deque>
#include <vector>

struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;

namespace raindock {

class MeterLine : public Meter
{
public:
    MeterLine(Skin* skin, const WCHAR* name) : Meter(skin, name) {}
    ~MeterLine() override;

    UINT GetTypeID() override { return TypeID<MeterLine>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Draw(ID2D1RenderTarget* rt) override;
    void Update() override;

    void PushSample(double v);   // 推入一个采样点

private:
    // 重算纵轴量程并写入 m_Min / m_Max（保证 m_Max > m_Min，Draw 可直接做除法）。
    void ComputeRange();

    std::deque<double> m_Samples;     // 历史采样（最新在尾部）
    size_t             m_MaxSamples = 60;
    D2D1_COLOR_F       m_LineColor = {0.2f, 0.6f, 1.0f, 1.0f};
    float              m_LineWidth = 1.0f;
    bool               m_AutoScale = true;
    double             m_Min = 0.0, m_Max = 1.0;

    // Brush / StrokeStyle 与渲染目标绑定：rt 变化时重建（COM 引用持有 rt）。
    ID2D1SolidColorBrush* m_LineBrush = nullptr;
    ID2D1StrokeStyle*     m_Stroke    = nullptr;
    ID2D1RenderTarget*    m_BrushRT   = nullptr;
};

}  // namespace raindock
