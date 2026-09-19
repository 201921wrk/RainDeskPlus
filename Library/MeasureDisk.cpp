/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 磁盘空间 Measure（自实现，上游 FreeDiskSpace 未 vendor 到本仓库）。
 *
 * 口径对齐上游 FreeDiskSpace：
 *   - 分母（m_MaxValue）恒为「该盘总容量」，故 MeterBar 拿到的相对值
 *     就是「剩余可用空间占比」；Total=1 时 m_Value == m_MaxValue，条恒满。
 *   - 分子默认取 GetDiskFreeSpaceExW 的「调用者可用字节」（含配额限制），
 *     而非第三个参数 totalFreeBytes（该盘物理剩余，忽略配额）。
 */
#include "MeasureDisk.h"
#include "ConfigParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cwctype>
#include <iomanip>
#include <sstream>

namespace raindock {

namespace {

constexpr double kKiB = 1024.0;

// 取系统盘盘符，形如 L"C:"。SystemDrive 环境变量最直接；个别精简系统没有该
// 变量，此时退化为 Windows 目录首两字符。
std::wstring SystemDrive()
{
    WCHAR buf[MAX_PATH] = {};
    DWORD len = ::GetEnvironmentVariableW(L"SystemDrive", buf, MAX_PATH);
    if (len != 2 || buf[1] != L':') {
        len = ::GetWindowsDirectoryW(buf, MAX_PATH);
    }
    if (len >= 2 && buf[1] == L':') {
        return std::wstring{ static_cast<wchar_t>(std::towupper(buf[0])), L':' };
    }
    // 兜底：即便上面全失败，返回 C: 也不会崩，只是采样可能失败并归零。
    return L"C:";
}

// 归一化盘符：只接受「单个字母 + 可选冒号 + 可选反斜杠」（如 "C" / "C:" / "C:\"），
// 输出恒为 L"C:"。空值或非法值（含 "C:\Users" 这类长路径）退回系统盘——
// 长路径不是本 Measure 的口径，静默接受会把「某个目录所在盘」误当作 Drive 配置。
std::wstring NormalizeDrive(const std::wstring& raw)
{
    size_t begin = 0;
    size_t end   = raw.size();
    while (begin < end && (raw[begin] == L' ' || raw[begin] == L'\t')) ++begin;
    while (end > begin && (raw[end - 1] == L' ' || raw[end - 1] == L'\t')) --end;

    const size_t len = end - begin;
    if (len >= 1 && len <= 3) {
        const wchar_t c = raw[begin];
        const bool isAlpha = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
        const bool colonOk = (len < 2) || (raw[begin + 1] == L':');
        const bool slashOk = (len < 3) || (raw[begin + 2] == L'\\');
        if (isAlpha && colonOk && slashOk) {
            return std::wstring{ static_cast<wchar_t>(std::towupper(c)), L':' };
        }
    }

    return SystemDrive();
}

// 字节数转人类可读串（如 L"12.3 GB"），供 MeterString 的 %1 直接显示。
std::wstring FormatBytes(double bytes)
{
    static const wchar_t* const kUnits[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
    constexpr int kLastUnit = 5;

    double value = bytes;
    int unit = 0;
    while (value >= kKiB && unit < kLastUnit) {
        value /= kKiB;
        ++unit;
    }

    std::wostringstream oss;
    // 字节档位不带小数（"512 B" 比 "512.0 B" 自然），其余保留一位。
    oss << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << L' ' << kUnits[unit];
    return oss.str();
}

}  // namespace

MeasureDisk::MeasureDisk(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
    , m_Drive(SystemDrive())
{
    // 构造期只定默认盘符，不做采样：真值要等 ReadOptions 读完 Drive= 之后
    // 才有意义（见 MeasureDisk.h 的生命周期说明）。
}

void MeasureDisk::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    // 保留构造期已确定的 m_MaxValue：基类 ReadOptions 会以 MaxValue= 的默认值 0
    // 覆盖它（Measure.h:118 的「未设置范围」语义），而本 Measure 的分母是
    // 「总容量」，被清零会破坏量纲（与 MeasureMemory.cpp:41-46 同一处理）。
    const double oldMax = m_MaxValue;
    Measure::ReadOptions(parser, section);
    m_MaxValue = oldMax;

    m_Drive = NormalizeDrive(parser.ReadString(section, L"Drive", L""));
    m_Total = parser.ReadBool(section, L"Total", m_Total);
}

void MeasureDisk::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);
}

void MeasureDisk::UpdateValue()
{
    ULARGE_INTEGER freeToCaller{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFree{};
    // 尾部反斜杠是 GetDiskFreeSpaceExW 的要求：传 L"C:" 会被解释为
    // 「C 盘上的当前目录」，其可用空间变成慢速查询且语义不同。
    const std::wstring root = m_Drive + L"\\";
    if (!::GetDiskFreeSpaceExW(root.c_str(), &freeToCaller, &totalBytes, &totalFree)) {
        // 盘不存在（如已拔出的移动盘）：归零而非沿用上次读数，
        // 避免 Meter 继续显示一个已经不成立的旧值。
        m_MaxValue = 0.0;
        m_Value = 0.0;
        return;
    }

    const double total = static_cast<double>(totalBytes.QuadPart);
    m_MaxValue = total;
    m_Value = m_Total ? total : static_cast<double>(freeToCaller.QuadPart);
    m_StringValue = FormatBytes(m_Value);
}

}  // namespace raindock
