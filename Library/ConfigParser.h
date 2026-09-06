/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/ConfigParser.h（INI 解析引擎，直接复用）。
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <d2d1_1.h>  // 确保 D2D1_COLOR_F / D2D1_RECT_F 由 Windows SDK 统一定义（Meter 模块已间接引入）
#include <windef.h>  // RECT

namespace raindock {

// Batch-2 adapter: upstream (Section / IfActions / Mouse) uses a 5-parameter
// ReadString with an options struct plus `GetLastDefaultUsed()` flag.
struct ReadStringOptions
{
    bool sectionVariables = false;   // 骨架阶段暂不处理段变量，保留字段。
};

// Mouse::ReplaceMouseVariables drives 2 pass ([$X] then $X$).  Only the
// mouse-variable modes are needed in Batch-2; other modes are deferred.
enum class VariableExpandMode
{
    Normal = 0,           // Full [#var]/$var$/[section:key] (not used yet).
    DollarMouseOnly = 1,  // Only expand [$X] style section-scoped mouse vars.
};

class Meter;  // forward — Meter* passed to ExpandSectionVariables / GetDollarMouseVariable.

// INI 解析器。对应 Rainmeter ConfigParser。
// 骨架阶段：提供最小接口；提取 Rainmeter 实现后接入完整功能
// （变量展开、公式求值、动态变量、Include 等）。
class ConfigParser
{
public:
    ConfigParser() = default;

    // 读取 INI 文件并按段加载。返回是否成功。
    bool LoadFile(const std::wstring& path);
    // 从字符串加载（测试用）。
    void LoadFromString(const std::string& utf8Content);

    // 读取键值。section 如 L"Variables"，key 如 L"Color"。
    // 返回值：拷贝副本（调用可在任何地方安全保留）。
    std::wstring ReadString(const std::wstring& section,
                            const std::wstring& key,
                            const std::wstring& defValue = L"") const;
    int       ReadInt(const std::wstring& section, const std::wstring& key, int defValue = 0) const;
    uint32_t  ReadUInt(const std::wstring& section, const std::wstring& key, uint32_t defValue = 0) const;
    uint64_t  ReadUInt64(const std::wstring& section, const std::wstring& key, uint64_t defValue = 0) const;
    double    ReadFloat(const std::wstring& section, const std::wstring& key, double defValue = 0.0) const;
    bool      ReadBool(const std::wstring& section, const std::wstring& key, bool defValue = false) const;
    D2D1_COLOR_F ReadColor(const std::wstring& section,
                           const std::wstring& key,
                           const D2D1_COLOR_F& defValue = { 0.0f, 0.0f, 0.0f, 1.0f }) const;
    D2D1_RECT_F ReadRect(const std::wstring& section,
                         const std::wstring& key,
                         const D2D1_RECT_F& defValue = {}) const;
    RECT ReadRECT(const std::wstring& section,
                  const std::wstring& key,
                  const RECT& defValue = {}) const;
    std::vector<float> ReadFloats(const std::wstring& section, const std::wstring& key) const;

    // 列出某段下所有键名。
    std::vector<std::wstring> GetKeys(const std::wstring& section) const;
    // 列出所有段名（按 INI 出现顺序，如 [Rainmeter] [Variables] [MeasureCPU] ...）。
    const std::vector<std::wstring>& GetSections() const;

    bool IsSectionExists(const std::wstring& section) const;

    // ===== Batch-2 upstream compatibility =====
    // Upstream-style 5-parameter reader used by Section/IfActions/Mouse.
    // Writes the result into |out| (clears on default path so caller can
    // distinguish empty-string values from missing keys).
    void ReadString(std::wstring& out,
                    std::wstring_view section,
                    const WCHAR*       key,
                    std::wstring       defValue,
                    ReadStringOptions  opts = {});
    // Returns true iff the last 5-param ReadString call hit the fallback
    // (i.e. section/key was absent or empty in the INI store).
    bool GetLastDefaultUsed() const { return m_LastDefaultUsed; }

    // ---------- Batch-2: (wstring_view section, const WCHAR* key) 3-param
    // convenience overloads.  Upstream TUs universally accept section as a
    // wstring_view and key as a const WCHAR[] / L"..." literal; thin
    // temporaries forward to the existing (const wstring&, const wstring&, T)
    // signatures without modifying any of the original M4 lookup paths.
    std::wstring ReadString(std::wstring_view section,
                            const WCHAR*       key,
                            const std::wstring& defValue = L"") const
    {
        return ReadString(std::wstring(section),
                          key ? std::wstring(key) : std::wstring(),
                          defValue);
    }
    // 4-param by-value variant (used by Mouse.cpp L73): same signature as the
    // 5-param void-out form but WITHOUT the leading |wstring& out|.  Falls
    // back to the out-param form for all real logic.
    std::wstring ReadString(std::wstring_view section,
                            const WCHAR*       key,
                            std::wstring       defValue,
                            ReadStringOptions  opts)
    {
        std::wstring out;
        ReadString(out, section, key, std::move(defValue), opts);
        return out;
    }
    int ReadInt(std::wstring_view section, const WCHAR* key, int defValue = 0) const
    {
        return ReadInt(std::wstring(section),
                       key ? std::wstring(key) : std::wstring(),
                       defValue);
    }
    double ReadFloat(std::wstring_view section, const WCHAR* key, double defValue = 0.0) const
    {
        return ReadFloat(std::wstring(section),
                         key ? std::wstring(key) : std::wstring(),
                         defValue);
    }
    bool ReadBool(std::wstring_view section, const WCHAR* key, bool defValue = false) const
    {
        return ReadBool(std::wstring(section),
                        key ? std::wstring(key) : std::wstring(),
                        defValue);
    }

    // ---------- Batch-2: (const wstring& section, const WCHAR* key)
    // disambiguation overloads.  M4 call sites pass section as a const wstring&
    // lvalue and key as L"..." wide-string literal (→ const WCHAR*).  Without
    // these overloads the call is C2666: the canonical base (const wstring&,
    // const wstring&, T) wins arg1 but the wstring_view family wins arg2
    // array-decay.  These forms are exact on BOTH args.
    //
    // Forwarding note: std::wstring has NO implicit conversion to const WCHAR*,
    // so `ReadXxx(section, std::wstring(key), def)` below can only bind to the
    // base (const wstring&, const wstring&, T) — it cannot recurse into these
    // WCHAR* overloads nor into the wstring_view family.
    std::wstring ReadString(const std::wstring& section,
                            const WCHAR*       key,
                            const std::wstring& defValue = L"") const
    {
        return ReadString(section,
                          key ? std::wstring(key) : std::wstring(),
                          defValue);
    }
    int ReadInt(const std::wstring& section, const WCHAR* key, int defValue = 0) const
    {
        return ReadInt(section,
                       key ? std::wstring(key) : std::wstring(),
                       defValue);
    }
    double ReadFloat(const std::wstring& section, const WCHAR* key, double defValue = 0.0) const
    {
        return ReadFloat(section,
                         key ? std::wstring(key) : std::wstring(),
                         defValue);
    }
    bool ReadBool(const std::wstring& section, const WCHAR* key, bool defValue = false) const
    {
        return ReadBool(section,
                        key ? std::wstring(key) : std::wstring(),
                        defValue);
    }

    // ---------- Batch-2: (const WCHAR* section, const WCHAR* key) literal
    // overloads.  A few call sites pass BOTH section and key as L"..." literals
    // (Skin.cpp Load() `ReadInt(L"Rainmeter", L"Update", …)`, Mouse.cpp L99
    // `ReadString(L"Rainmeter", L"MouseActionCursorName", L"")`).  With only the
    // families above this is C2668: arg1 literal→wstring_view and
    // literal→wstring are both user-defined conversions of equal rank.  These
    // forms make both args array→pointer decays (exact), so they win cleanly.
    std::wstring ReadString(const WCHAR* section,
                            const WCHAR* key,
                            const std::wstring& defValue = L"") const
    {
        return ReadString(section ? std::wstring(section) : std::wstring(),
                          key ? std::wstring(key) : std::wstring(),
                          defValue);
    }
    int ReadInt(const WCHAR* section, const WCHAR* key, int defValue = 0) const
    {
        return ReadInt(section ? std::wstring(section) : std::wstring(),
                       key ? std::wstring(key) : std::wstring(),
                       defValue);
    }
    double ReadFloat(const WCHAR* section, const WCHAR* key, double defValue = 0.0) const
    {
        return ReadFloat(section ? std::wstring(section) : std::wstring(),
                         key ? std::wstring(key) : std::wstring(),
                         defValue);
    }
    bool ReadBool(const WCHAR* section, const WCHAR* key, bool defValue = false) const
    {
        return ReadBool(section ? std::wstring(section) : std::wstring(),
                        key ? std::wstring(key) : std::wstring(),
                        defValue);
    }

    // Mouse::ReplaceMouseVariables integration.  Skeleton implementations are
    // intentionally no-op / empty-string in Batch-2; full [$MOUSEX] / $MOUSEX$
    // substitution logic joins Batch-3+.
    void ExpandSectionVariables(std::wstring& result,
                                VariableExpandMode mode,
                                Meter* ctxMeter);
    std::wstring GetDollarMouseVariable(const std::wstring& name,
                                        Meter* ctxMeter) const;

    // 变量存取：Measure 更新动态变量用。#name# 语法由 ReadString 自动替换。
    void SetVariable(const std::wstring& name, const std::wstring& value);
    // 任意段键值写入（!SetOption 用）。段不存在则新建；Variables 段同步到变量表。
    void SetValue(const std::wstring& section, const std::wstring& key, const std::wstring& value);
    bool GetVariable(const std::wstring& name, std::wstring& value) const;
    const std::map<std::wstring, std::wstring>& GetVariables() const { return m_Variables; }

    // 变量替换（也暴露给 Section 派生类用）：
    //   #var#        → m_Variables[var]
    //   $$           → 单个 $
    //   [m] [s]      → 段变量（占位，骨架阶段不处理，后续升级）
    bool ReplaceVariables(std::wstring& value) const;

private:
    // section -> (key -> value)
    std::map<std::wstring, std::map<std::wstring, std::wstring>> m_Sections;
    // 按 Ini 出现顺序的段名列表（Rainmeter 默认遍历顺序）
    std::vector<std::wstring> m_SectionOrder;
    // [Variables] 段 + SetVariable 合并视图，供变量展开。
    std::map<std::wstring, std::wstring> m_Variables;

    // Batch-2 state
    mutable bool m_LastDefaultUsed = false;
};

}  // namespace raindock
