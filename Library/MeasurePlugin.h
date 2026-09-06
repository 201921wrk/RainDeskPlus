/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeasurePlugin.h（第三方插件加载，直接复用）。
 * 插件 DLL 必须导出 Initialize / Reload / Update / Finalize。
 */
#ifndef RAINDOCK_LIBRARY_MEASURE_PLUGIN_H_
#define RAINDOCK_LIBRARY_MEASURE_PLUGIN_H_

#include "Measure.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // HMODULE

#include <string>

namespace raindock {

class MeasurePlugin : public Measure
{
public:
    MeasurePlugin(Skin* skin, const WCHAR* name);
    ~MeasurePlugin() override;

    UINT GetTypeID() override { return TypeID<MeasurePlugin>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;
    void Reload(ConfigParser& parser, const std::wstring& iniPath, double* maxValue);
    const wchar_t* GetString() override;
    void Finalize() override;

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // 插件导出函数指针类型（对应 Rainmeter PLUGIN_* 签名）。
    using FnInitialize = void(*)(void* data);
    using FnReload     = void(*)(void* data, void* rm, double* maxValue);
    using FnUpdate     = double(*)(void* data);
    using FnGetString  = const wchar_t*(*)(void* data);
    using FnFinalize   = void(*)(void* data);

    HMODULE       m_Module = nullptr;
    void*         m_Data = nullptr;       // 插件实例上下文
    FnInitialize  m_FnInitialize = nullptr;
    FnReload      m_FnReload = nullptr;
    FnUpdate      m_FnUpdate = nullptr;
    FnGetString   m_FnGetString = nullptr;
    FnFinalize    m_FnFinalize = nullptr;
    std::wstring  m_PluginPath;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_MEASURE_PLUGIN_H_
