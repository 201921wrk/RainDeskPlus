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
#include "Skin.h"
#include "Mouse.h"

namespace raindock {

Meter::~Meter()
{
    delete m_Mouse;
    m_Mouse = nullptr;
}

::Mouse& Meter::GetMouse()
{
    if (!m_Mouse) m_Mouse = new ::Mouse(m_Skin, this);
    return *m_Mouse;
}

void Meter::Initialize(ConfigParser& parser, Measure* measure)
{
    m_Measure = measure;
    Section::ReadOptions(parser, m_Name);
    m_X = parser.ReadInt(m_Name, L"X", 0);
    m_Y = parser.ReadInt(m_Name, L"Y", 0);
    m_Hidden = parser.ReadBool(m_Name, L"Hidden", false);

    // 无 Skin 的独立 Meter（M3 Smoke 直构 MeterString，验证 INI→Measure→Meter 全链路）
    // 没有皮肤级鼠标设置可继承。上游 Mouse::ReadOptions 会无条件解引用 m_Skin
    // （如 m_Skin->GetMouse().GetCursorState()），在 Skin* == nullptr 时触发
    // 空指针访问 0xC0000005（D16 回归定位）。与 Section::GetDefaultUpdateDivider
    // 的空 Skin 处理保持一致。
    if (m_Skin) GetMouse().ReadOptions(parser, m_Name);
}

void Meter::Update()
{
    // Measure 的刷新由 Skin::Update() 单点驱动（先遍历 Measure 再遍历 Meter）。
    // 此处不得再调用 m_Measure->Update()，否则同一次 Update 周期内 Measure 被
    // 驱动两次，UpdateDivider 实际减半（详见 D10 审查 #4）。
}

}  // namespace raindock
