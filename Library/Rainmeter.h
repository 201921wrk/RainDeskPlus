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

// 皮肤清单注册表（扫描 Skins/ 目录）。骨架阶段最小占位。
class SkinRegistry
{
public:
    void Refresh() { /* TODO(Phase1): 扫描 Skins/ 下所有 .ini 并登记 */ }
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
    std::unique_ptr<SkinRegistry>            m_SkinRegistry;
    std::unique_ptr<CommandHandler>          m_CommandHandler;
    bool                                     m_Initialized = false;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_RAINMETER_H_
