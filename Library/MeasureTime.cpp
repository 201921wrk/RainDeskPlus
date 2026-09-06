/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasureTime.cpp（时间 Measure，直接复用）。
 * M1 提取要点：
 *   - 用 MSVC CRT 的 wcsftime/_wcsftime_l 做宽字符格式化（避免 MBCS 转码）；
 *   - 支持 Format / FormatLocale / DaylightSavingTime / TimeStamp；
 *   - GetString() 经基类 Substitute；m_Value = 自 Unix epoch 起的秒数（double）。
 *
 * TODO(M2): 接入 TimeStamp = "DSTStart" / "DST_NEXT_START" 语法（本版只支持数字秒）。
 */
#include "MeasureTime.h"
#include "ConfigParser.h"

#include <windows.h>

#include <ctime>
#include <locale.h>
#include <wchar.h>

namespace raindock {

MeasureTime::MeasureTime(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
}

MeasureTime::~MeasureTime()
{
    if (m_FormatLocaleHandle) {
        _free_locale(static_cast<_locale_t>(m_FormatLocaleHandle));
        m_FormatLocaleHandle = nullptr;
    }
}

void MeasureTime::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    m_Format = parser.ReadString(section, L"Format", L"%H:%M:%S");

    const std::wstring locale = parser.ReadString(section, L"FormatLocale", L"");
    if (locale != m_FormatLocale) {
        m_FormatLocale = locale;
        if (m_FormatLocaleHandle) {
            _free_locale(static_cast<_locale_t>(m_FormatLocaleHandle));
            m_FormatLocaleHandle = nullptr;
        }
        if (!m_FormatLocale.empty()) {
            // _create_locale(LC_TIME, name)；失败则静默回退到当前系统 locale
            std::string nameA(m_FormatLocale.size() * 3 + 1, '\0');
            const int n = ::WideCharToMultiByte(CP_ACP, 0, m_FormatLocale.c_str(), -1,
                nameA.data(), static_cast<int>(nameA.size()), nullptr, nullptr);
            if (n > 0) {
                nameA.resize(n - 1);
                _locale_t h = _create_locale(LC_TIME, nameA.c_str());
                if (h) m_FormatLocaleHandle = h;
            }
        }
    }

    m_UseDaylightSaving = parser.ReadBool(section, L"DaylightSavingTime", true);
    m_TimeZoneBiasMinutes = parser.ReadInt(section, L"TimeZone", 0);

    // TimeStamp：要么纯数字（秒），要么 -1（默认当前时间）
    const std::wstring ts = parser.ReadString(section, L"TimeStamp", L"-1");
    try {
        m_TimeStamp = std::stod(ts);
    } catch (...) {
        m_TimeStamp = -1.0;  // DSTStart 等关键字：M1 不支持，视为当前时间
    }
}

void MeasureTime::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);
}

void MeasureTime::UpdateValue()
{
    // 1) 选时间基准：要么 TimeStamp（秒自 1970），要么系统当前 time()
    std::time_t base;
    if (m_TimeStamp >= 0.0) {
        base = static_cast<std::time_t>(m_TimeStamp);
    } else {
        std::time_t now = std::time(nullptr);
        base = now;
    }

    // 2) 时区偏移 + 夏令时开关（_daylight 控制）
    if (m_TimeZoneBiasMinutes != 0) {
        // TimeZone 正向加（如北京 +8 → +480 分钟，相对 UTC 东移）
        base += static_cast<std::time_t>(m_TimeZoneBiasMinutes) * 60;
    }
    // 对 DaylightSavingTime=0 场景：
    //   MSVC CRT 无公开 _set_daylight()，旧的 __daylight 被弃用且写入不可靠。
    //   改为：直接用 UTC 换算——先调用 gmtime_s 得到不带 DST 的分解结构，
    //   再在分解基础上额外叠加 (TimeZone + 本地固定偏差)，模拟「不考虑夏令时」的 local。
    std::tm tmBuf{};
    if (m_UseDaylightSaving) {
        ::localtime_s(&tmBuf, &base);
    } else {
        // 1) UTC 分解
        ::gmtime_s(&tmBuf, &base);
        // 2) 本地时区偏差（固定部分，不含 DST）
        long tzBiasMin = 0;
        _get_timezone(&tzBiasMin);  // UTC 西向为正（CRT 惯例），秒数
        tzBiasMin /= 60;            // → 分钟
        // 用户在 TimeZone 中指定：正向加是往东，相当于减少「UTC 西向」偏差
        tzBiasMin -= m_TimeZoneBiasMinutes;
        // 3) 对分解结构手工加 tzBiasMin（负是东加正是西减），并 normalize
        std::time_t epoch = ::_mkgmtime(&tmBuf);
        if (epoch != -1) {
            epoch -= static_cast<std::time_t>(tzBiasMin) * 60;
            ::gmtime_s(&tmBuf, &epoch);
        }
    }

    // 3) 格式化：优先 FormatLocale 句柄 → 否则默认 wcsftime
    if (!m_Format.empty()) {
        std::wstring buf(256, L'\0');
        size_t written = 0;
        if (m_FormatLocaleHandle) {
            written = _wcsftime_l(&buf[0], buf.size(), m_Format.c_str(), &tmBuf,
                static_cast<_locale_t>(m_FormatLocaleHandle));
        } else {
            written = wcsftime(&buf[0], buf.size(), m_Format.c_str(), &tmBuf);
        }
        buf.resize(written);
        m_StringValue = std::move(buf);
    } else {
        m_StringValue.clear();
    }
    // m_Value = 自 epoch 起秒数（符合 Rainmeter GetValue(RelativeValue) 语义）
    m_Value = static_cast<double>(base);
}

const wchar_t* MeasureTime::GetString()
{
    // 惰性：若字符串还没填（一般 Update 之前调用），先采样一次
    if (m_StringValue.empty()) UpdateValue();
    return CheckSubstitute(m_StringValue.c_str());
}

}  // namespace raindock
