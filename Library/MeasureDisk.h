/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 磁盘空间 Measure。上游的 FreeDiskSpace 源码未 vendor，本类按同语义自实现。
 * 依赖：GetDiskFreeSpaceExW。
 *
 * INI 键：
 *   Drive=  目标盘符，接受 "C" / "C:" / "C:\"，缺省（或非法值）退回系统盘。
 *   Total=  true → 返回该盘总容量；false（默认）→ 返回剩余可用容量。
 *
 * 归一化语义：m_MaxValue 恒为该盘总容量，故 MeterBar 读到的相对值即
 * 「剩余空间占比」（Total=1 时恒为 1.0，适合只要数字不要条的场景）。
 */
#pragma once

#include "Measure.h"

namespace raindock {

class MeasureDisk : public Measure
{
public:
    MeasureDisk(Skin* skin, const WCHAR* name);
    ~MeasureDisk() override = default;

    UINT GetTypeID() override { return TypeID<MeasureDisk>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    std::wstring m_Drive;          // 归一化后的盘符，形如 L"C:"（不带尾随反斜杠）
    bool         m_Total = false;  // true=总容量，false=剩余可用容量
};

}  // namespace raindock
