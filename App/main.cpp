/*
 * RainDeskPlus - Desktop beautification platform
 * Based on Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * M2 Smoke 验证入口：
 *   1) 在 exe 目录生成 smoke.ini（Time/CPU/Memory/Net 四 Measure + Variables）
 *   2) ConfigParser 加载，断言段序 / 变量展开 / 数值读取
 *   3) 各 Measure Initialize → Update×3（间隔采样，让差分类 Measure 产出真值）
 *   4) 断言结果写入 smoke_result.txt（GUI 子系统无 stdout，落盘供脚本断言）
 *   5) 全部通过返回 0，任一失败返回 1
 *
 * Build: 链接 RainDeskPlusCore (+RainDeskPlusDock)。
 */
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>   // netioapi.h 的 MIB_IF_TABLE2/GetIfTable2 由 _WS2IPDEF_ 解锁
#include <iphlpapi.h>

#include "ConfigParser.h"
#include "MeasureTime.h"
#include "MeasureCPU.h"
#include "MeasureMemory.h"
#include "MeasureNet.h"
#include "Rainmeter.h"

using namespace raindock;

namespace {

// ---------- 小工具 ----------

std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? dir.substr(0, slash + 1) : dir;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              out.data(), n, nullptr, nullptr);
    }
    return out;
}

bool WriteUtf8File(const std::wstring& path, const std::string& utf8)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(f);
}

// ---------- Smoke 框架 ----------

struct SmokeLog
{
    std::string text;
    int pass = 0;
    int fail = 0;

    void Record(bool ok, const char* name, const std::string& detail)
    {
        if (ok) ++pass; else ++fail;
        text += ok ? "PASS " : "FAIL ";
        text += name;
        text += " | ";
        text += detail;
        text += "\n";
    }
};

const char* kSmokeIni =
    "; RainDeskPlus M2 Smoke 测试配置\n"
    "[Variables]\n"
    "FontFace=Segoe UI\n"
    "\n"
    "[MeasureTime]\n"
    "Measure=Time\n"
    "Format=%Y-%m-%d %H:%M:%S\n"
    "FontFace=#FontFace#\n"
    "\n"
    "[MeasureCPU]\n"
    "Measure=CPU\n"
    "Processor=0\n"
    "\n"
    "[MeasureMemory]\n"
    "Measure=Memory\n"
    "Mode=UsedPercent\n"
    "\n"
    "[MeasureNet]\n"
    "Measure=Net\n"
    "Type=InOctets\n"
    "Cumulative=1\n"
    "Interface=Best\n";

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    const std::wstring exeDir   = GetExeDir();
    const std::wstring iniPath  = exeDir + L"smoke.ini";
    const std::wstring outPath  = exeDir + L"smoke_result.txt";

    SmokeLog log;

    // ---------- 1) 生成 smoke.ini ----------
    const bool iniWritten = WriteUtf8File(iniPath, kSmokeIni);
    log.Record(iniWritten, "ini-write", iniWritten ? WideToUtf8(iniPath) : "ofstream failed");

    // ---------- 2) ConfigParser：加载 + 段序 + 变量展开 + 读取器 ----------
    ConfigParser parser;
    const bool loaded = parser.LoadFile(iniPath);
    log.Record(loaded, "parser-load", loaded ? "LoadFile ok" : "LoadFile failed");

    if (loaded) {
        // 段序保持（Ini 出现顺序）
        const auto sections = parser.GetSections();
        bool orderOk = (sections.size() >= 5) &&
                       (sections[0] == L"Variables") &&
                       (sections[1] == L"MeasureTime") &&
                       (sections[2] == L"MeasureCPU") &&
                       (sections[3] == L"MeasureMemory") &&
                       (sections[4] == L"MeasureNet");
        log.Record(orderOk, "parser-section-order", "first5=Variables,Time,CPU,Memory,Net");

        // ReadString 自动 #Var# 展开
        const std::wstring face = parser.ReadString(L"MeasureTime", L"FontFace", L"");
        log.Record(face == L"Segoe UI", "parser-var-expand",
                   "FontFace=" + WideToUtf8(face));

        // ReplaceVariables + SetVariable 合并视图
        parser.SetVariable(L"Greet", L"hello");
        std::wstring expand = L"#Greet# world";
        parser.ReplaceVariables(expand);
        log.Record(expand == L"hello world", "parser-setvar",
                   "expand=" + WideToUtf8(expand));

        // 数值读取器
        const int proc = parser.ReadInt(L"MeasureCPU", L"Processor", -1);
        log.Record(proc == 0, "parser-read-int", "Processor=" + std::to_string(proc));
    }

    // ---------- 3) Measure 全链路 ----------
    // Time
    {
        MeasureTime time(nullptr, L"MeasureTime");
        time.Initialize(parser, iniPath);
        time.Update();
        const wchar_t* s = time.GetString();
        const std::wstring str(s ? s : L"");
        const bool ok = str.size() >= 19 && str.find(L'-') != std::wstring::npos
                        && str.find(L':') != std::wstring::npos;
        log.Record(ok, "measure-time", "GetString=" + WideToUtf8(str));
    }

    // CPU：3 次差分采样
    {
        MeasureCPU cpu(nullptr, L"MeasureCPU");
        cpu.Initialize(parser, iniPath);
        cpu.Update(); ::Sleep(400);
        cpu.Update(); ::Sleep(400);
        cpu.Update();
        const double v = cpu.GetValue();
        const bool ok = (v >= 0.0 && v <= 100.0);
        log.Record(ok, "measure-cpu", "value=" + std::to_string(v) + " (expect 0-100)");
    }

    // Memory：UsedPercent 模式
    {
        MeasureMemory mem(nullptr, L"MeasureMemory");
        mem.Initialize(parser, iniPath);
        mem.Update();
        const double v = mem.GetValue();
        const double maxV = mem.GetMaxValue();
        const bool ok = (v > 0.0 && v <= 100.0) && (maxV > 0.0);
        log.Record(ok, "measure-memory",
                   "value=" + std::to_string(v) + " max=" + std::to_string(maxV));
    }

    // Net：Cumulative InOctets（开机以来必有入流量）+ 接口级 dump 诊断
    {
        MeasureNet net(nullptr, L"MeasureNet");
        net.Initialize(parser, iniPath);
        net.Update();
        const double v = net.GetValue();
        const bool ok = (v > 0.0);
        log.Record(ok, "measure-net", "best-cumulative-in=" + std::to_string(v));

        // 诊断：dump GetIfTable2 有流量的非回环接口（验证 Best 选择数据源）
        PMIB_IF_TABLE2 ifTable = nullptr;
        if (::GetIfTable2(&ifTable) == NO_ERROR && ifTable) {
            for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
                const MIB_IF_ROW2& r = ifTable->Table[i];
                if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                if (r.InOctets == 0 && r.OutOctets == 0) continue;
                char row[256];
                std::snprintf(row, sizeof(row), "  ifidx=%lu in=%llu out=%llu oper=%d",
                              r.InterfaceIndex,
                              static_cast<unsigned long long>(r.InOctets),
                              static_cast<unsigned long long>(r.OutOctets),
                              static_cast<int>(r.OperStatus));
                log.text += row;
                log.text += "\n";
            }
            ::FreeMibTable(ifTable);
        }
    }

    // ---------- 4) 骨架生命周期 API 不回归 ----------
    {
        auto& rainmeter = CRainmeter::GetInstance();
        const bool ok = rainmeter.Initialize(hInstance);
        log.Record(ok, "rainmeter-init", ok ? "CRainmeter::Initialize ok" : "failed");
        if (ok) rainmeter.Finalize();
    }

    // ---------- 5) 汇总落盘 ----------
    char summary[64];
    std::snprintf(summary, sizeof(summary), "SMOKE SUMMARY PASS=%d FAIL=%d",
                  log.pass, log.fail);
    log.text += summary;
    log.text += "\n";

    const bool outWritten = WriteUtf8File(outPath, log.text);
    if (!outWritten) {
        std::fprintf(stderr, "[Smoke] cannot write %ls\n", outPath.c_str());
        return 1;
    }
    return (log.fail == 0) ? 0 : 1;
}
