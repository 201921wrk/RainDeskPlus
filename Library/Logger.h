// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE file).

#pragma once

#include <Windows.h>
#include <cstdarg>
#include <string>
#include <list>
#include <chrono>
#include "../Common/CriticalSection.h"

// RainDeskPlus adapter: Section lives at global scope (upstream TUs copied
// verbatim) while Skin / Measure live in `namespace raindock` (M4/M5 local
// headers).  Explicit using-declarations at global scope make every bare
// reference inside this header (and every TU that includes Logger.h AFTER
// Library/StdAfx.h) resolve consistently, regardless of the local
// `using namespace raindock;` state.
class Section;
namespace raindock { class Skin; class Measure; class Meter; }
using raindock::Skin;
using raindock::Measure;
using raindock::Meter;

// Singleton class to handle and store log messages and control the log file.
class Logger
{
public:
	enum class Level
	{
		Error   = 1,
		Warning = 2,
		Notice  = 3,
		Debug   = 4
	};

	struct Entry
	{
		Level level = Level::Notice;
		std::wstring timestamp;
		std::wstring source;
		std::wstring message;
	};

	static Logger& GetInstance();

	void SetLogFilePath(const std::wstring& path) { m_LogFilePath = path; }

	void StartLogFile();
	void StopLogFile();
	void DeleteLogFile();

	bool IsLogToFile() const { return m_LogToFile; }
	void SetLogToFile(bool logToFile);

	void Log(Logger::Entry* entry);
	void Log(Level level, const WCHAR* source, const WCHAR* msg);
	void LogVF(Level level, const WCHAR* source, const WCHAR* format, va_list args);
	void LogSkinVF(Logger::Level level, Skin* skin, const WCHAR* format, va_list args);
	void LogSkinSVF(Logger::Level level, Skin* skin, const WCHAR* section, const WCHAR* format, va_list args);
	void LogSection(Logger::Level level, Section* section, const WCHAR* message);
	void LogSectionVF(Logger::Level level, Section* section, const WCHAR* format, va_list args);
	void LogMeasureVF(Logger::Level level, Measure* section, const WCHAR* format, va_list args);

	const std::wstring& GetLogFilePath() const { return m_LogFilePath; }

	const std::list<Entry>& GetEntries() const { return m_Entries; }

private:
	void LogInternal(Level level, std::chrono::system_clock::time_point timestamp, const WCHAR* source, const WCHAR* msg);
	void WriteToLogFile(const Entry& entry);

	Logger();
	~Logger();

	Logger(const Logger& other) = delete;
	Logger& operator=(Logger other) = delete;

	bool m_LogToFile;
	std::wstring m_LogFilePath;

	std::list<Entry> m_Entries;

	CriticalSection m_CsLog;
	CriticalSection m_CsLogDelay;
};

// Convenience functions.
inline Logger& GetLogger() { return Logger::GetInstance(); }

#define RM_LOGGER_DEFINE_LOG_FUNCTIONS(name) \
	inline void Log ## name(const WCHAR* msg) \
	{ \
		GetLogger().Log(Logger::Level::name, L"", msg); \
	} \
	\
	inline void Log ## name ## F(const WCHAR* format, ...) \
	{ \
		va_list args; \
		va_start(args, format); \
		GetLogger().LogVF(Logger::Level::name, L"", format, args); \
		va_end(args); \
	} \
	inline void Log ## name ## SF(Skin* skin, const WCHAR* section, const WCHAR* format, ...) \
	{ \
		va_list args; \
		va_start(args, format); \
		GetLogger().LogSkinSVF(Logger::Level::name, skin, section, format, args); \
		va_end(args); \
	} \
	\
	inline void Log ## name ## F(Section* section, const WCHAR* format, ...) \
	{ \
		va_list args; \
		va_start(args, format); \
		GetLogger().LogSectionVF(Logger::Level::name, section, format, args); \
		va_end(args); \
	} \
	\
	inline void Log ## name ## F(Skin* skin, const WCHAR* format, ...) \
	{ \
		va_list args; \
		va_start(args, format); \
		GetLogger().LogSkinVF(Logger::Level::name, skin, format, args); \
		va_end(args); \
	} \
	\
	inline void Log ## name ## F(Measure* measure, const WCHAR* format, ...) \
	{ \
		va_list args; \
		va_start(args, format); \
		GetLogger().LogMeasureVF(Logger::Level::name, measure, format, args); \
		va_end(args); \
	}

RM_LOGGER_DEFINE_LOG_FUNCTIONS(Error)
RM_LOGGER_DEFINE_LOG_FUNCTIONS(Warning)
RM_LOGGER_DEFINE_LOG_FUNCTIONS(Notice)
RM_LOGGER_DEFINE_LOG_FUNCTIONS(Debug)
#undef RM_LOGGER_DEFINE_LOG_FUNCTIONS

// Meter*-context overloads used by Mouse.cpp (e.g. LogErrorF(m_Meter, L"...")).
// Batch-2: Meter does not yet derive from Section, so route as labelled
// level-messages; the context pointer is logged as a source label.
#define RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER(name) \
    inline void Log ## name ## F(Meter* meter, const WCHAR* format, ...) \
    { \
        va_list args; va_start(args, format); \
        GetLogger().LogVF(Logger::Level::name, (meter) ? L"Meter" : L"", format, args); \
        va_end(args); \
    }
RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER(Error)
RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER(Warning)
RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER(Notice)
RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER(Debug)
#undef RM_LOGGER_DEFINE_LOG_FUNCTIONS_METER
