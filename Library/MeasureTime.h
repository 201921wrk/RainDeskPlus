/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureTime.h（时间/日期，直接复用，无外部依赖）。
 */
#pragma once

#include "Measure.h"

#include <ctime>
#include <string>

namespace raindock {

class MeasureTime : public Measure
{
public:
    MeasureTime(Skin* skin, const WCHAR* name);
    ~MeasureTime() override;

    UINT GetTypeID() override { return TypeID<MeasureTime>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;
    const wchar_t* GetString() override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    std::wstring m_Format;          // strftime/wcsftime 风格，如 L"%H:%M:%S"
    std::wstring m_FormatLocale;    // C 运行时 locale 名（空=当前系统 LC_TIME）
    void*        m_FormatLocaleHandle = nullptr;  // _create_locale 句柄，MSVC CRT

    double       m_TimeStamp = 0.0; // 若 >0：用固定 Unix 时间戳采样
    bool         m_UseDaylightSaving = true;     // DaylightSavingTime=0 则强制不考虑夏令时偏移
    int          m_TimeZoneBiasMinutes = 0;      // TimeZone=（分钟数相对 UTC，正=东）。0=按系统默认

    // GetString 的惰性补采样只允许发生一次（详见 D10 审查 #18）。
    bool         m_LazySampled = false;
};

}  // namespace raindock
