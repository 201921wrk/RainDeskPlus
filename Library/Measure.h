/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Measure.h（基类，直接复用）。
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Section.h"
#include "IfActions.h"
#include "Util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // RECT / BOOL / HANDLE 等（Win SDK 体系结构定义）

namespace raindock {

class ConfigParser;
class Skin;  // 上游 Measure 由 Skin 拥有并传入构造。

// 数据源基类。所有 MeasureXXX 继承此类。
//
// 生命周期（对齐 Rainmeter）：
//   构造(skin,name) → Initialize(parser,iniPath) 调用 ReadOptions(parser,name)
//                   → 循环：Update() [若 rereadOptions 则再 ReadOptions 一次]
//                         Update() 内部按 UpdateDivider 跳帧 → 调 UpdateValue()
//                        → Meter 读取 GetValue/GetString
//                   → 析构
// 若子类只需最基本行为，不再 override Initialize/Update，而是：
//   - override ReadOptions(parser, section) 读取自定义 INI 键；
//   - override UpdateValue() 做采样并写入 m_Value / m_StringValue。
class Measure : public ::Section
{
public:
    virtual ~Measure();

    Measure(const Measure&) = delete;
    Measure& operator=(const Measure&) = delete;

    // 构造：提供 name；Skin* 允许为空（M1 占位）。
    Measure(Skin* skin, const WCHAR* name);

    // 初始化并从 INI 读取配置（读 [m_Name] 段）。
    // 骨架兼容：iniPath 仍保留，实际上游使用 parser 直接读段。
    virtual void Initialize(ConfigParser& parser, const std::wstring& iniPath);

    // 周期采集。rereadOptions=true 时先重新解析一次（对应 Rainmeter 的 !UpdateMeasure 带选项）。
    virtual void Update(bool rereadOptions = false);

    // 当前数值（供 Meter 使用）。按 InvertMeasure / MinValue / MaxValue 校正。
    virtual double GetValue();

    // 当前字符串值（用于 [MeasureName] 段变量 & MeterString）。会应用 Substitute。
    virtual const wchar_t* GetString();

    // ===== Batch-2 upstream compatibility =====
    // Upstream IfActions::DoIfActions() calls `measure.GetStringValue()`.
    // Alias kept because local skeleton provides GetString() / upstream
    // provides GetStringValue() — point both at same (non-const) string out.
    const wchar_t* GetStringValue() { return GetString(); }

    // 释放资源。默认 no-op（子类 override）。
    virtual void Finalize();

    // 更新间隔（骨架：另存字段，与 Section 的 UpdateDivider 无关）。
    void     SetUpdateInterval(uint32_t ms) { m_UpdateInterval = ms; }
    uint32_t GetUpdateInterval() const      { return m_UpdateInterval; }

    // 启用/停用
    void Disable();
    void Enable();
    bool IsDisabled() const { return m_Disabled; }

    // 数值范围（Rainmeter 的 MinValue/MaxValue，供相对计算/图形归一化）
    void   SetMinValue(double v) { m_MinValue = v; }
    double GetMinValue() const   { return m_MinValue; }
    void   SetMaxValue(double v) { m_MaxValue = v; }
    double GetMaxValue() const   { return m_MaxValue; }
    void   SetInvert(bool v)     { m_Invert = v; }
    bool   GetInvert() const     { return m_Invert; }

    // Substitute：按顺序的「查找-替换」对（骨架只做字面量替换；M2 升级到 Regex 分支）
    void AddSubstitute(std::wstring find, std::wstring replace) {
        m_Substitute.emplace_back(std::move(find), std::move(replace));
    }
    void ClearSubstitute() { m_Substitute.clear(); }

    // ===== Section base-type id（上游 GetBaseTypeID）=====
    UINT GetBaseTypeID() override { return TypeID<Measure>(); }

protected:
    // 读取 [section] 下的公共选项 + 子类自定义。子类 override 应先调基类。
    virtual void ReadOptions(ConfigParser& parser, std::wstring_view section);
    // 实际采样入口（由 Update() 跳帧后调用）。子类 MUST override。
    virtual void UpdateValue() = 0;
    // 对字符串做 Substitute 替换。
    const wchar_t* CheckSubstitute(const wchar_t* src);

    // 供子类写采样结果
    double        m_Value = 0.0;
    std::wstring  m_StringValue;

    // 子类需频繁读写的范围/状态字段（对齐 Rainmeter Measure 原生 protected 暴露风格）
    bool          m_Disabled  = false;
    bool          m_Invert    = false;
    double        m_MinValue  = 0.0;
    // 默认 0 = 「未设置范围」：GetValue 不 clamp（Net/Memory 等无界 Measure 依赖此语义）。
    // 子类或用户 INI 显式建立范围（如 CPU 0-100）后才参与 clamp/Invert 归一化。
    double        m_MaxValue  = 0.0;

    bool          m_Initialized = false;  // Initialize() 幂等保护

    // IfCondition / IfAbove / IfBelow / IfEqual / IfMatch（上游 IfActions）。
    IfActions     m_IfActions;

private:
    uint32_t      m_UpdateInterval = 1000;

    std::vector<std::pair<std::wstring, std::wstring>> m_Substitute;  // <find, replace>
    std::wstring  m_Substituted;  // 暂存 CheckSubstitute 返回结果（返回的指针指向这里）
};

}  // namespace raindock
