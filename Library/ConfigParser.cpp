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

#include "Logger.h"
#include "PathUtil.h"

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

std::string ToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

// 单通道 ARGB -> [0,1] float（Rainmeter 的 ARGBHEX / RRGGBB / AARRGGBB 惯例）
float ByteNorm(uint8_t b) { return static_cast<float>(b) / 255.0f; }

// 读取整份文件为字节串（UTF-8/ANSI 混合内容由 FromUtf8 逐行还原）。
bool ReadFileUtf8(const std::wstring& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

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
    std::string content;
    if (!ReadFileUtf8(path, content)) return false;
    LoadFromString(content);
    // 主题合并：主文件声明 [Theme] Name= 时，把同目录 Themes\<名>.ini 并入为低优先级默认值。
    ApplyThemeDefaults(path);
    return true;
}

// 主题合并（D26-30）：
//   主文件解析完成后读取 [Theme] Name；若声明了主题，则加载同目录 Themes\<名>.ini，
//   把其中的「段/键」按低优先级默认值并入：主文件已显式写出的键保持不变（主文件优先），
//   主题文件只补主文件缺失的缺口。主题文件缺失时只告警降级，不影响主文件加载。
void ConfigParser::ApplyThemeDefaults(const std::wstring& path)
{
    const std::wstring themeName = Trim(ReadString(L"Theme", L"Name", L""));
    if (themeName.empty()) return;  // 未启用主题系统，静默返回。

    const std::wstring themePath =
        PathUtil::GetFolderFromFilePath(path) + L"Themes\\" + themeName + L".ini";

    std::string themeContent;
    if (!ReadFileUtf8(themePath, themeContent))
    {
        // 降级：主题文件缺失不阻断加载，主文件自身的兜底默认值继续生效。
        LogWarningF(L"主题文件缺失，已按主文件默认值降级加载：%s", themePath.c_str());
        return;
    }

    // 主题文件只做内存解析（LoadFromString），不走 LoadFile，天然规避「主题引用主题」的递归。
    ConfigParser theme;
    theme.LoadFromString(themeContent);

    // 同类实例可直接访问私有成员，从而取到未经变量展开的原始值（与主文件解析口径一致）。
    for (const std::wstring& section : theme.m_SectionOrder)
    {
        auto& destSection = m_Sections[section];
        if (std::find(m_SectionOrder.begin(), m_SectionOrder.end(), section) == m_SectionOrder.end())
        {
            m_SectionOrder.push_back(section);
        }

        for (const auto& [key, value] : theme.m_Sections[section])
        {
            // 低优先级：emplace 仅在键缺失时插入，主文件已写出的键保持原值。
            if (!destSection.emplace(key, value).second) continue;
            if (section == L"Variables" && m_Variables.find(key) == m_Variables.end())
            {
                m_Variables[key] = value;
            }
        }
    }
}

void ConfigParser::LoadFromString(const std::string& utf8Content)
{
    m_Sections.clear();
    m_SectionOrder.clear();
    m_KeyOrder.clear();
    m_HeaderComments.clear();
    m_Variables.clear();

    std::istringstream ss(utf8Content);
    std::string lineUtf8;
    std::wstring current;
    bool headerDone = false;   // 是否已越过首个段头（用于截取文件头注释块）

    while (std::getline(ss, lineUtf8))
    {
        std::wstring line = FromUtf8(lineUtf8);
        line = Trim(line);

        if (line.empty()) continue;
        // 注释：行首 ; 或 #
        if (line[0] == L';' || line[0] == L'#')
        {
            // 首个段头之前的连续注释块即文件头注释，记入以便「读→改→写」不丢首行注释。
            if (!headerDone) m_HeaderComments.push_back(line);
            continue;
        }

        if (line.front() == L'[' && line.back() == L']') {
            current = Trim(line.substr(1, line.size() - 2));
            if (current.empty()) continue;
            headerDone = true;
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
        RegisterKeyOrder(current, key);
        if (current == L"Variables") m_Variables[key] = val;
    }
}

// -----------------------------------------------------------------------
// 落盘与段/键删除（D31-35：统一 INI 持久化出口）
// -----------------------------------------------------------------------

// 段内键的落盘顺序单独记录：m_Sections 是 std::map（字典序），
// 直接按它输出会把手写/生成文件的键序打乱，故另行登记插入顺序。
void ConfigParser::RegisterKeyOrder(const std::wstring& section, const std::wstring& key)
{
    std::vector<std::wstring>& order = m_KeyOrder[section];
    if (std::find(order.begin(), order.end(), key) == order.end())
    {
        order.push_back(key);
    }
}

// 序列化口径（与既有生成的 Dock.ini / items.ini 字节格式保持一致）：
//   - UTF-8 无 BOM、行尾 CRLF；
//   - 首行注释块各自成行，紧随其后即为首个段头（不额外空行）；
//   - 段按出现顺序，段之间空一行；段内键按插入顺序。
bool ConfigParser::SaveFile(const std::wstring& path) const
{
    std::string out;
    out.reserve(512 + m_Sections.size() * 128);

    for (const std::wstring& line : m_HeaderComments)
    {
        out += ToUtf8(line);
        out += "\r\n";
    }

    bool firstSection = true;
    for (const std::wstring& section : m_SectionOrder)
    {
        auto sIt = m_Sections.find(section);
        if (sIt == m_Sections.end()) continue;
        if (!firstSection) out += "\r\n";
        firstSection = false;

        out += "[";
        out += ToUtf8(section);
        out += "]\r\n";

        // 先按登记顺序输出；未登记顺序的键（异常路径）再按字典序补齐，保证不丢内容。
        std::vector<std::wstring> written;
        auto oIt = m_KeyOrder.find(section);
        if (oIt != m_KeyOrder.end())
        {
            for (const std::wstring& key : oIt->second)
            {
                auto kIt = sIt->second.find(key);
                if (kIt == sIt->second.end()) continue;
                out += ToUtf8(key) + "=" + ToUtf8(kIt->second) + "\r\n";
                written.push_back(key);
            }
        }
        for (const auto& [key, value] : sIt->second)
        {
            if (std::find(written.begin(), written.end(), key) != written.end()) continue;
            out += ToUtf8(key) + "=" + ToUtf8(value) + "\r\n";
        }
    }

    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    return f.good();
}

// 与 LoadFile 的唯一差别：**不**做主题合并。
// 「读→改→写」场景（!WriteKeyValue）若走 LoadFile，会把主题文件内容展开进主文件，
// 覆盖 [Theme] Name= 声明本身，故必须走这条裸读路径。
bool ConfigParser::LoadFileRaw(const std::wstring& path)
{
    std::string content;
    if (!ReadFileUtf8(path, content)) return false;
    LoadFromString(content);
    return true;
}

void ConfigParser::SetHeaderComment(const std::wstring& comment)
{
    m_HeaderComments.clear();
    const std::wstring line = Trim(comment);
    if (!line.empty()) m_HeaderComments.push_back(line);
}

const std::wstring& ConfigParser::GetHeaderComment() const
{
    static const std::wstring kEmpty;
    return m_HeaderComments.empty() ? kEmpty : m_HeaderComments.front();
}

bool ConfigParser::RemoveValue(const std::wstring& section, const std::wstring& key)
{
    auto sIt = m_Sections.find(section);
    if (sIt == m_Sections.end() || sIt->second.erase(key) == 0) return false;

    auto oIt = m_KeyOrder.find(section);
    if (oIt != m_KeyOrder.end())
    {
        std::vector<std::wstring>& order = oIt->second;
        order.erase(std::remove(order.begin(), order.end(), key), order.end());
    }
    if (section == L"Variables") m_Variables.erase(key);

    // 键被删空后不留空段，否则落盘会写出无意义的 "[Section]" 头。
    if (sIt->second.empty()) RemoveSection(section);
    return true;
}

bool ConfigParser::RemoveSection(const std::wstring& section)
{
    if (m_Sections.erase(section) == 0) return false;

    m_SectionOrder.erase(std::remove(m_SectionOrder.begin(), m_SectionOrder.end(), section),
                         m_SectionOrder.end());
    m_KeyOrder.erase(section);
    if (section == L"Variables") m_Variables.clear();
    return true;
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
    RegisterKeyOrder(L"Variables", name);
}

void ConfigParser::SetValue(const std::wstring& section,
                            const std::wstring& key,
                            const std::wstring& value)
{
    if (section == L"Variables") {
        m_Variables[key] = value;
    }
    m_Sections[section][key] = value;
    RegisterKeyOrder(section, key);
    auto it = std::find(m_SectionOrder.begin(), m_SectionOrder.end(), section);
    if (it == m_SectionOrder.end()) m_SectionOrder.push_back(section);
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

const std::vector<std::wstring>& ConfigParser::GetSections() const
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
