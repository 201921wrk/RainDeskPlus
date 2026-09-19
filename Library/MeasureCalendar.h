/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 日历 Measure（D22-23）。产出「整月网格」多行字符串：表头 + 6 行 × 7 列日期，
 * 皮肤侧一个 MeterString（Text=%1）即可显示整月。
 *
 * 时间基准口径与 MeasureTime 完全一致（TimeStamp / TimeZone / DaylightSavingTime），
 * 因此时钟与日历在同一皮肤上不会出现「时钟已跨天、日历还是昨天」的错位。
 *
 * INI 键：
 *   Year=                0（默认）= 用参考时刻所在年；非 0 则固定为指定年。
 *   Month=               0（默认）= 用参考时刻所在月；1..12 则固定为指定月。
 *                        Year 与 Month 同时给出固定值时，网格与「今天」完全可复现
 *                        （离线回归用），今日标记只在固定月与参考月一致时才出现。
 *   TimeStamp=           Unix 秒。>=0 用固定时间戳做参考时刻；缺省/-1 = 系统当前时间。
 *   TimeZone=            相对 UTC 的分钟数（正 = 东，如北京 +480）。0 = 按系统默认。
 *   DaylightSavingTime=  1（默认）考虑夏令时；0 则按不含 DST 的固定时区换算。
 *   WeekStart=           0（默认）= 周日为一周首日；1 = 周一。
 *   HighlightToday=      1（默认）在今日格尾追加标记；0 = 不标记。
 *   TodayMark=           "*"（默认）今日标记字符（取首字符；留空 = 不标记）。
 *
 * 网格约定：每格固定 3 字符宽（日期右对齐 + 1 位标记位），列间 1 个空格，
 * 故每行 27 字符、表头与日期行等宽，等宽字体下天然对齐。本月之外的格子留白。
 * 行数固定 6 行，保证挂件高度不随月份跳动。
 *
 * m_Value = 当月天数（供 Bar/Line 等数值型 Meter 或 MinValue/MaxValue 归一化用）。
 */
#pragma once

#include "Measure.h"

#include <string>

namespace raindock {

class MeasureCalendar : public Measure
{
public:
    MeasureCalendar(Skin* skin, const WCHAR* name);
    ~MeasureCalendar() override = default;

    UINT GetTypeID() override { return TypeID<MeasureCalendar>(); }

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // 整月网格固定 6 行：避免挂件高度随月份（4~6 周）跳动。
    static constexpr int kRows = 6;

    int  m_Year  = 0;    // 0 = 跟随参考时刻
    int  m_Month = 0;    // 0 = 跟随参考时刻；1..12 = 固定月
    double m_TimeStamp = -1.0;   // >=0：固定 Unix 秒参考时刻

    bool m_UseDaylightSaving   = true;
    int  m_TimeZoneBiasMinutes = 0;      // 0 = 按系统默认
    int  m_WeekStart           = 0;      // 0 = 周日 / 1 = 周一

    bool         m_HighlightToday = true;
    wchar_t      m_TodayMarkChar  = L'*';
};

}  // namespace raindock
