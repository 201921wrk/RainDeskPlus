// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include <cstdint>
#include <string_view>

#include "../ThirdParty/pcre/config.h"
#include "../ThirdParty/pcre/pcre.h"

class Pcre
{
public:
	Pcre() : m_Pcre(nullptr), m_ErrorOffset(0), m_Offset(0) {}

	Pcre(const WCHAR* pattern, const char** error) : m_Pcre(nullptr), m_ErrorOffset(0), m_Offset(0)
	{
		Compile(pattern, error);
	}

	~Pcre() { Reset(); }

	Pcre(const Pcre&) = delete;
	Pcre& operator=(const Pcre&) = delete;

	// 累计 pcre16_compile 调用次数（进程级）。编译是重量级操作，这个计数是
	// D36-40 集成测试的确定性锚点：断言「N 个更新周期只编译一次」。
	static uint64_t GetCompileCount() { return s_CompileCount; }
	static void ResetCompileCount() { s_CompileCount = 0; }

	void Compile(const WCHAR* pattern, const char** error)
	{
		Reset();
		++s_CompileCount;
		m_Pcre = pcre16_compile(reinterpret_cast<PCRE_SPTR16>(pattern), 0, error, &m_ErrorOffset, nullptr);
	}

	void Reset()
	{
		pcre16_free(m_Pcre);
		m_Pcre = nullptr;
		m_ErrorOffset = 0;
		m_Offset = 0;
	}

	int Execute(std::wstring_view subject, int options, int* offsets, int offsetCount) const
	{
		return pcre16_exec(m_Pcre, nullptr, reinterpret_cast<PCRE_SPTR16>(subject.data()), static_cast<int>(subject.length()), m_Offset, options, offsets, offsetCount);
	}

	int GetErrorOffset() const { return m_ErrorOffset; }
	void SetOffset(int offset) { m_Offset = offset; }

	explicit operator bool() const { return m_Pcre != nullptr; }

private:
	pcre16* m_Pcre;
	int m_ErrorOffset;
	int m_Offset;

	// inline static（C++17）：Pcre 是纯头文件类，避免为此单独引入一个 .cpp。
	inline static uint64_t s_CompileCount = 0;
};
