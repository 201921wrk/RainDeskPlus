/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/ConfigParser.cpp（骨架 + Rainmeter 关键逻辑提取）。
 *
 * 提取逻辑：
 *   - INI 按段解析（段头：[SectionName]）；
 *   - 注释：行首 ; 或 #（[Rainmeter] 区以 # 开头作为特殊注释）；
 *   - [Variables] 区加载后平铺到 m_Variables；
 *   - ReadString 等所有读取器会自动做 #Var#/$$ 变量展开（ReplaceVariables），
 *     这是 Rainmeter 皮肤动态引用的核心语义。
 *
 * M1 阶段刻意保留「就地升级」方式：对外 API 与骨架完全一致，
 * 骨架里 17 个调用点无需改动；后续 M2 再接入 @Include /
 * 动态变量 / MathParser 公式 / Substitute 等更高级功能。
 */
#include "ConfigParser.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <fstream>
#include <sstream>

namespace raindock {

namespace {

std::wstring Trim(const std::wstring& s)
{
    const auto is_ws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    auto a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    auto b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// 单通道 ARGB -> [0,1] float（Rainmeter 的 ARGBHEX / RRGGBB / AARRGGBB 惯例）
float ByteNorm(uint8_t b) { return static_cast<float>(b) / 255.0f; }

bool ParseHexByte(const std::wstring& s, size_t off, uint8_t* out)
{
    if (off + 2 > s.size()) return false;
    unsigned val = 0;
    for (size_t i = 0; i < 2; ++i) {
        wchar_t c = s[off + i];
        unsigned nib;
        if      (c >= L'0' && c <= L'9') nib = c - L'0';
        else if (c >= L'a' && c <= L'f') nib = 10 + (c - L'a');
        else if (c >= L'A' && c <= L'F') nib = 10 + (c - L'A');
        else return false;
        val = (val << 4) | nib;
    }
    *out = static_cast<uint8_t>(val);
    return true;
}

}  // namespace

// -----------------------------------------------------------------------
// 解析
// -----------------------------------------------------------------------

bool ConfigParser::LoadFile(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    LoadFromString(content);
    return true;
}

void ConfigParser::LoadFromString(const std::string& utf8Content)
{
    m_Sections.clear();
    m_SectionOrder.clear();
    m_Variables.clear();

    std::istringstream ss(utf8Content);
    std::string lineUtf8;
    std::wstring current;

    while (std::getline(ss, lineUtf8))
    {
        std::wstring line = FromUtf8(lineUtf8);
        line = Trim(line);

        if (line.empty()) continue;
        // 注释：行首 ; 或 #
        if (line[0] == L';' || line[0] == L'#') continue;

        if (line.front() == L'[' && line.back() == L']') {
            current = Trim(line.substr(1, line.size() - 2));
            if (current.empty()) continue;
            auto [it, inserted] = m_Sections.try_emplace(current);
            if (inserted) m_SectionOrder.push_back(current);
            continue;
        }
        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(0, eq));
        std::wstring val = Trim(line.substr(eq + 1));
        if (key.empty() || current.empty()) continue;
        // Rainmeter 约定：段内重复键后者覆盖前者（允许 @Include 叠加）
        m_Sections[current][key] = val;
        if (current == L"Variables") m_Variables[key] = val;
    }
}

// -----------------------------------------------------------------------
// 变量展开
// -----------------------------------------------------------------------

bool ConfigParser::GetVariable(const std::wstring& name, std::wstring& value) const
{
    auto it = m_Variables.find(name);
    if (it == m_Variables.end()) return false;
    value = it->second;
    return true;
}

bool ConfigParser::ReplaceVariables(std::wstring& value) const
{
    // 单 pass：#name#、$$。最多迭代 3 次处理嵌套替换，避免病态循环爆炸。
    static constexpr int kMaxPass = 3;
    bool replacedAny = false;
    for (int pass = 0; pass < kMaxPass; ++pass) {
        bool changed = false;
        std::wstring out;
        out.reserve(value.size());
        const size_t n = value.size();
        for (size_t i = 0; i < n; ) {
            const wchar_t c = value[i];
            if (c == L'$' && i + 1 < n && value[i + 1] == L'$') {
                out.push_back(L'$');
                i += 2;
                changed = true;
                continue;
            }
            if (c == L'#') {
                const size_t end = value.find(L'#', i + 1);
                if (end != std::wstring::npos && end > i + 1) {
                    const std::wstring var = value.substr(i + 1, end - i - 1);
                    std::wstring resolved;
                    if (GetVariable(var, resolved)) {
                        out.append(resolved);
                        i = end + 1;
                        changed = true;
                        continue;
                    }
                }
            }
            out.push_back(c);
            ++i;
        }
        value.swap(out);
        if (changed) replacedAny = true; else break;
    }
    return replacedAny;
}

void ConfigParser::SetVariable(const std::wstring& name, const std::wstring& value)
{
    m_Variables[name] = value;
    m_Sections[L"Variables"][name] = value;
}

// -----------------------------------------------------------------------
// 字符串读取 + 数字/布尔 包装
// -----------------------------------------------------------------------

std::wstring ConfigParser::ReadString(const std::wstring& section,
                                       const std::wstring& key,
                                       const std::wstring& defValue) const
{
    auto it = m_Sections.find(section);
    if (it == m_Sections.end()) {
        std::wstring out = defValue;
        ReplaceVariables(out);
        return out;
    }
    auto k = it->second.find(key);
    if (k == it->second.end()) {
        std::wstring out = defValue;
        ReplaceVariables(out);
        return out;
    }
    std::wstring val = k->second;
    ReplaceVariables(val);
    return val;
}

int ConfigParser::ReadInt(const std::wstring& section, const std::wstring& key, int defValue) const
{
    const std::wstring s = ReadString(section, key, L"");
    if (s.empty()) return defValue;
    // Rainmeter 允许 0x / 前导 +/-，但禁止整行带后缀字母（无意义，回退默认）。
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos, 0);
        // 允许尾随空白，其它算不合法。
        for (; pos < s.size(); ++pos) {
            if (s[pos] != L' ' && s[pos] != L'\t') return defValue;
        }
        return v;
    } catch (...) {
        return defValue;
    }
}

uint32_t ConfigParser::ReadUInt(const std::wstring& section, const std::wstring& key, uint32_t defValue) const
{
    const std::wstring s = ReadString(section, key, L"");
    if (s.empty()) return defValue;
    try {
        size_t pos = 0;
        unsigned long v = std::stoul(s, &pos, 0);
        for (; pos < s.size(); ++pos) {
            if (s[pos] != L' ' && s[pos] != L'\t') return defValue;
        }
        return static_cast<uint32_t>(v);
    } catch (...) {
        return defValue;
    }
}

uint64_t ConfigParser::ReadUInt64(const std::wstring& section, const std::wstring& key, uint64_t defValue) const
{
    const std::wstring s = ReadString(section, key, L"");
    if (s.empty()) return defValue;
    try {
        size_t pos = 0;
        unsigned long long v = std::stoull(s, &pos, 0);
        for (; pos < s.size(); ++pos) {
            if (s[pos] != L' ' && s[pos] != L'\t') return defValue;
        }
        return v;
    } catch (...) {
        return defValue;
    }
}

double ConfigParser::ReadFloat(const std::wstring& section, const std::wstring& key, double defValue) const
{
    const std::wstring s = ReadString(section, key, L"");
    if (s.empty()) return defValue;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        for (; pos < s.size(); ++pos) {
            if (s[pos] != L' ' && s[pos] != L'\t') return defValue;
        }
        return v;
    } catch (...) {
        return defValue;
    }
}

bool ConfigParser::ReadBool(const std::wstring& section, const std::wstring& key, bool defValue) const
{
    std::wstring s = ReadString(section, key, L"");
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (s == L"1" || s == L"true" || s == L"yes")  return true;
    if (s == L"0" || s == L"false" || s == L"no") return false;
    return defValue;
}

// -----------------------------------------------------------------------
// 颜色/矩形/浮点数组（Rainmeter 通用格式）
// -----------------------------------------------------------------------

D2D1_COLOR_F ConfigParser::ReadColor(const std::wstring& section,
                                     const std::wstring& key,
                                     const D2D1_COLOR_F& defValue) const
{
    const std::wstring s = ReadString(section, key, L"");
    if (s.empty()) return defValue;

    // 1) Hex 形式：RRGGBB 或 AARRGGBB / 可选前导 0x
    std::wstring hex = s;
    if (hex.size() >= 2 && (hex[0] == L'0' && (hex[1] == L'x' || hex[1] == L'X'))) {
        hex = hex.substr(2);
    }
    if (hex.size() == 6 || hex.size() == 8) {
        bool ok = true;
        uint8_t c0 = 0, c1 = 0, c2 = 0, c3 = 0xFF;
        if (hex.size() == 6) {
            ok = ok && ParseHexByte(hex, 0, &c0); // R
            ok = ok && ParseHexByte(hex, 2, &c1); // G
            ok = ok && ParseHexByte(hex, 4, &c2); // B
        } else {
            ok = ok && ParseHexByte(hex, 0, &c3); // A
            ok = ok && ParseHexByte(hex, 2, &c0); // R
            ok = ok && ParseHexByte(hex, 4, &c1); // G
            ok = ok && ParseHexByte(hex, 6, &c2); // B
        }
        if (ok) {
            return D2D1_COLOR_F{ ByteNorm(c0), ByteNorm(c1), ByteNorm(c2), ByteNorm(c3) };
        }
    }
    // 2) 逗号/空格分隔：R, G, B[, A]（0-255）
    std::vector<int> comps;
    size_t pos = 0;
    while (pos < hex.size()) {
        size_t end = hex.size();
        for (size_t i = pos; i < hex.size(); ++i) {
            if (hex[i] == L',' || hex[i] == L' ' || hex[i] == L'\t') { end = i; break; }
        }
        std::wstring piece = (pos < end) ? hex.substr(pos, end - pos) : L"";
        if (!piece.empty()) {
            try { comps.push_back(std::stoi(piece)); } catch (...) { return defValue; }
        }
        pos = end;
        while (pos < hex.size() && (hex[pos] == L',' || hex[pos] == L' ' || hex[pos] == L'\t')) ++pos;
    }
    if (comps.size() == 3 || comps.size() == 4) {
        const float r = ByteNorm(static_cast<uint8_t>(std::clamp(comps[0], 0, 255)));
        const float g = ByteNorm(static_cast<uint8_t>(std::clamp(comps[1], 0, 255)));
        const float b = ByteNorm(static_cast<uint8_t>(std::clamp(comps[2], 0, 255)));
        const float a = (comps.size() == 4)
            ? ByteNorm(static_cast<uint8_t>(std::clamp(comps[3], 0, 255))) : 1.0f;
        return D2D1_COLOR_F{ r, g, b, a };
    }
    // 兜底命名颜色（Rainmeter 有专门表，M1 只取常用 16 种，未知用默认）
    auto lowered = s;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (lowered == L"black")   return D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 1.0f };
    if (lowered == L"white")   return D2D1_COLOR_F{ 1.0f, 1.0f, 1.0f, 1.0f };
    if (lowered == L"red")     return D2D1_COLOR_F{ 1.0f, 0.0f, 0.0f, 1.0f };
    if (lowered == L"green")   return D2D1_COLOR_F{ 0.0f, 0.5f, 0.0f, 1.0f };
    if (lowered == L"blue")    return D2D1_COLOR_F{ 0.0f, 0.0f, 1.0f, 1.0f };
    if (lowered == L"yellow")  return D2D1_COLOR_F{ 1.0f, 1.0f, 0.0f, 1.0f };
    if (lowered == L"cyan")    return D2D1_COLOR_F{ 0.0f, 1.0f, 1.0f, 1.0f };
    if (lowered == L"magenta") return D2D1_COLOR_F{ 1.0f, 0.0f, 1.0f, 1.0f };
    if (lowered == L"gray" || lowered == L"grey")
        return D2D1_COLOR_F{ 0.5f, 0.5f, 0.5f, 1.0f };
    return defValue;
}

namespace {

template<typename Float>
std::vector<Float> ReadCommaSeparatedFloats(const std::wstring& s)
{
    std::vector<Float> out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.size();
        for (size_t i = pos; i < s.size(); ++i) {
            if (s[i] == L',') { end = i; break; }
        }
        std::wstring piece = (pos < end) ? s.substr(pos, end - pos) : L"";
        // 去两端空白
        size_t a = piece.find_first_not_of(L" \t");
        if (a == std::wstring::npos) {
            pos = (end < s.size()) ? (end + 1) : s.size();
            continue;
        }
        size_t b = piece.find_last_not_of(L" \t");
        piece = piece.substr(a, b - a + 1);
        if (piece.empty()) continue;
        try { out.push_back(static_cast<Float>(std::stod(piece))); } catch (...) {}
        pos = (end < s.size()) ? (end + 1) : s.size();
    }
    return out;
}

}  // namespace

D2D1_RECT_F ConfigParser::ReadRect(const std::wstring& section,
                                   const std::wstring& key,
                                   const D2D1_RECT_F& defValue) const
{
    const std::vector<float> f = ReadFloats(section, key);
    if (f.size() != 4) return defValue;
    return D2D1_RECT_F{ f[0], f[1], f[2], f[3] };
}

RECT ConfigParser::ReadRECT(const std::wstring& section,
                            const std::wstring& key,
                            const RECT& defValue) const
{
    const auto f = ReadCommaSeparatedFloats<float>(ReadString(section, key, L""));
    if (f.size() != 4) return defValue;
    return RECT{ static_cast<long>(f[0]), static_cast<long>(f[1]),
                 static_cast<long>(f[2]), static_cast<long>(f[3]) };
}

std::vector<float> ConfigParser::ReadFloats(const std::wstring& section, const std::wstring& key) const
{
    return ReadCommaSeparatedFloats<float>(ReadString(section, key, L""));
}

// -----------------------------------------------------------------------
// 段/键枚举（按 Ini 顺序）
// -----------------------------------------------------------------------

std::vector<std::wstring> ConfigParser::GetKeys(const std::wstring& section) const
{
    std::vector<std::wstring> out;
    auto it = m_Sections.find(section);
    if (it == m_Sections.end()) return out;
    out.reserve(it->second.size());
    for (const auto& kv : it->second) out.push_back(kv.first);
    return out;
}

std::vector<std::wstring> ConfigParser::GetSections() const
{
    return m_SectionOrder;  // 出现顺序，而非 map 字典序
}

bool ConfigParser::IsSectionExists(const std::wstring& section) const
{
    return m_Sections.find(section) != m_Sections.end();
}

// ===========================================================================
// Batch-2 upstream adapter: 5-param ReadString + sectionVariables option
// ===========================================================================
void ConfigParser::ReadString(std::wstring& out,
                              std::wstring_view section,
                              const WCHAR*       key,
                              std::wstring       defValue,
                              ReadStringOptions  opts)
{
    const std::wstring sec(section);
    const std::wstring k(key ? key : L"");
    auto sIt = m_Sections.find(sec);
    bool keyPresent = false;
    if (sIt != m_Sections.end()) {
        auto kIt = sIt->second.find(k);
        if (kIt != sIt->second.end()) {
            // Found: copy raw, apply standard variable expansion.
            out = kIt->second;
            ReplaceVariables(out);
            keyPresent = true;
        }
    }
    if (!keyPresent) {
        out = std::move(defValue);
        ReplaceVariables(out);
    }
    // opts.sectionVariables: Rainmeter 原生允许 [MeasureName] 在字符串内。
    //   骨架阶段不做段变量求值（需要 Skin 上下文），但保留 flag 在
    //   struct 中便于后续接入时启用。
    (void)opts;
    m_LastDefaultUsed = !keyPresent;
}

// ===========================================================================
// Batch-2: Mouse::ReplaceMouseVariables skeleton shims.
// Real $MOUSEX$ / [$MOUSEX] variable resolution needs WindowProc hook, cursor
// hit-test cache and a Meter → Skin → CRainmeter chain; the scope here is
// purely "let Batch-2 link and run".  Stubs are defined as out-of-line to
// avoid touching the inline-convenience region (which stays header-only to
// match existing M4 lookup ABI).
// ===========================================================================
void ConfigParser::ExpandSectionVariables(std::wstring& /*result*/,
                                          VariableExpandMode /*mode*/,
                                          Meter* /*ctxMeter*/)
{
    // Intentionally a no-op.  Upstream's Section-variable expansion engine
    // lands in Batch-3 along with the ConfigParser::ReplaceVariables upgrade
    // that handles [SectionKey] syntax.
}

std::wstring ConfigParser::GetDollarMouseVariable(const std::wstring& /*name*/,
                                                  Meter* /*ctxMeter*/) const
{
    // Intentionally empty: caller (Mouse::ReplaceMouseVariables) skips the
    // $..$ substitution when the returned wstring is empty().
    return {};
}

}  // namespace raindock
