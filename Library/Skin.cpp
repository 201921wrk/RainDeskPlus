/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Skin.cpp（简化版骨架）。
 * TODO(Phase1): 提取 Rainmeter 实现：
 *   - 按 [Rainmeter] 段读 Update= 周期、背景、位置
 *   - 实例化 [Measure*] 段 -> MeasureXXX
 *   - 实例化 [Meter*] 段 -> MeterXXX，绑定同名 Measure
 *   - 窗口创建：替换为 Duilib 窗口（见 UI/DuilibWindowBase）
 */
#include "Skin.h"

#include "Measure.h"
#include "Meter.h"
#include "ConfigParser.h"

namespace raindock {

Skin::Skin()
    : m_Parser(std::make_unique<ConfigParser>())
{
}

Skin::~Skin() = default;

bool Skin::Load(const std::wstring& iniPath)
{
    if (!m_Parser->LoadFile(iniPath)) return false;

    // TODO(Phase1): 遍历段名，按前缀 Measure / Meter 实例化并绑定。
    //   for (section : parser.GetSections())
    //     if (section starts with "Measure") factory->CreateMeasure(section)
    //     else if (starts with "Meter") factory->CreateMeter(section, boundMeasure)
    m_UpdateInterval = static_cast<uint32_t>(
        m_Parser->ReadInt(L"Rainmeter", L"Update", 1000));
    return true;
}

void Skin::Update()
{
    for (auto& m : m_Measures) m->Update();
    for (auto& t : m_Meters)  t->Update();
}

void Skin::Render(ID2D1RenderTarget* rt)
{
    if (!rt) return;
    for (auto& t : m_Meters) t->Draw(rt);
}

void Skin::DoBang(const std::wstring& /*bang*/)
{
    // TODO(Phase1): 由 CRainmeter -> CommandHandler 统一分发，Skin 仅持有上下文。
}

}  // namespace raindock
