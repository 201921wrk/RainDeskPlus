/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureNet.h（网络流量，直接复用）。
 * 依赖：IP Helper API（GetIfTable / GetIfEntry2）。
 */
#pragma once

#include "Measure.h"

#include <cstdint>

namespace raindock {

class MeasureNet : public Measure
{
public:
    enum class NetType
    {
        InOctets,     // 入流量（字节）
        OutOctets,    // 出流量
        InError,      // 入错包
        OutError,
        InUcast,      // 单播
        OutUcast,
        InMulticast,
        OutMulticast
    };

    MeasureNet(Skin* skin, const WCHAR* name);
    ~MeasureNet() override = default;

    UINT GetTypeID() override { return TypeID<MeasureNet>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // Interface= 解析结果（对齐上游 SelectBestInterface / Total / 指定编号三种语义）
    enum class IfaceMode { Best, Total, Index };
    IfaceMode m_IfaceMode   = IfaceMode::Best;
    int       m_IfaceIndex  = -1;      // IfaceMode::Index 时的 dwIndex

    NetType  m_Type        = NetType::InOctets;
    uint64_t m_LastRawValue = 0;       // 上一周期累计计数（速率差分基准）
    uint64_t m_LastTickMs   = 0;       // 上一周期 GetTickCount64()
    bool     m_HaveLast     = false;  // 首次采样只记录基准不产出速率
    bool     m_Cumulative  = false;   // true=累计值（上游 Cumulative），false=每秒速率
    std::wstring m_InterfaceName;      // Interface= 原始值（Best/Total/数字）
};

}  // namespace raindock
