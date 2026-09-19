/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 音频频谱 Measure（D25）。
 *
 * 上游对应 Rainmeter 的 PluginAudioLevel（Audio + AudioLevel + kiss_fft，
 * MODULE_EXTRACTION.md:188/205）。本项目不 vendor kiss_fft，改为自写 radix-2
 * FFT：零新增构建依赖，频段划分与产出契约由本类自己定义。
 *
 * 与其他 Measure 一致，本阶段是「离线可测骨架」：AudioFile= 指向一份单帧 PCM
 * 快照（采样率 + 采样点数组），无音频设备、无播放也能全量回归；真实 WASAPI
 * loopback 采集作为同一接口的第二段后续接上，届时快照来源换成实时采样，
 * Analyze() 的入参与产出契约不变。
 *
 * INI 键：
 *   AudioFile=  快照路径，默认 audio_state.json。绝对路径直接用；相对路径依次相对
 *               皮肤 @Resources 目录、皮肤 INI 所在目录解析。
 *   Field=      本段产出哪个读数，默认 Band1：
 *                 Band1..Band8  8 个对数（≈倍频程）频段的电平，0..1
 *                 Level         整帧时域电平（RMS 归一化），0..1
 *                 Peak          整帧时域峰值，0..1
 *               未知取值一律回落 Band1。
 *   MaxValue=   量程，默认 1（本类读数已全部归一到 0..1）。一般无需显式给出。
 *
 * 快照格式（单帧，标称幅度 -1..1 的单声道浮点）：
 *   { "sampleRate": 44100, "samples": [0.0, 0.12, -0.35, ...] }
 *   sampleRate 缺省按 44100；samples 缺失 / 非数组 / 空数组视同无有效报文。
 *
 * 输出约定：
 *   m_Value        Field= 选中读数的 0..1 归一化值，可直接绑 MeterBar。
 *   m_StringValue  百分比文本（如 "72%"）；不可用时为占位 "-"。不能留空串，否则
 *                  基类 GetString() 会回退成裸数值（Measure.cpp:141-145）。
 *   未显式给量程时本类补 MaxValue=1，否则 MeterBar 因 span<=0 恒取 0
 *   （MeterBar.cpp:71-74）。
 *
 * 失败回退：快照缺失 / JSON 畸形 / 无 samples 时沿用上一次成功值（数值本就留在
 * m_Value 上，字符串靠 m_LastString 自留一份），不打回 0；从未成功过则值为 0、
 * 字符串取占位 "-"。
 */
#pragma once

#include "Measure.h"

#include <array>
#include <cstddef>

namespace raindock {

class MeasureAudio : public Measure
{
public:
    // 频段数：8 段，按几何级数覆盖 60Hz..12kHz（每段约一个倍频程），
    // 与挂件的 8 根竖条一一对应（Field=Band1..Band8）。
    static constexpr int kBandCount = 8;

    // 分析窗长（radix-2）。采样不足补零，超出只取最新的一窗。
    static constexpr int kFftSize = 1024;

    // 一帧的分析结果。
    struct Frame
    {
        std::array<double, kBandCount> bands{};  // 各频段电平，0..1
        double level = 0.0;                      // 时域 RMS 归一化，0..1
        double peak  = 0.0;                      // max|sample|，0..1
    };

    MeasureAudio(Skin* skin, const WCHAR* name);
    ~MeasureAudio() override = default;

    UINT GetTypeID() override { return TypeID<MeasureAudio>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

    // 单帧 PCM → 频段/电平/峰值。公开是为了让离线 Smoke 能直接喂正弦与静音，
    // 不必先落盘一份快照（真实采集接上后走的也是这条路径）。
    static Frame Analyze(const double* samples, std::size_t count, double sampleRate);

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // 本段产出哪一类读数（一个 Measure 段只对应一个数值）。
    enum class Field { Band, Level, Peak };

    // AudioFile= 相对路径解析（绝对路径/空值原样返回）。
    std::wstring ResolveStatePath(const std::wstring& raw);

    // 失败回退：有成功基准则灌回上一次字符串，否则给该字段的空形态。
    void RestoreLastValue();

    std::wstring m_StateFile;                 // 解析后的可用路径
    Field        m_Field     = Field::Band;
    int          m_BandIndex = 0;             // Field::Band 时有效，0 基（对应 Band1）

    // 回退基准：Measure::Update() 每帧都会清空 m_StringValue（Measure.cpp:90），
    // 故上一次成功产出的字符串必须自己留一份，否则回退时只剩裸数值。
    bool         m_HasValue  = false;
    std::wstring m_LastString;
};

}  // namespace raindock
