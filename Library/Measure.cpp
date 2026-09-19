/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Measure.cpp（基类）。
 * M1 提取：
 *   - 构造(skin,name) / ~Measure() / Initialize(parser,iniPath)
 *   - Update(bool rereadOptions)：按 UpdateDivider 跳帧，调 UpdateValue（纯虚，子类实现）
 *   - ReadOptions(parser, section)：公共选项 InvertMeasure / MinValue / MaxValue /
 *     UpdateDivider / Disabled / Substitute
 *   - GetValue：应用 InvertMeasure、裁剪到 [MinValue, MaxValue]
 *   - GetString / CheckSubstitute：对字符串按顺序做字面量 Substitute
 *
 * 上游 IfActions / Median / Average / AutoScale 等字段：M2 再引入。
 */
#include "Measure.h"
#include "ConfigParser.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace raindock {

Measure::Measure(Skin* skin, const WCHAR* name)
    : Section(skin, name)
{
}

Measure::~Measure() = default;

void Measure::Disable()    { m_Disabled = true; }
void Measure::Enable()     { m_Disabled = false; }

void Measure::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    // 公共 INI 键（名称与 Rainmeter 一一对应）；UpdateDivider 由 Section 统一管理。
    Section::ReadOptions(parser, section);
    m_IfActions.ReadOptions(parser, section);
    m_IfActions.ReadConditionOptions(parser, section);

    m_MinValue = parser.ReadFloat(section, L"MinValue", m_MinValue);
    m_MaxValue = parser.ReadFloat(section, L"MaxValue", m_MaxValue);
    m_Invert   = parser.ReadBool(section, L"InvertMeasure", m_Invert);
    m_Disabled = parser.ReadBool(section, L"Disabled", m_Disabled);

    // Substitute: "pattern1":"repl1","pattern2":"repl2" —— 引号 + 逗号分隔（Rainmeter 最简格式）
    // M1 用字面量替换实现（不支持正则），若子串内有 `\` 转义后续升级处理。
    const std::wstring raw = parser.ReadString(section, L"Substitute", L"");
    if (!raw.empty()) {
        ClearSubstitute();
        std::vector<std::wstring> tokens;
        std::wstring cur;
        bool inQuote = false;
        for (size_t i = 0; i < raw.size(); ++i) {
            const wchar_t c = raw[i];
            if (c == L'"') { inQuote = !inQuote; continue; }
            if (!inQuote && c == L',') {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
                continue;
            }
            cur.push_back(c);
        }
        if (!cur.empty()) tokens.push_back(cur);
        for (size_t i = 1; i < tokens.size(); i += 2) {
            AddSubstitute(tokens[i - 1], tokens[i]);
        }
    }
}

void Measure::Initialize(ConfigParser& parser, const std::wstring& /*iniPath*/)
{
    ReadOptions(parser, m_Name);
    m_Initialized = true;
}

void Measure::Update(bool rereadOptions)
{
    if (!m_Initialized) return;     // 先 Initialize
    if (m_Disabled) return;
    // rereadOptions：M1 阶段未接入（需要皮肤层重新喂 ConfigParser 句柄），
    // 留 hook 给 M2；当前语义下不读 INI 不会影响采样一致性。
    (void)rereadOptions;

    if (!Section::UpdateCounter()) {
        return;  // 跳帧：保持上一次 m_Value
    }
    m_StringValue.clear();  // 清空字符串缓存，GetString() 将按需重新格式化
    UpdateValue();  // 子类采样
    m_IfActions.DoIfActions(*this, GetValue());
    DoUpdateAction();
}

double Measure::GetValue()
{
    double v = m_Value;
    if (m_MaxValue > m_MinValue) {
        v = std::clamp(v, m_MinValue, m_MaxValue);
    }
    if (m_Invert && m_MaxValue > m_MinValue) {
        v = (m_MaxValue + m_MinValue) - v;
    }
    return v;
}

const wchar_t* Measure::CheckSubstitute(const wchar_t* src)
{
    if (!src) src = L"";

    // 无 Substitute 配置时无需改写：直接返回源串，省掉每帧一次整串拷贝
    // （详见 D10 审查 #20）。调用方传入的源串在本次使用期内均有效。
    if (m_Substitute.empty()) return src;

    m_Substituted = src;
    for (const auto& kv : m_Substitute) {
        if (kv.first.empty()) continue;
        std::wstring out;
        out.reserve(m_Substituted.size());
        size_t pos = 0;
        while (pos < m_Substituted.size()) {
            const size_t hit = m_Substituted.find(kv.first, pos);
            if (hit == std::wstring::npos) {
                out.append(m_Substituted, pos, m_Substituted.size() - pos);
                break;
            }
            out.append(m_Substituted, pos, hit - pos);
            out.append(kv.second);
            pos = hit + kv.first.size();
        }
        m_Substituted.swap(out);
    }
    return m_Substituted.c_str();
}

const wchar_t* Measure::GetString()
{
    // 若子类未设字符串值（纯数值 Measure），回退为数值格式化字符串。
    // 对齐 Rainmeter 语义：MeterString 的 %1 总是能拿到一个有意义的字符串。
    if (m_StringValue.empty()) {
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(1) << GetValue();
        m_StringValue = oss.str();
    }
    return CheckSubstitute(m_StringValue.c_str());
}

void Measure::Command(const std::wstring& /*command*/)
{
    // 默认 no-op（对齐上游 Measure 基类语义）：支持控制能力的子类自行 override。
    // 无命令可执行的 Measure 因此对 !CommandMeasure 静默无副作用。
}

void Measure::Finalize()
{
    m_StringValue.clear();
    m_Substituted.clear();
    ClearSubstitute();
}

}  // namespace raindock
