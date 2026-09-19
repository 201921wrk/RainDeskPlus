/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 天气 Measure（数据源 OpenWeatherMap 当前天气）。
 *
 * D20-21 为「离线可测骨架」：本阶段只做「读缓存报文 → 解析 → 产出数值/字符串」，
 * 真实联网拉取（WinHTTP）作为同一接口的第二段后续接上，因此无 Key、无网络的环境
 * 也能全量回归。
 *
 * INI 键：
 *   ApiKey=         OWM API Key。留空时回退环境变量 OPENWEATHER_API_KEY
 *                   （仓库内不放真实 Key，示例皮肤留占位值）。
 *   City=           城市名（如 Beijing / Beijing,CN）；报文缺 name 字段时用它兜底显示。
 *   Units=          metric（默认，摄氏度）/ imperial（华氏度）。
 *   CacheFile=      缓存报文路径。绝对路径直接用；相对路径依次相对皮肤 @Resources
 *                   目录、皮肤 INI 所在目录解析（无皮肤上下文时按进程工作目录）。
 *   ExpireMinutes=  报文 dt 距今超过该分钟数即视为过期；0 = 不做过期判断（默认 30）。
 *
 * 失败回退：缓存缺失 / 无法解析 / 报错体（无 main 对象）时沿用上一次成功值（数值与
 * 字符串一并保留），不把画面打回 0；从未成功过则值为 0、字符串为空（MeterString 落到
 * "0.0"）。过期数据仍照常展示（离线时可得的唯一数据就是它），仅在字符串尾部追加
 * " (cache stale)" 提示。
 */
#pragma once

#include "Measure.h"

namespace raindock {

class MeasureWeather : public Measure
{
public:
    MeasureWeather(Skin* skin, const WCHAR* name);
    ~MeasureWeather() override = default;

    UINT GetTypeID() override { return TypeID<MeasureWeather>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

    // 是否已拿到可用 Key（INI 优先，环境变量兜底）。后续接入联网拉取时由它决定能否请求。
    bool HasApiKey() const { return !m_ApiKey.empty(); }

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // CacheFile= 相对路径解析（绝对路径/空值原样返回）。
    std::wstring ResolveCachePath(const std::wstring& raw);

    // 失败回退：把上一次成功产出的字符串灌回 m_StringValue。
    void RestoreLastValue();

    std::wstring m_ApiKey;
    std::wstring m_City         = L"Beijing";
    bool         m_Imperial     = false;   // Units=imperial → 华氏度
    std::wstring m_CacheFile;              // 解析后的可用路径
    int          m_ExpireMinutes = 30;

    // 回退基准：Measure::Update() 每帧都会清空 m_StringValue，故上一次成功产出的
    // 字符串必须自己留一份，否则回退时只剩裸数值（城市/天气描述全丢）。
    bool         m_HasValue  = false;
    std::wstring m_LastString;
};

}  // namespace raindock
