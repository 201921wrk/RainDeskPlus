/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/ConfigParser.h（INI 解析引擎，直接复用）。
 */
#ifndef RAINDOCK_LIBRARY_CONFIGPARSER_H_
#define RAINDOCK_LIBRARY_CONFIGPARSER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <d2d1_1.h>  // 确保 D2D1_COLOR_F / D2D1_RECT_F 由 Windows SDK 统一定义（Meter 模块已间接引入）
#include <windef.h>  // RECT

namespace raindock {

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
    std::vector<std::wstring> GetSections() const;

    bool IsSectionExists(const std::wstring& section) const;

    // 变量存取：Measure 更新动态变量用。#name# 语法由 ReadString 自动替换。
    void SetVariable(const std::wstring& name, const std::wstring& value);
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
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_CONFIGPARSER_H_
