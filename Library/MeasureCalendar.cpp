/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 日历 Measure（契约与 INI 键见 MeasureCalendar.h）。
 *
 * 数据流：参考时刻（TimeStamp 或系统当前 → 时区/DST 换算 → struct tm）→
 *         定年定月 → 当月天数 + 当月 1 日星期 → 按 WeekStart 折算首格偏移 →
 *         拼「表头 + 6 行 × 7 列」多行字符串 → m_StringValue。
 *
 * 为什么行数固定 6 行：一个月的周数在 4~6 之间浮动，若按需裁行，String Meter 的
 * AutoSize 会让挂件高度逐月跳动；固定 6 行（不足留白）换取稳定版式。
 */
#include "MeasureCalendar.h"
#include "ConfigParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <ctime>

namespace raindock {

namespace {

// 单元格固定 3 字符宽（日期右对齐 + 1 位标记位）：表头与日期行等宽，等宽字体下对齐。
constexpr int kCellWidth = 3;

const wchar_t* const kDayNames[7] = {
    L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"
};

bool IsLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInMonth(int year, int month)
{
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return kDays[month - 1];
}

// 参考时刻 → 本地分解结构（与 MeasureTime::UpdateValue 同一口径，保证时钟/日历同步）。
void ResolveReferenceTime(double timeStamp, int timeZoneBiasMinutes,
                          bool useDaylightSaving, std::tm& out)
{
    std::time_t base = (timeStamp >= 0.0)
                           ? static_cast<std::time_t>(timeStamp)
                           : std::time(nullptr);

    // TimeZone 正向加（北京 +8 → +480 分钟，相对 UTC 东移），只叠加一次。
    if (timeZoneBiasMinutes != 0) {
        base += static_cast<std::time_t>(timeZoneBiasMinutes) * 60;
    }

    if (useDaylightSaving) {
        ::localtime_s(&out, &base);
        return;
    }

    // DaylightSavingTime=0：MSVC CRT 无可靠的 _set_daylight()，改为「UTC 分解 +
    // 本地固定时区偏差」模拟不含 DST 的 localtime。base 已叠加过用户 TimeZone，
    // 此处不得再减一次（否则用户偏移被重复施加，详见 D10 审查 #7）。
    ::gmtime_s(&out, &base);
    long tzBiasSeconds = 0;
    _get_timezone(&tzBiasSeconds);   // 返回「UTC 西向为正」的秒数
    const std::time_t epoch = ::_mkgmtime(&out);
    if (epoch != -1) {
        const std::time_t shifted = epoch - static_cast<std::time_t>(tzBiasSeconds);
        ::gmtime_s(&out, &shifted);
    }
}

}  // namespace

MeasureCalendar::MeasureCalendar(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
    // 构造期不采样：真值要等 ReadOptions 读完 Year/Month/TimeStamp 才有意义
    // （与 MeasureWeather / MeasureDisk 的生命周期约定一致）。
}

void MeasureCalendar::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    m_Year  = parser.ReadInt(section, L"Year", 0);
    m_Month = parser.ReadInt(section, L"Month", 0);

    // TimeStamp：纯数字（秒）为固定参考时刻；缺省或非法 → -1（系统当前时间）。
    const std::wstring ts = parser.ReadString(section, L"TimeStamp", L"-1");
    try {
        m_TimeStamp = std::stod(ts);
    } catch (...) {
        m_TimeStamp = -1.0;
    }

    m_TimeZoneBiasMinutes = parser.ReadInt(section, L"TimeZone", 0);
    m_UseDaylightSaving   = parser.ReadBool(section, L"DaylightSavingTime", true);

    // WeekStart：非 0 一律按周一（对齐 Rainmeter 里 0/1 的布尔式取值习惯）。
    m_WeekStart = (parser.ReadInt(section, L"WeekStart", 0) != 0) ? 1 : 0;

    m_HighlightToday = parser.ReadBool(section, L"HighlightToday", true);
    const std::wstring mark = parser.ReadString(section, L"TodayMark", L"*");
    m_TodayMarkChar = mark.empty() ? L'\0' : mark[0];
}

void MeasureCalendar::UpdateValue()
{
    // 1) 参考时刻：给出确定「今天」，同时兜底 Year=/Month= 的默认值。
    std::tm ref{};
    ResolveReferenceTime(m_TimeStamp, m_TimeZoneBiasMinutes, m_UseDaylightSaving, ref);

    const int refYear  = ref.tm_year + 1900;
    const int refMonth = ref.tm_mon + 1;
    const int refDay   = ref.tm_mday;

    const int year  = (m_Year != 0) ? m_Year : refYear;
    const int month = (m_Month >= 1 && m_Month <= 12) ? m_Month : refMonth;

    const int daysInMonth = DaysInMonth(year, month);

    // 固定年/月与参考月不一致时不存在「今日」，避免把当前日期标到别的月份上。
    const int todayDay = (year == refYear && month == refMonth) ? refDay : 0;

    // 2) 当月 1 日星期（0 = 周日）。以正午为基准，规避 DST 边界造成的日期漂移。
    std::tm first{};
    first.tm_year  = year - 1900;
    first.tm_mon   = month - 1;
    first.tm_mday  = 1;
    first.tm_hour  = 12;
    first.tm_isdst = -1;
    const int firstWday = (::mktime(&first) != -1) ? first.tm_wday : 0;

    // 首格列偏移：把「1 日星期」折算到 WeekStart 起算的列号。
    const int offset = ((firstWday - m_WeekStart) % 7 + 7) % 7;

    // 3) 拼网格：表头 + 固定 6 行。
    std::wstring grid;
    grid.reserve(7 * (kCellWidth + 1) * (kRows + 1));

    for (int col = 0; col < 7; ++col) {
        if (col) grid += L' ';
        grid += kDayNames[(m_WeekStart + col) % 7];
    }

    int day = 1;
    for (int row = 0; row < kRows; ++row) {
        grid += L'\n';
        for (int col = 0; col < 7; ++col) {
            if (col) grid += L' ';

            const int cell     = row * 7 + col;
            const bool inMonth = (cell >= offset) && (day <= daysInMonth);
            if (!inMonth) {
                grid.append(kCellWidth, L' ');   // 本月之外：留白
                continue;
            }

            wchar_t buf[kCellWidth + 1] = {};
            if (m_HighlightToday && m_TodayMarkChar != L'\0' && day == todayDay) {
                swprintf_s(buf, L"%2d%c", day, m_TodayMarkChar);
            } else {
                swprintf_s(buf, L"%2d ", day);
            }
            grid += buf;
            ++day;
        }
    }

    m_StringValue = std::move(grid);
    m_Value = static_cast<double>(daysInMonth);
}

}  // namespace raindock
