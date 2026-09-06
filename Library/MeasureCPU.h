/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureCPU.h（CPU 占用率，直接复用）。
 * 依赖：Windows 性能计数器（Pdh）。
 */
#ifndef RAINDOCK_LIBRARY_MEASURE_CPU_H_
#define RAINDOCK_LIBRARY_MEASURE_CPU_H_

#include "Measure.h"

namespace raindock {

class MeasureCPU : public Measure
{
public:
    MeasureCPU(Skin* skin, const WCHAR* name);
    ~MeasureCPU() override;

    UINT GetTypeID() override { return TypeID<MeasureCPU>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;
    void Finalize() override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // Pdh 查询句柄（void* 让头避免引入 pdh.h；实现里强转）——M1 暂未启用 Pdh，先占位
    void* m_Query = nullptr;    // HQUERY
    void* m_Counter = nullptr;  // HCOUNTER
    bool  m_TotalProcessors = true;   // MeasureCPU 默认整机 Processor(_Total)\% Processor Time
    int   m_ProcessorIndex = 0;       // Processor=0-N 指定核

    // GetSystemTimes 差分基准（上一次采样的 100ns 单位累计值）
    double m_LastIdle100ns = 0.0;
    double m_LastSys100ns  = 0.0;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_MEASURE_CPU_H_
