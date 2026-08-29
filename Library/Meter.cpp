/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Meter.cpp（基类）。
 * M3：公共定位选项 X/Y/Hidden；W/H 由子类决定（如 MeterString 用文本 metrics）。
 */
#include "Meter.h"

#include "ConfigParser.h"
#include "Measure.h"

namespace raindock {

void Meter::Initialize(ConfigParser& parser, Measure* measure)
{
    m_Measure = measure;
    m_X = parser.ReadInt(m_Name, L"X", 0);
    m_Y = parser.ReadInt(m_Name, L"Y", 0);
    m_Hidden = parser.ReadBool(m_Name, L"Hidden", false);
}

void Meter::Update()
{
    // 触发绑定的 Measure 刷新；具体皮肤驱动里在 Update 周期统一调用。
    if (m_Measure) m_Measure->Update();
}

}  // namespace raindock
