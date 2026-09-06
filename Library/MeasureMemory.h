/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureMemory.h（内存占用，直接复用）。
 * 依赖：GlobalMemoryStatusEx。
 */
#pragma once

#include "Measure.h"

namespace raindock {

class MeasureMemory : public Measure
{
public:
    enum class Mode
    {
        UsedPercent,  // 已用百分比
        UsedBytes,    // 已用字节
        FreeBytes,    // 剩余字节
        TotalBytes    // 总字节
    };

    MeasureMemory(Skin* skin, const WCHAR* name);
    ~MeasureMemory() override = default;

    UINT GetTypeID() override { return TypeID<MeasureMemory>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    Mode m_Mode  = Mode::UsedPercent;
    bool m_Total = false;  // Rainmeter 原生 Total=键：Mode=Used 下强制返回 TotalBytes
};

}  // namespace raindock
