/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 轻量 JSON 解析（只读）。Common/ 与 Library/ 均未 vendor JSON 库，而天气
 * Measure 需要读 OpenWeatherMap 的嵌套响应（对象 + weather 数组），故按
 * RFC 8259 自实现最小子集：对象 / 数组 / 字符串 / 数字 / 布尔 / null。
 *
 * 有意不做：JSON5 扩展、注释、尾随逗号、非标准数字（前导零/前导 +）。
 * 数字一律以 double 承载（时间戳、温度等量级远在 2^53 之内，无精度损失）。
 * 输入是 UTF-16 宽串（FileUtil::ReadTextFile 的输出口径），字符串转义
 * \uXXXX 支持代理对合并，输出仍是 UTF-16。
 */
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace raindock {

namespace json_detail {
class Reader;   // 解析实现（JsonParser.cpp）；需白盒写入 JsonValue 的私有字段
}  // namespace json_detail

// 只读 JSON 值。类型不匹配时取默认值而不抛异常，便于对不完整报文容错：
//   const JsonValue* main = root.Find(L"main");
//   double temp = main ? main->Find(L"temp")->AsNumber() : 0.0;
class JsonValue
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;

    Type GetType() const { return m_Type; }
    bool IsNull()   const { return m_Type == Type::Null; }
    bool IsBool()   const { return m_Type == Type::Bool; }
    bool IsNumber() const { return m_Type == Type::Number; }
    bool IsString() const { return m_Type == Type::String; }
    bool IsArray()  const { return m_Type == Type::Array; }
    bool IsObject() const { return m_Type == Type::Object; }

    // 以下取值器在类型不匹配时返回 def / 空串，不抛异常。
    double              AsNumber(double def = 0.0) const;
    bool                AsBool(bool def = false) const;
    const std::wstring& AsString() const;

    // Array 为元素个数，Object 为成员个数，其余类型为 0。
    size_t Size() const;

    // Object 按键取成员（重复键取首个）；非 Object 或键缺失返回 nullptr。
    const JsonValue* Find(std::wstring_view key) const;
    // Array 按下标取元素；越界返回 Null 单例（可安全链式 Find/At）。
    const JsonValue& At(size_t index) const;

    // 全局 Null 单例（At 越界 / 默认返回值）。
    static const JsonValue& Null();

private:
    friend class json_detail::Reader;

    Type         m_Type   = Type::Null;
    double       m_Number = 0.0;
    bool         m_Bool   = false;
    std::wstring m_String;
    std::vector<JsonValue>                          m_Items;    // Array 元素
    std::vector<std::pair<std::wstring, JsonValue>> m_Members;  // Object 成员（保持原序）
};

// 无状态解析入口。成功时 out 为根值并返回 true；失败时 out 归位为 Null。
class JsonParser
{
public:
    static bool Parse(std::wstring_view text, JsonValue& out);
};

}  // namespace raindock
