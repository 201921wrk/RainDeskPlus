/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * 对应 Rainmeter: Library/Rainmeter.h（简化版，单例 + 生命周期 + 皮肤集合 + 命令分发）。
 */
#ifndef RAINDOCK_LIBRARY_RAINMETER_H_
#define RAINDOCK_LIBRARY_RAINMETER_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Batch-2 forward decl: upstream Section class lives at global scope.
class Section;

namespace raindock {

class Skin;
class Measure;  // Batch-2 forward decl.
class CommandHandler;

// 皮肤清单注册表（扫描 Skins/ 目录）。每个子目录视为一个 config，
// 目录内的 skin.ini（或首个 .ini）作为其加载入口。
class SkinRegistry
{
public:
    void Refresh(const std::wstring& skinsRoot);
    // 返回 (config, iniPath) 列表（按目录枚举顺序）。
    const std::vector<std::pair<std::wstring, std::wstring>>& GetSkins() const { return m_Skins; }
    // 按 config 名查找 ini 路径。
    bool Find(const std::wstring& config, std::wstring& iniPath) const;

private:
    std::vector<std::pair<std::wstring, std::wstring>> m_Skins;
};

// 应用核心单例。对应 Rainmeter CRainmeter。
class CRainmeter
{
public:
    static CRainmeter& GetInstance();

    // 生命周期
    bool Initialize(HINSTANCE hInstance);
    void Finalize();

    // 皮肤集合管理
    Skin* ActivateSkin(const std::wstring& config, const std::wstring& iniPath);
    void  DeactivateSkin(Skin* skin);
    const std::map<std::wstring, Skin*>& GetSkins() const { return m_Skins; }

    // ===== M6 皮肤切换 =====
    // Skins 根目录（默认为空；!ActivateConfig 前需先 SetSkinRootPath）。
    void SetSkinRootPath(const std::wstring& path) { m_SkinRootPath = path; }
    const std::wstring& GetSkinRootPath() const { return m_SkinRootPath; }
    // 重新扫描 Skins 根目录，刷新皮肤注册表。
    void RefreshSkinRegistry();
    const SkinRegistry* GetSkinRegistry() const { return m_SkinRegistry.get(); }
    // 按 config 名从注册表查找 iniPath 并激活。
    Skin* ActivateConfig(const std::wstring& config);
    // 按 config 名停用皮肤。
    void  DeactivateConfig(const std::wstring& config);

    // 命令分发（Bang 命令）
    void ExecuteCommand(const std::wstring& command, Skin* skin);

    // ===== Batch-2 upstream compatibility =====
    // Upstream Section / IfActions call `GetRainmeter().ExecuteActionCommand(
    // action.c_str(), context)` with either a global ::Section* (Section base)
    // or a local raindock::Measure* (IfActions DoIfActions uses &measure).
    // Both are routed to ExecuteCommand() — context skin is recovered from
    // the Measure/Section when we can.
    void ExecuteActionCommand(const WCHAR* command, ::Section* ctx);
    void ExecuteActionCommand(const WCHAR* command, Measure* ctx);

    // 访问器
    HINSTANCE GetInstanceHandle() const { return m_hInstance; }

private:
    CRainmeter();
    ~CRainmeter();
    CRainmeter(const CRainmeter&) = delete;
    CRainmeter& operator=(const CRainmeter&) = delete;

    HINSTANCE                                m_hInstance = nullptr;
    std::map<std::wstring, Skin*>            m_Skins;
    std::wstring                             m_SkinRootPath;
    std::unique_ptr<SkinRegistry>            m_SkinRegistry;
    std::unique_ptr<CommandHandler>          m_CommandHandler;
    bool                                     m_Initialized = false;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_RAINMETER_H_
