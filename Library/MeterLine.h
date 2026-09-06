/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterLine.h（折线图，直接复用）。
 */
#pragma once

#include "Meter.h"

#include <deque>
#include <vector>

namespace raindock {

class MeterLine : public Meter
{
public:
    MeterLine(Skin* skin, const WCHAR* name) : Meter(skin, name) {}

    UINT GetTypeID() override { return TypeID<MeterLine>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Draw(ID2D1RenderTarget* rt) override;
    void Update() override;

    void PushSample(double v);   // 推入一个采样点

private:
    std::deque<double> m_Samples;     // 历史采样
    size_t             m_MaxSamples = 60;
    D2D1_COLOR_F       m_LineColor = {0.2f, 0.6f, 1.0f, 1.0f};
    float              m_LineWidth = 1.0f;
    bool               m_AutoScale = true;
    double             m_Min = 0.0, m_Max = 1.0;
};

}  // namespace raindock
