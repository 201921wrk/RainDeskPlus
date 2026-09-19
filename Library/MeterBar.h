/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterBar.h（进度条，直接复用）。
 * D2D 版实现：底槽 + 填充矩形，尺寸取自 INI 的 W=/H=（无 AutoSize）。
 */
#pragma once

#include "Meter.h"

struct ID2D1SolidColorBrush;

namespace raindock {

class MeterBar : public Meter
{
public:
    enum class Orientation { Horizontal, Vertical };

    MeterBar(Skin* skin, const WCHAR* name) : Meter(skin, name) {}
    ~MeterBar() override;

    UINT GetTypeID() override { return TypeID<MeterBar>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Update() override;
    void Draw(ID2D1RenderTarget* rt) override;

private:
    Orientation   m_Orientation = Orientation::Horizontal;
    D2D1_COLOR_F  m_BarColor     = {0.2f, 0.6f, 1.0f, 1.0f};
    D2D1_COLOR_F  m_TrackColor   = {1.0f, 1.0f, 1.0f, 0.4f};
    double        m_Value = 0.0;   // 0..1（已归一化，Update() 时从 Measure 重算）

    // Brush 与渲染目标绑定：rt 变化时重建（COM 引用持有 rt，保证比较/使用安全）。
    ID2D1SolidColorBrush* m_BarBrush   = nullptr;
    ID2D1SolidColorBrush* m_TrackBrush = nullptr;
    ID2D1RenderTarget*    m_BrushRT    = nullptr;
};

}  // namespace raindock
