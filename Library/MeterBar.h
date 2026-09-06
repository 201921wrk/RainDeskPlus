/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterBar.h（进度条，直接复用）。
 */
#pragma once

#include "Meter.h"

namespace raindock {

class MeterBar : public Meter
{
public:
    enum class Orientation { Horizontal, Vertical };

    MeterBar(Skin* skin, const WCHAR* name) : Meter(skin, name) {}

    UINT GetTypeID() override { return TypeID<MeterBar>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Draw(ID2D1RenderTarget* rt) override;

private:
    Orientation   m_Orientation = Orientation::Horizontal;
    D2D1_COLOR_F  m_BarColor     = {0.2f, 0.6f, 1.0f, 1.0f};
    D2D1_COLOR_F  m_FillColor    = {1.0f, 1.0f, 1.0f, 0.4f};
    double        m_Value = 0.0;   // 0..1（已归一化）
};

}  // namespace raindock
