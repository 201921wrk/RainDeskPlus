/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureCPU.cpp（直接复用）。
 *
 * 采样算法（对齐上游 M1.3 提取范围）：
 *   - Processor=0（默认）: 用 Win32 GetSystemTimes() 得到累积 Idle/Kernel/User，
 *     基于 ΔIdle / Δ(Kernel+User+Idle) -> 1 - ΔIdle/ΔTotal 得到 % Processor Time。
 *   - Processor≥1（指定核）: M1 暂未接入 NtQuerySystemInformation 读单核累计（需要 ntdll 导出 +
 *     SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION 解析），M1 降级：自动按 Processor=0 走 Total。
 *
 * 与上游相同：m_MaxValue = 100.0，GetString() 空（留给 MeterString 取数值字符串）。
 */
#include "MeasureCPU.h"
#include "ConfigParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>  // std::clamp

namespace raindock {

namespace {

// FILETIME 拼到 100ns 单位 double（同 Rainmeter Ft2Double）
inline double FtToDouble(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<double>(u.QuadPart);
}

}  // namespace

MeasureCPU::MeasureCPU(Skin* skin, std::wstring name)
    : Measure(skin, std::move(name))
{
    m_MaxValue = 100.0;
    m_MinValue =   0.0;
}

MeasureCPU::~MeasureCPU()
{
    Finalize();
}

void MeasureCPU::ReadOptions(ConfigParser& parser, const std::wstring& section)
{
    Measure::ReadOptions(parser, section);

    // Rainmeter 约定 Processor=0 表示「整机 Total」，Processor>=1 表示核号（1-based）。
    int proc = parser.ReadInt(section, L"Processor", 0);
    if (proc < 0) proc = 0;
    if (proc == 0 || !parser.ReadBool(section, L"TotalProcessor", m_TotalProcessors)) {
        m_TotalProcessors = (proc == 0);
        m_ProcessorIndex = proc;
    } else {
        // 用户显式指定了 Processor=N>0，但 M1 不支持单核：保留字段但实际会在采样时降级为 Total。
        m_TotalProcessors = false;
        m_ProcessorIndex = proc;
    }
}

void MeasureCPU::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);
    // 预置历史采样基准：首次调用 UpdateValue() 时的 Idle/Total 会缓存为「上一次」。
    m_LastIdle100ns = 0.0;
    m_LastSys100ns  = 0.0;
}

void MeasureCPU::Finalize()
{
    m_LastIdle100ns = 0.0;
    m_LastSys100ns  = 0.0;
    Measure::Finalize();
}

void MeasureCPU::UpdateValue()
{
    // Processor>0 在 M1 降级为 Total（避免返回假 0，误导性更高）。
    FILETIME ftIdle{}, ftKernel{}, ftUser{};
    if (!::GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
        return;  // 保留上一次 m_Value
    }
    const double idle = FtToDouble(ftIdle);
    const double kerenl_and_user = FtToDouble(ftKernel) + FtToDouble(ftUser);
    const double sysTotal = idle + kerenl_and_user;

    const double dIdle = idle - m_LastIdle100ns;
    const double dSys  = sysTotal - m_LastSys100ns;
    m_LastIdle100ns = idle;
    m_LastSys100ns  = sysTotal;

    if (dSys <= 0.0) {
        // 两次采样同 tick 或倒走（极少见）：保持上次
        return;
    }
    const double usage = 1.0 - (dIdle / dSys);
    // 裁剪到 [0,1]，再换算百分比（对齐 MaxValue=100）
    const double pct = std::clamp(usage, 0.0, 1.0) * 100.0;
    m_Value = pct;
}

}  // namespace raindock
