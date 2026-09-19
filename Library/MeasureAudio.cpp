/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 音频频谱 Measure（契约与 INI 键见 MeasureAudio.h）。
 *
 * 数据流：AudioFile= 快照 → FileUtil::ReadTextFile（BOM 判编码，无 BOM 按 UTF-8）
 * → JsonParser 解析出 {sampleRate, samples} → Analyze() 做 radix-2 FFT →
 * 8 个频段电平 / 整帧电平 / 峰值 → 按 Field= 取一个写入 m_Value + m_StringValue。
 *
 * FFT 说明：输入是实数（PCM），这里按最直白的做法把虚部填 0 后做复数 FFT，
 * 只取前半谱。1024 点的规模下这点冗余远小于代码可读性的收益。
 */
#include "MeasureAudio.h"

#include "ConfigParser.h"
#include "FileUtil.h"
#include "JsonParser.h"
#include "PathUtil.h"
#include "Skin.h"
#include "StringUtil.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cwctype>
#include <string_view>
#include <vector>

namespace raindock {

namespace {

// 字段缺失时的占位：不能让 m_StringValue 留空，否则基类会回退成裸数值。
const wchar_t* const kPlaceholder = L"-";

// AudioFile= 未给出时的默认快照名（相对皮肤目录）。
const wchar_t* const kDefaultSnapshotFile = L"audio_state.json";

// 快照未声明采样率时的默认值（CD 音质）。
constexpr double kDefaultSampleRate = 44100.0;

// 频段覆盖范围：8 段按几何级数平分 60Hz..12kHz，每段约一个倍频程。
// 下限避开直流与工频，上限取到多数内容仍可辨识的高频区。
constexpr double kBandLowHz  = 60.0;
constexpr double kBandHighHz = 12000.0;

const double kPi   = 3.14159265358979323846;
const double kSqrt2 = 1.41421356237309504880;

// 原地 radix-2 Cooley-Tukey FFT（n 必须是 2 的幂）。
// 上游用 kiss_fft 覆盖同一需求（MODULE_EXTRACTION.md:205）；此处自写是为了
// 零新增构建依赖，且本项目只用到这一种规格（定长 1024、实数输入）。
void FftRadix2(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    if (n < 2) return;

    // 位反转置换：把自然序输入摆成蝶形所需的倒序。
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    // 逐级蝶形：len = 2, 4, ..., n。
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wLen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]             = u + v;
                a[i + k + len / 2]   = u - v;
                w *= wLen;
            }
        }
    }
}

// 一帧 PCM 快照。
struct AudioSnapshot
{
    bool               valid = false;
    double             sampleRate = kDefaultSampleRate;
    std::vector<double> samples;
};

// 解析快照。valid 的判据是「能解析成 JSON 对象，且 samples 是非空数组」；
// 空数组按「无有效报文」处理 —— 一帧都没有采样点时没有任何可分析的内容。
AudioSnapshot ParseSnapshot(const std::wstring& text)
{
    AudioSnapshot snapshot;

    JsonValue root;
    if (!JsonParser::Parse(text, root) || !root.IsObject()) return snapshot;

    const JsonValue* samples = root.Find(L"samples");
    if (!samples || !samples->IsArray() || samples->Size() == 0) return snapshot;

    // 非数字元素按 0 处理（静音）而不是整帧判废：个别脏点不该毁掉整帧。
    snapshot.samples.reserve(samples->Size());
    for (std::size_t i = 0; i < samples->Size(); ++i) {
        const JsonValue& v = samples->At(i);
        snapshot.samples.push_back(v.IsNumber() ? v.AsNumber() : 0.0);
    }

    const JsonValue* rate = root.Find(L"sampleRate");
    if (rate && rate->IsNumber() && rate->AsNumber() > 0.0) {
        snapshot.sampleRate = rate->AsNumber();
    }

    snapshot.valid = true;
    return snapshot;
}

// "BandN" → N-1（1..kBandCount）；大小写不敏感，其余一律 -1。
int ParseBandIndex(std::wstring_view raw)
{
    // 前缀全小写：输入先经 towlower 归一，两边必须在同一大小写口径上比对。
    constexpr std::wstring_view kPrefix = L"band";
    if (raw.size() <= kPrefix.size()) return -1;

    for (std::size_t i = 0; i < kPrefix.size(); ++i) {
        if (std::towlower(raw[i]) != kPrefix[i]) return -1;
    }

    int n = 0;
    for (std::size_t i = kPrefix.size(); i < raw.size(); ++i) {
        if (raw[i] < L'0' || raw[i] > L'9') return -1;   // 只认纯数字，拒绝 Band1x
        n = n * 10 + static_cast<int>(raw[i] - L'0');
        if (n > MeasureAudio::kBandCount) return -1;
    }

    return (n >= 1) ? n - 1 : -1;
}

// 0..1 → 百分比文本（"72%"）。字符串挂件直接可读，数值挂件的量纲不变。
std::wstring FormatPercent(double value)
{
    wchar_t buf[16] = {};
    ::swprintf_s(buf, L"%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
    return buf;
}

}  // namespace

MeasureAudio::MeasureAudio(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
    // 构造期不读盘：真值要等 ReadOptions 读完 AudioFile= 才有意义
    // （与 MeasureMedia / MeasureWeather 的生命周期约定一致）。
}

MeasureAudio::Frame MeasureAudio::Analyze(const double* samples, std::size_t count, double sampleRate)
{
    Frame frame;
    if (!samples || count == 0) return frame;
    if (!(sampleRate > 0.0)) sampleRate = kDefaultSampleRate;

    // 只取最新的一窗：快照/采集帧可以长于 kFftSize，陈旧部分对"当前频谱"无意义。
    const std::size_t used = (count < static_cast<std::size_t>(kFftSize))
                                 ? count
                                 : static_cast<std::size_t>(kFftSize);
    const double* const src = samples + (count - used);

    // 时域两项与频谱无关，先算：静音帧自然全 0，不必额外分支。
    double peak  = 0.0;
    double sumSq = 0.0;
    for (std::size_t i = 0; i < used; ++i) {
        const double v = src[i];
        if (std::fabs(v) > peak) peak = std::fabs(v);
        sumSq += v * v;
    }
    frame.peak = std::clamp(peak, 0.0, 1.0);
    // 满量程正弦的 RMS 是 1/√2，乘 √2 把它归一到 1，可直接当 0..1 电平读。
    frame.level = std::clamp(std::sqrt(sumSq / static_cast<double>(used)) * kSqrt2, 0.0, 1.0);

    std::vector<std::complex<double>> spec(static_cast<std::size_t>(kFftSize),
                                           std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < used; ++i) {
        spec[i] = std::complex<double>(src[i], 0.0);   // 实输入：虚部补零
    }
    FftRadix2(spec);

    const double binHz  = sampleRate / static_cast<double>(kFftSize);
    const double ratio  = kBandHighHz / kBandLowHz;
    // 归一化基准 used/2：单频满量程正弦在某个 bin 上的幅度就是 used/2。
    // 以实际参与变换的点数为基准（而非补零后的 kFftSize），短帧因此不会被低估。
    const double norm = 2.0 / static_cast<double>(used);
    const int maxBin  = kFftSize / 2 - 1;   // 实数谱只取前半，直流 bin 0 弃用

    for (int b = 0; b < kBandCount; ++b) {
        const double f0 = kBandLowHz * std::pow(ratio, static_cast<double>(b) / kBandCount);
        const double f1 = kBandLowHz * std::pow(ratio, static_cast<double>(b + 1) / kBandCount);

        // 低频段窄到可能不足一个 bin（尤其短帧），此时退化为最近的那个 bin，
        // 而不是留下空段让挂件出现"断条"。
        int b0 = std::clamp(static_cast<int>(std::llround(f0 / binHz)), 1, maxBin);
        int b1 = std::clamp(static_cast<int>(std::llround(f1 / binHz)), b0, maxBin);

        // 取段内最强 bin 而不是均值：高段覆盖的 bin 数远多于低段（8 段里最多
        // 相差数十倍），均值会把单频能量按 bin 数稀释，使高段恒定偏低。
        double mag = 0.0;
        for (int k = b0; k <= b1; ++k) {
            const double m = std::abs(spec[static_cast<std::size_t>(k)]);
            if (m > mag) mag = m;
        }
        frame.bands[static_cast<std::size_t>(b)] = std::clamp(mag * norm, 0.0, 1.0);
    }

    return frame;
}

std::wstring MeasureAudio::ResolveStatePath(const std::wstring& raw)
{
    if (raw.empty() || PathUtil::IsAbsolute(raw)) return raw;

    // 相对路径按「皮肤 @Resources → 皮肤 INI 目录」顺序解析。两者都没有时保留
    // 原样，交给 CreateFile 按进程工作目录解析（Smoke 直构 Measure 时走这条）。
    Skin* skin = GetSkin();
    if (skin) {
        const std::wstring& resources = skin->GetResourcesPath();
        if (!resources.empty()) return resources + raw;

        const std::wstring& iniPath = skin->GetIniPath();
        if (!iniPath.empty()) {
            return PathUtil::GetFolderFromFilePath(iniPath) + raw;
        }
    }
    return raw;
}

void MeasureAudio::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);

    // 已解析过的绝对路径再走一次 ResolveStatePath 是幂等的（IsAbsolute 直接返回）。
    m_StateFile = ResolveStatePath(
        parser.ReadString(section, L"AudioFile", kDefaultSnapshotFile));

    const std::wstring field = parser.ReadString(section, L"Field", L"Band1");
    m_BandIndex = 0;
    const int band = ParseBandIndex(field);
    if (band >= 0) {
        m_Field     = Field::Band;
        m_BandIndex = band;
    } else if (StringUtil::EqualsIgnoreCase(field, L"Level")) {
        m_Field = Field::Level;
    } else if (StringUtil::EqualsIgnoreCase(field, L"Peak")) {
        m_Field = Field::Peak;
    } else {
        // Band1 及任何未知取值都按 Band1 处理：默认字段必须是能出条的那个。
        m_Field = Field::Band;
    }

    // 所有读数都是 0..1 归一化量纲，未显式给量程时补默认上限，否则 MeterBar 因
    // span<=0 恒取 0（MeterBar.cpp:71-74）。显式写了 MaxValue= 的用户配置优先。
    if (m_MaxValue <= m_MinValue) m_MaxValue = 1.0;
}

void MeasureAudio::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);

    // 重置回退基准：皮肤重新加载后不得沿用上一份皮肤的旧读数。
    m_HasValue = false;
    m_LastString.clear();
    m_Value = 0.0;
}

void MeasureAudio::RestoreLastValue()
{
    if (m_HasValue) {
        m_StringValue = m_LastString;
        return;
    }

    // 从未成功过：给出占位串。数值留在 0（m_Value 不被覆写即保持初值）。
    m_StringValue = kPlaceholder;
}

void MeasureAudio::UpdateValue()
{
    std::wstring text;
    if (m_StateFile.empty() || !FileUtil::ReadTextFile(m_StateFile, text)) {
        RestoreLastValue();   // 快照缺失/不可读
        return;
    }

    const AudioSnapshot snapshot = ParseSnapshot(text);
    if (!snapshot.valid) {
        RestoreLastValue();   // JSON 畸形或无 samples
        return;
    }

    const Frame frame = Analyze(snapshot.samples.data(), snapshot.samples.size(),
                               snapshot.sampleRate);

    double value = 0.0;
    switch (m_Field) {
    case Field::Level: value = frame.level; break;
    case Field::Peak:  value = frame.peak;  break;
    case Field::Band:
    default:
        value = frame.bands[static_cast<std::size_t>(std::clamp(m_BandIndex, 0, kBandCount - 1))];
        break;
    }

    m_Value = std::clamp(value, 0.0, 1.0);

    const std::wstring out = FormatPercent(m_Value);
    m_StringValue = out;
    m_LastString  = out;
    m_HasValue    = true;
}

}  // namespace raindock
