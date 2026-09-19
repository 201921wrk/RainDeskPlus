/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureMemory.cpp（直接复用）。
 *
 * Rainmeter 语义说明（本实现对齐）：
 *   - 统计范围是「Physical + PageFile」（即系统可提交总限额），不是纯物理内存。
 *   - m_MaxValue 每次采样重算 = ullTotalPhys + ullTotalPageFile（pagefile 可动态变）。
 *   - Total= 键（Rainmeter 原生）为 true 则 m_Value = m_MaxValue（不分 Mode）。
 *
 * 本版附加友好 Mode 键：Mode=UsedPercent / UsedBytes / FreeBytes / TotalBytes。
 * Physical 纯物理内存子类留给 M2 新增 PhysicalMeasureMemory。
 */
#include "MeasureMemory.h"
#include "ConfigParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>

namespace raindock {

MeasureMemory::MeasureMemory(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
    MEMORYSTATUSEX stat{};
    stat.dwLength = sizeof(stat);
    if (::GlobalMemoryStatusEx(&stat)) {
        m_MaxValue = static_cast<double>(
            static_cast<unsigned long long>(stat.ullTotalPhys) +
            static_cast<unsigned long long>(stat.ullTotalPageFile));
    }
}

void MeasureMemory::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    // 保留 Measure 未读取的 m_MaxValue 语义：用户手写 MaxValue= 优先级最高。
    const double oldMax = m_MaxValue;
    Measure::ReadOptions(parser, section);
    m_MaxValue = oldMax;

    m_Total = parser.ReadBool(section, L"Total", m_Total);

    const std::wstring mode = parser.ReadString(section, L"Mode", L"UsedPercent");
    if (mode == L"UsedPercent") m_Mode = Mode::UsedPercent;
    else if (mode == L"UsedBytes") m_Mode = Mode::UsedBytes;
    else if (mode == L"FreeBytes") m_Mode = Mode::FreeBytes;
    else if (mode == L"TotalBytes") m_Mode = Mode::TotalBytes;
}

void MeasureMemory::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);
}

void MeasureMemory::UpdateValue()
{
    MEMORYSTATUSEX stat{};
    stat.dwLength = sizeof(stat);
    if (!::GlobalMemoryStatusEx(&stat)) {
        m_Value = 0.0;
        return;
    }

    // 对齐上游：Phys + PageFile 作为 MaxValue（pagefile 动态，每次都重算）
    const unsigned long long totalPhys = static_cast<unsigned long long>(stat.ullTotalPhys);
    const unsigned long long totalPage = static_cast<unsigned long long>(stat.ullTotalPageFile);
    const unsigned long long availPhys = static_cast<unsigned long long>(stat.ullAvailPhys);
    const unsigned long long availPage = static_cast<unsigned long long>(stat.ullAvailPageFile);
    const unsigned long long totalAll  = totalPhys + totalPage;
    const unsigned long long availAll  = availPhys + availPage;
    const unsigned long long usedAll   = (totalAll > availAll) ? (totalAll - availAll) : 0ULL;

    if (m_Total) {
        m_MaxValue = static_cast<double>(totalAll);
        m_Value = static_cast<double>(totalAll);
        return;
    }
    switch (m_Mode) {
    case Mode::TotalBytes:
        m_MaxValue = static_cast<double>(totalAll);
        m_Value = static_cast<double>(totalAll);
        break;
    case Mode::UsedBytes:
        m_MaxValue = static_cast<double>(totalAll);
        m_Value = static_cast<double>(usedAll);
        break;
    case Mode::FreeBytes:
        m_MaxValue = static_cast<double>(totalAll);
        m_Value = static_cast<double>(availAll);
        break;
    case Mode::UsedPercent:
    default:
        // dwMemoryLoad 是系统给出的百分比（0-100），故 MaxValue 必须同为 100；
        // 否则 Bar/Meter 会拿「字节总量」当分母做相对刻度，导致条永远贴满
        // （详见 D10 审查 #8）。
        m_MaxValue = 100.0;
        m_Value = static_cast<double>(stat.dwMemoryLoad);
        break;
    }
}

}  // namespace raindock
