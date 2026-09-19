/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * JSON 解析实现（契约见 JsonParser.h）。递归下降，单遍扫描，无回溯。
 */
#include "JsonParser.h"

#include <cwchar>
#include <cstdlib>

namespace raindock {

namespace {

// 递归深度上限：正常报文（OWM 响应最深 3 层）远达不到；设限只为让畸形输入
// 走「解析失败」而不是把线程栈递归耗尽。
constexpr int kMaxDepth = 64;

bool IsWhitespace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}

bool IsDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

// 码点 → UTF-16（BMP 外拆成代理对）。
void AppendCodePoint(std::wstring& out, unsigned cp)
{
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
        return;
    }
    cp -= 0x10000;
    out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
    out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
}

}  // namespace

namespace json_detail {

class Reader
{
public:
    explicit Reader(std::wstring_view text) : m_Text(text) {}

    bool ParseDocument(JsonValue& out)
    {
        SkipBom();
        SkipWhitespace();

        JsonValue root;
        if (!ParseValue(root, 0)) return false;

        SkipWhitespace();
        if (m_Pos != m_Text.size()) return false;   // 根值之后还有残余内容
        out = std::move(root);
        return true;
    }

private:
    bool ParseValue(JsonValue& out, int depth)
    {
        if (depth > kMaxDepth) return false;
        if (m_Pos >= m_Text.size()) return false;

        const wchar_t c = m_Text[m_Pos];
        if (c == L'{') return ParseObject(out, depth);
        if (c == L'[') return ParseArray(out, depth);
        if (c == L'"') return ParseStringValue(out);
        if (c == L'-' || IsDigit(c)) return ParseNumber(out);

        if (MatchLiteral(L"true")) {
            out.m_Type = JsonValue::Type::Bool;
            out.m_Bool = true;
            return true;
        }
        if (MatchLiteral(L"false")) {
            out.m_Type = JsonValue::Type::Bool;
            out.m_Bool = false;
            return true;
        }
        if (MatchLiteral(L"null")) {
            out.m_Type = JsonValue::Type::Null;
            return true;
        }
        return false;
    }

    bool ParseObject(JsonValue& out, int depth)
    {
        ++m_Pos;   // 吃掉 '{'
        out.m_Type = JsonValue::Type::Object;
        out.m_Members.clear();

        SkipWhitespace();
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == L'}') {
            ++m_Pos;
            return true;
        }

        for (;;) {
            SkipWhitespace();

            std::wstring key;
            if (!ParseStringRaw(key)) return false;   // 键必须是字符串

            SkipWhitespace();
            if (m_Pos >= m_Text.size() || m_Text[m_Pos] != L':') return false;
            ++m_Pos;

            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, depth + 1)) return false;
            out.m_Members.emplace_back(std::move(key), std::move(value));

            SkipWhitespace();
            if (m_Pos >= m_Text.size()) return false;
            if (m_Text[m_Pos] == L',') {   // 严格模式：紧跟 '}' 的空悬逗号在下一轮失败
                ++m_Pos;
                continue;
            }
            if (m_Text[m_Pos] == L'}') {
                ++m_Pos;
                return true;
            }
            return false;
        }
    }

    bool ParseArray(JsonValue& out, int depth)
    {
        ++m_Pos;   // 吃掉 '['
        out.m_Type = JsonValue::Type::Array;
        out.m_Items.clear();

        SkipWhitespace();
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == L']') {
            ++m_Pos;
            return true;
        }

        for (;;) {
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, depth + 1)) return false;
            out.m_Items.push_back(std::move(value));

            SkipWhitespace();
            if (m_Pos >= m_Text.size()) return false;
            if (m_Text[m_Pos] == L',') {
                ++m_Pos;
                continue;
            }
            if (m_Text[m_Pos] == L']') {
                ++m_Pos;
                return true;
            }
            return false;
        }
    }

    bool ParseStringValue(JsonValue& out)
    {
        std::wstring s;
        if (!ParseStringRaw(s)) return false;
        out.m_Type = JsonValue::Type::String;
        out.m_String = std::move(s);
        return true;
    }

    // 从当前 '"' 起解析字符串字面量（含转义），成功时 m_Pos 停在收尾引号之后。
    bool ParseStringRaw(std::wstring& out)
    {
        if (m_Pos >= m_Text.size() || m_Text[m_Pos] != L'"') return false;
        ++m_Pos;

        out.clear();
        while (m_Pos < m_Text.size()) {
            const wchar_t c = m_Text[m_Pos++];
            if (c == L'"') return true;

            if (c != L'\\') {
                if (c < 0x20) return false;   // RFC 8259：控制字符必须写成转义
                out.push_back(c);
                continue;
            }

            if (m_Pos >= m_Text.size()) return false;
            const wchar_t esc = m_Text[m_Pos++];
            switch (esc) {
            case L'"':  out.push_back(L'"');  break;
            case L'\\': out.push_back(L'\\'); break;
            case L'/':  out.push_back(L'/');  break;
            case L'b':  out.push_back(L'\b'); break;
            case L'f':  out.push_back(L'\f'); break;
            case L'n':  out.push_back(L'\n'); break;
            case L'r':  out.push_back(L'\r'); break;
            case L't':  out.push_back(L'\t'); break;
            case L'u': {
                unsigned cp = 0;
                if (!ReadHex4(cp)) return false;

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // 高位代理：必须紧跟 "\uXXXX" 低位代理，才能还原成真实码点
                    if (m_Pos + 1 >= m_Text.size() ||
                        m_Text[m_Pos] != L'\\' || m_Text[m_Pos + 1] != L'u') {
                        return false;
                    }
                    m_Pos += 2;
                    unsigned lo = 0;
                    if (!ReadHex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;   // 孤立低位代理
                }
                AppendCodePoint(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;   // 引号未闭合
    }

    bool ReadHex4(unsigned& out)
    {
        if (m_Pos + 4 > m_Text.size()) return false;

        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const wchar_t c = m_Text[m_Pos++];
            v <<= 4;
            if (c >= L'0' && c <= L'9')      v |= static_cast<unsigned>(c - L'0');
            else if (c >= L'a' && c <= L'f') v |= static_cast<unsigned>(c - L'a' + 10);
            else if (c >= L'A' && c <= L'F') v |= static_cast<unsigned>(c - L'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    // 数字文法：-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
    bool ParseNumber(JsonValue& out)
    {
        const size_t begin = m_Pos;

        if (m_Text[m_Pos] == L'-') ++m_Pos;

        if (m_Pos >= m_Text.size()) return false;
        if (m_Text[m_Pos] == L'0') {
            ++m_Pos;   // 前导零非法：0 之后不得再接数字
        } else if (m_Text[m_Pos] >= L'1' && m_Text[m_Pos] <= L'9') {
            while (m_Pos < m_Text.size() && IsDigit(m_Text[m_Pos])) ++m_Pos;
        } else {
            return false;
        }

        if (m_Pos < m_Text.size() && m_Text[m_Pos] == L'.') {
            ++m_Pos;
            if (m_Pos >= m_Text.size() || !IsDigit(m_Text[m_Pos])) return false;
            while (m_Pos < m_Text.size() && IsDigit(m_Text[m_Pos])) ++m_Pos;
        }

        if (m_Pos < m_Text.size() && (m_Text[m_Pos] == L'e' || m_Text[m_Pos] == L'E')) {
            ++m_Pos;
            if (m_Pos < m_Text.size() && (m_Text[m_Pos] == L'+' || m_Text[m_Pos] == L'-')) ++m_Pos;
            if (m_Pos >= m_Text.size() || !IsDigit(m_Text[m_Pos])) return false;
            while (m_Pos < m_Text.size() && IsDigit(m_Text[m_Pos])) ++m_Pos;
        }

        const std::wstring token(m_Text.substr(begin, m_Pos - begin));
        wchar_t* end = nullptr;
        const double value = std::wcstod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) return false;

        out.m_Type = JsonValue::Type::Number;
        out.m_Number = value;
        return true;
    }

    bool MatchLiteral(const wchar_t* literal)
    {
        const size_t n = std::wcslen(literal);
        if (m_Text.compare(m_Pos, n, literal, n) != 0) return false;
        m_Pos += n;
        return true;
    }

    void SkipBom()
    {
        if (!m_Text.empty() && m_Text[0] == 0xFEFF) m_Pos = 1;
    }

    void SkipWhitespace()
    {
        while (m_Pos < m_Text.size() && IsWhitespace(m_Text[m_Pos])) ++m_Pos;
    }

    std::wstring_view m_Text;
    size_t            m_Pos = 0;
};

}  // namespace json_detail

const JsonValue& JsonValue::Null()
{
    static const JsonValue kNull;
    return kNull;
}

double JsonValue::AsNumber(double def) const
{
    return (m_Type == Type::Number) ? m_Number : def;
}

bool JsonValue::AsBool(bool def) const
{
    return (m_Type == Type::Bool) ? m_Bool : def;
}

const std::wstring& JsonValue::AsString() const
{
    static const std::wstring kEmpty;
    return (m_Type == Type::String) ? m_String : kEmpty;
}

size_t JsonValue::Size() const
{
    if (m_Type == Type::Array)  return m_Items.size();
    if (m_Type == Type::Object) return m_Members.size();
    return 0;
}

const JsonValue* JsonValue::Find(std::wstring_view key) const
{
    if (m_Type != Type::Object) return nullptr;
    for (const auto& member : m_Members) {
        if (std::wstring_view(member.first) == key) return &member.second;
    }
    return nullptr;
}

const JsonValue& JsonValue::At(size_t index) const
{
    if (m_Type != Type::Array || index >= m_Items.size()) return Null();
    return m_Items[index];
}

bool JsonParser::Parse(std::wstring_view text, JsonValue& out)
{
    out = JsonValue();   // 失败时保证调用方拿到 Null，而非上一次的残留

    json_detail::Reader reader(text);
    JsonValue root;
    if (!reader.ParseDocument(root)) return false;

    out = std::move(root);
    return true;
}

}  // namespace raindock
