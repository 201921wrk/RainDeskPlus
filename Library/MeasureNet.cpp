/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureNet.cpp（网络流量，M2 真实现）。
 *
 * 采样算法（对齐上游 MeasureNet 语义，计数器升级 64 位）：
 *   - 上游用 GetIfTable + MIB_IFROW（32 位计数器，现代网卡驱动普遍不正确上报）；
 *     本实现改用 GetIfTable2 + MIB_IF_ROW2（64 位 InOctets/OutOctets，NDIS 真实统计）。
 *   - Interface=Best（默认）→ 选非回环、流量(In+Out Octets)最大的接口（上游 SelectBestInterface 思路）；
 *     Interface=Total → 所有非回环接口求和；Interface=<N> → 按 InterfaceIndex 指定。
 *   - Cumulative=0（默认）→ 输出速率（Δbytes / Δ秒）；Cumulative=1 → 输出累计值。
 *   - 64 位计数器无回绕；MIB_IF_ROW2 有专用 In/OutMulticastOctets（multicast 字节）。
 */
#include "MeasureNet.h"
#include "ConfigParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>   // 定义 _WS2IPDEF_：netioapi.h 的 MIB_IF_ROW2/GetIfTable2 由该宏解锁
#include <iphlpapi.h>

#include <algorithm> // std::all_of
#include <cwctype>   // std::iswdigit
#include <vector>

namespace raindock {

namespace {

// 按 NetType 取 MIB_IF_ROW2 对应计数器（64 位）
uint64_t RowCounter(const MIB_IF_ROW2& row, MeasureNet::NetType type)
{
    using T = MeasureNet::NetType;
    switch (type) {
    case T::InOctets:     return row.InOctets;
    case T::OutOctets:    return row.OutOctets;
    case T::InError:      return row.InErrors;
    case T::OutError:     return row.OutErrors;
    case T::InUcast:      return row.InUcastPkts;
    case T::OutUcast:     return row.OutUcastPkts;
    case T::InMulticast:  return row.InMulticastOctets;
    case T::OutMulticast: return row.OutMulticastOctets;
    }
    return 0;
}

bool IsLoopback(const MIB_IF_ROW2& row)
{
    return row.Type == IF_TYPE_SOFTWARE_LOOPBACK;
}

}  // namespace

MeasureNet::MeasureNet(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
}

void MeasureNet::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    const std::wstring type = parser.ReadString(section, L"Type", L"InOctets");
    if      (type == L"InOctets")     m_Type = NetType::InOctets;
    else if (type == L"OutOctets")    m_Type = NetType::OutOctets;
    else if (type == L"InError")      m_Type = NetType::InError;
    else if (type == L"OutError")     m_Type = NetType::OutError;
    else if (type == L"InUcast")      m_Type = NetType::InUcast;
    else if (type == L"OutUcast")     m_Type = NetType::OutUcast;
    else if (type == L"InMulticast")  m_Type = NetType::InMulticast;
    else if (type == L"OutMulticast") m_Type = NetType::OutMulticast;

    m_Cumulative    = parser.ReadBool(section, L"Cumulative", m_Cumulative);
    m_InterfaceName = parser.ReadString(section, L"Interface", m_InterfaceName);

    // 解析 Interface=：Best（默认）/ Total / 数字（dwIndex）。
    // 旧实现用 _wtoi 直接解析，形如 "3abc" 的畸形值会被静默当作索引 3；
    // 这里要求整串均为数字，其余取值（含尚未支持的网络接口名）显式回退
    // Best，避免把非法配置当成有效索引（详见 D10 审查 #23）。
    m_IfaceMode = IfaceMode::Best;
    m_IfaceIndex = -1;
    if (_wcsicmp(m_InterfaceName.c_str(), L"Total") == 0)
    {
        m_IfaceMode = IfaceMode::Total;
    }
    else if (!m_InterfaceName.empty() && _wcsicmp(m_InterfaceName.c_str(), L"Best") != 0)
    {
        const bool allDigits = std::all_of(m_InterfaceName.begin(), m_InterfaceName.end(),
            [](wchar_t c) { return std::iswdigit(c) != 0; });
        const int idx = allDigits ? _wtoi(m_InterfaceName.c_str()) : 0;
        if (idx > 0)
        {
            m_IfaceMode  = IfaceMode::Index;
            m_IfaceIndex = idx;
        }
    }
}

void MeasureNet::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);
    m_HaveLast = false;
    m_LastRawValue = 0;
    m_LastTickMs = 0;
}

void MeasureNet::UpdateValue()
{
    // GetIfTable2：系统分配表缓冲，用完 FreeMibTable 释放
    PMIB_IF_TABLE2 table = nullptr;
    if (::GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return;  // 保留上一次 m_Value
    }

    // 第一遍：Best 模式找流量最大的非回环接口（64 位口径）
    ULONG64 bestIndex = 0;
    ULONG64 bestFlow = 0;
    if (m_IfaceMode == IfaceMode::Best) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2& row = table->Table[i];
            if (IsLoopback(row)) continue;
            const ULONG64 flow = row.InOctets + row.OutOctets;
            if (flow > bestFlow) { bestFlow = flow; bestIndex = row.InterfaceIndex; }
        }
    }

    // 第二遍：按模式累计所选接口的计数器
    ULONG64 raw = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        switch (m_IfaceMode) {
        case IfaceMode::Best:
            if (row.InterfaceIndex != bestIndex) continue;
            break;
        case IfaceMode::Index:
            if (row.InterfaceIndex != static_cast<ULONG64>(m_IfaceIndex)) continue;
            break;
        case IfaceMode::Total:
            if (IsLoopback(row)) continue;
            break;
        }
        raw += RowCounter(row, m_Type);
    }

    ::FreeMibTable(table);
    table = nullptr;

    const uint64_t nowTick = ::GetTickCount64();
    if (m_Cumulative) {
        m_Value = static_cast<double>(raw);
        m_HaveLast = true;
        m_LastRawValue = raw;
        m_LastTickMs = nowTick;
        return;
    }

    // 速率模式：首次采样只记基准
    if (!m_HaveLast) {
        m_HaveLast = true;
        m_LastRawValue = raw;
        m_LastTickMs = nowTick;
        m_Value = 0.0;
        return;
    }
    const uint64_t dRaw = (raw >= m_LastRawValue) ? (raw - m_LastRawValue) : 0;
    const uint64_t dMs  = (nowTick > m_LastTickMs) ? (nowTick - m_LastTickMs) : 0;
    m_LastRawValue = raw;
    m_LastTickMs   = nowTick;
    if (dMs == 0) return;  // 同 tick：保留上次速率
    m_Value = static_cast<double>(dRaw) * 1000.0 / static_cast<double>(dMs);
}

}  // namespace raindock
