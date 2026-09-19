/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Logger.cpp（最小实现）。
 * 上游 Logger.cpp 依赖 DialogDebug / Util / System / resource.h /
 * GetFormattedString / ShowMessage 等模块，这些不在当前提取范围内；本文件以
 * OutputDebugString + 内存环形缓冲替代，满足 Section / IfActions / Mouse /
 * Group 引用的 8 个 LogVF / LogSectionVF / LogMeasureVF / LogSkinVF 重载。
 *
 * 完整的文件日志与托盘调试窗口在后续批次（ConfigParser 全量 / 插件）接入。
 */

#include "StdAfx.h"
#include "Logger.h"
#include "Section.h"
#include "Skin.h"
#include "Measure.h"

#include <cwchar>
#include <iomanip>
#include <sstream>

namespace {

constexpr size_t kMaxEntries = 50;

void VAppendFormat(std::wstring& out, const WCHAR* fmt, va_list args) {
    if (!fmt) return;
    va_list argsCopy;
    va_copy(argsCopy, args);
    int len = _vscwprintf(fmt, argsCopy);
    va_end(argsCopy);
    if (len <= 0) return;
    // _vscwprintf 返回的 len 不含终止符。用 len+1 的独立缓冲承载格式化结果，
    // 不再依赖 `data()[size()]` 是否可写的实现细节（详见 D10 审查 #10）。
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    vswprintf_s(buf.data(), buf.size(), fmt, args);
    out.assign(buf.c_str(), static_cast<size_t>(len));
}

std::wstring LevelSuffix(Logger::Level l) {
    switch (l) {
        case Logger::Level::Error:   return L"ERROR: ";
        case Logger::Level::Warning: return L"WARN:  ";
        case Logger::Level::Notice:  return L"NOTE:  ";
        case Logger::Level::Debug:   return L"DEBUG: ";
    }
    return L"LOG: ";
}

std::wstring TimestampNow() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(2) << st.wHour << L':'
        << std::setw(2) << st.wMinute << L':'
        << std::setw(2) << st.wSecond << L' ';
    return oss.str();
}

void EmitODS(const std::wstring& line) {
    ::OutputDebugStringW(line.c_str());
}

}  // namespace

Logger::Logger()  : m_LogToFile(false) {}
Logger::~Logger() = default;

Logger& Logger::GetInstance() {
    static Logger s_Instance;
    return s_Instance;
}

void Logger::StartLogFile()  { /* Batch-2 no-op */ }
void Logger::StopLogFile()   { /* Batch-2 no-op */ }
void Logger::DeleteLogFile() { /* Batch-2 no-op */ }
void Logger::SetLogToFile(bool v) { m_LogToFile = v; }

void Logger::LogInternal(Level lvl, std::chrono::system_clock::time_point, const WCHAR* src, const WCHAR* msg) {
    Entry e;
    e.level = lvl;
    e.timestamp = TimestampNow();
    e.source = src ? src : L"";
    e.message = msg ? msg : L"";
    // 环形缓冲的裁剪与写入必须整体加锁：Logger 可能被多个线程（定时器/插件）
    // 同时写入（详见 D10 审查 #9）。锁只覆盖容器操作，EmitODS 在锁外执行。
    {
        CriticalSectionLock lock(m_CsLog);
        if (m_Entries.size() >= kMaxEntries) m_Entries.pop_front();
        m_Entries.push_back(e);
    }

    std::wstring line = e.timestamp + LevelSuffix(lvl);
    if (!e.source.empty()) { line.append(L"[").append(e.source).append(L"] "); }
    line.append(e.message);
    if (!line.empty() && line.back() != L'\n') line.push_back(L'\n');
    EmitODS(line);
}

void Logger::WriteToLogFile(const Entry&) { /* Batch-2 no-op */ }

void Logger::Log(Entry* entry) {
    if (!entry) return;
    LogInternal(entry->level, std::chrono::system_clock::now(),
                entry->source.c_str(), entry->message.c_str());
    WriteToLogFile(*entry);
}

void Logger::Log(Level lvl, const WCHAR* src, const WCHAR* msg) {
    LogInternal(lvl, std::chrono::system_clock::now(), src, msg);
}

void Logger::LogVF(Level lvl, const WCHAR* src, const WCHAR* fmt, va_list args) {
    std::wstring msg;
    VAppendFormat(msg, fmt, args);
    LogInternal(lvl, std::chrono::system_clock::now(), src, msg.c_str());
}

void Logger::LogSkinVF(Level lvl, Skin* skin, const WCHAR* fmt, va_list args) {
    std::wstring msg;
    VAppendFormat(msg, fmt, args);
    const WCHAR* src = L"skin";
    // Skin name lookup not critical for Batch-2; use generic tag.
    LogInternal(lvl, std::chrono::system_clock::now(), src, msg.c_str());
    (void)skin;
}

void Logger::LogSkinSVF(Level lvl, Skin* skin, const WCHAR* section, const WCHAR* fmt, va_list args) {
    std::wstring msg;
    VAppendFormat(msg, fmt, args);
    std::wstring src;
    src = section ? section : L"";
    LogInternal(lvl, std::chrono::system_clock::now(), src.c_str(), msg.c_str());
    (void)skin;
}

void Logger::LogSection(Level lvl, Section* section, const WCHAR* msg) {
    const WCHAR* src = section ? section->GetName() : L"section";
    LogInternal(lvl, std::chrono::system_clock::now(), src, msg);
}

void Logger::LogSectionVF(Level lvl, Section* section, const WCHAR* fmt, va_list args) {
    std::wstring msg;
    VAppendFormat(msg, fmt, args);
    LogSection(lvl, section, msg.c_str());
}

void Logger::LogMeasureVF(Level lvl, Measure* measure, const WCHAR* fmt, va_list args) {
    std::wstring msg;
    VAppendFormat(msg, fmt, args);
    // Measure derives from Section upstream; in Batch-2 local Measure doesn't,
    // so cast to Section* is not safe. Use generic tag "measure".
    const WCHAR* src = L"measure";
    if (measure) {
        // Best-effort name lookup via Section's GetName (Measure now derives
        // from Section).
        src = measure->GetName();
    }
    LogInternal(lvl, std::chrono::system_clock::now(), src, msg.c_str());
}
