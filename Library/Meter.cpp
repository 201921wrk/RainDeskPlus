/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Meter.cpp（基类骨架）。
 * TODO(Phase1): 提取 Rainmeter 实现（位置/尺寸/变换/绑定的 Measure 缓存）。
 */
#include "Meter.h"

#include "ConfigParser.h"
#include "Measure.h"

namespace raindock {

void Meter::Initialize(ConfigParser& parser, Measure* measure)
{
    m_Measure = measure;
    // TODO(Phase1): 读取 [MeterXxx] X/Y/W/H/SolidColor/Hidden/AntiAlias。
    (void)parser;
}

void Meter::Update()
{
    // 触发绑定的 Measure 刷新；具体皮肤驱动里在 Update 周期统一调用。
    if (m_Measure) m_Measure->Update();
}

}  // namespace raindock
