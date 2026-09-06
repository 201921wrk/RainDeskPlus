// RainDeskPlus - Minimal Logger implementation for Batch-2 compile.
// The upstream rainmeter/rainmeter Logger.cpp references DialogDebug / Util /
// System / resource.h / GetFormattedString / ShowMessage etc. which are
// intentionally NOT part of Batch-2. This file replaces that with a tiny
// OutputDebugString + in-memory ring implementation, enough for the 8 LogVF /
// LogSectionVF / LogMeasureVF / LogSkinVF overloads referenced by Section /
// IfActions / Mouse / Group.
//
// Runtime fidelity (file log, Rainmeter tray debug dialog) will be upgraded in
// Batch-3 (ConfigParser full) / Batch-6 (plugins) when we pull those modules.

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
    out.resize(static_cast<size_t>(len));
    vswprintf_s(out.data(), static_cast<size_t>(len) + 1, fmt, args);
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
    if (m_Entries.size() >= kMaxEntries) m_Entries.pop_front();
    m_Entries.push_back(e);

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
