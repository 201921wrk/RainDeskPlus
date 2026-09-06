/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasurePlugin.cpp（骨架，仅对齐接口）。
 * TODO(M3): 提取 Rainmeter 实现，LoadLibrary + GetProcAddress 加载
 *   Initialize / Reload / Update / GetString / Finalize 并按签名调用。
 */
#include "MeasurePlugin.h"
#include "ConfigParser.h"

namespace raindock {

MeasurePlugin::MeasurePlugin(Skin* skin, const WCHAR* name)
    : Measure(skin, name)
{
}

MeasurePlugin::~MeasurePlugin()
{
    Finalize();
}

void MeasurePlugin::ReadOptions(ConfigParser& parser, std::wstring_view section)
{
    Measure::ReadOptions(parser, section);
    m_PluginPath = parser.ReadString(section, L"Plugin", m_PluginPath);
}

void MeasurePlugin::Initialize(ConfigParser& parser, const std::wstring& iniPath)
{
    Measure::Initialize(parser, iniPath);

    if (m_PluginPath.empty()) return;  // 未指定 Plugin=，保持未加载状态

    // 相对路径按主程序 exe 目录解析（对齐 Rainmeter 插件目录搜索语义）
    std::wstring fullPath = m_PluginPath;
    const bool isAbsolute =
        (fullPath.size() >= 2 && fullPath[1] == L':') ||   // 盘符
        (!fullPath.empty() && (fullPath[0] == L'\\' || fullPath[0] == L'/'));  // UNC/根
    if (!isAbsolute) {
        wchar_t exePath[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring dir(exePath);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash + 1);
        fullPath = dir + fullPath;
    }

    m_Module = ::LoadLibraryW(fullPath.c_str());
    if (!m_Module) return;  // 加载失败保持静默（M3 接入日志系统后记录）

    m_FnInitialize = reinterpret_cast<FnInitialize>(::GetProcAddress(m_Module, "Initialize"));
    m_FnReload     = reinterpret_cast<FnReload>(::GetProcAddress(m_Module, "Reload"));
    m_FnUpdate     = reinterpret_cast<FnUpdate>(::GetProcAddress(m_Module, "Update"));
    m_FnGetString  = reinterpret_cast<FnGetString>(::GetProcAddress(m_Module, "GetString"));
    m_FnFinalize   = reinterpret_cast<FnFinalize>(::GetProcAddress(m_Module, "Finalize"));

    if (m_FnInitialize) m_FnInitialize(&m_Data);
}

void MeasurePlugin::Reload(ConfigParser& /*parser*/, const std::wstring& /*iniPath*/, double* /*maxValue*/)
{
    if (m_FnReload) m_FnReload(m_Data, nullptr, nullptr);
}

void MeasurePlugin::UpdateValue()
{
    if (m_FnUpdate) m_Value = m_FnUpdate(m_Data);
    else m_Value = 0.0;
}

const wchar_t* MeasurePlugin::GetString()
{
    if (m_FnGetString) return CheckSubstitute(m_FnGetString(m_Data));
    return CheckSubstitute(m_StringValue.c_str());
}

void MeasurePlugin::Finalize()
{
    if (m_FnFinalize) m_FnFinalize(m_Data);
    m_FnInitialize = nullptr;
    m_FnReload = nullptr;
    m_FnUpdate = nullptr;
    m_FnGetString = nullptr;
    m_FnFinalize = nullptr;
    if (m_Module) { ::FreeLibrary(m_Module); m_Module = nullptr; }
    Measure::Finalize();
}

}  // namespace raindock
