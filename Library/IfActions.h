// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

// IfMatch 的正则缓存持有 Pcre 实例（仅指针，故此处前置声明即可）。
class Pcre;

// RainDeskPlus adapter: ConfigParser / Measure / Skin are defined inside
// `namespace raindock` (M4/M5 skeleton), but verbatim upstream TUs reference
// them with bare names.  Mirror the Logger.h / Mouse.h pattern: forward in
// the real namespace, then explicit using-decls at global scope so bare
// references resolve consistently even BEFORE `using namespace raindock;`
// from Library/StdAfx.h becomes active.
namespace raindock { class ConfigParser; class Measure; class Skin; }
using raindock::ConfigParser;
using raindock::Measure;
using raindock::Skin;

// Helper class for IfCondition/IfMatch
class IfState
{
public:
	IfState(const std::wstring& value, const std::wstring& trueAction, const std::wstring& falseAction) :
		value(),
		tAction(),
		fAction(),
		parseError(false),
		tCommitted(false),
		fCommitted(false)
	{
		Set(value, trueAction, falseAction);
	}

	inline void Set(const std::wstring& newValue, const std::wstring& trueAction, const std::wstring& falseAction)
	{
		this->value = newValue;
		this->tAction = trueAction;
		this->fAction = falseAction;
	}

	std::wstring value;			// IfCondition/IfMatch
	std::wstring tAction;		// IfTrueAction/IfMatchAction
	std::wstring fAction;		// IfFalseAction/IfNotMatchAction
	bool parseError;
	bool tCommitted;
	bool fCommitted;
};

class IfActions
{
public:
	IfActions();
	~IfActions();

	IfActions(const IfActions& other) = delete;
	IfActions& operator=(IfActions other) = delete;

	void ReadOptions(ConfigParser& parser, std::wstring_view section);
	void ReadConditionOptions(ConfigParser& parser, std::wstring_view section);
	void DoIfActions(Measure& measure, double value);
	void SetState(double& value);

private:
	double m_AboveValue;
	double m_BelowValue;
	int64_t m_EqualValue;

	std::wstring m_AboveAction;
	std::wstring m_BelowAction;
	std::wstring m_EqualAction;

	bool m_AboveCommitted;
	bool m_BelowCommitted;
	bool m_EqualCommitted;

	std::vector<IfState> m_Conditions;
	bool m_ConditionMode;

	std::vector<IfState> m_Matches;
	bool m_MatchMode;

	// IfMatch 正则编译缓存（D36-40 性能项 #1）：pcre16_compile 属于重量级操作，
	// 原实现每个更新周期都要对同一个表达式重新编译。此缓存与 m_Matches 同索引，
	// 仅在表达式文本发生变化（如 !Refresh 重读配置）时才重新编译。
	// 注意：此处只持有 Pcre 指针，故头文件保持前置声明即可；实际析构发生在 .cpp。
	struct MatchRegexCache
	{
		std::wstring pattern;				// 上次编译使用的表达式，用于脏检查
		std::unique_ptr<Pcre> re;			// 编译产物；非空表示「已尝试编译」（其内部可能为空 = 编译失败）
		std::string error;					// 编译失败时的错误描述，供 LogErrorF 复用
	};
	std::vector<MatchRegexCache> m_MatchRegex;
};
