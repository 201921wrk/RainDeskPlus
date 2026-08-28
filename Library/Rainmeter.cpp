/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Rainmeter.cpp（简化版）。
 * 骨架阶段：所有方法为空实现占位，待 Rainmeter 源码提取后填实。
 */
#include "Rainmeter.h"

#include "Skin.h"
#include "CommandHandler.h"

namespace raindock {

CRainmeter::CRainmeter() = default;

CRainmeter::~CRainmeter()
{
    Finalize();
}

CRainmeter& CRainmeter::GetInstance()
{
    static CRainmeter instance;
    return instance;
}

bool CRainmeter::Initialize(HINSTANCE hInstance)
{
    // TODO(Phase1): 提取 Rainmeter::Initialize 实现：
    //   - 注册窗口类、创建隐藏消息窗口
    //   - 初始化 SkinRegistry（扫描 Skins/ 目录）
    //   - 创建 CommandHandler
    //   - 读取全局配置 Rainmeter.ini
    // 当前：仅占位。
    m_hInstance = hInstance;
    m_CommandHandler = std::make_unique<CommandHandler>();
    m_Initialized = true;
    return true;
}

void CRainmeter::Finalize()
{
    // TODO(Phase1): 释放所有皮肤、卸载插件、销毁窗口。
    if (!m_Initialized) return;
    for (auto& kv : m_Skins) { delete kv.second; }
    m_Skins.clear();
    m_CommandHandler.reset();
    m_SkinRegistry.reset();
    m_Initialized = false;
}

Skin* CRainmeter::ActivateSkin(const std::wstring& config, const std::wstring& iniPath)
{
    // TODO(Phase1): 创建 Skin、加载 INI、创建窗口。
    auto* skin = new Skin();
    if (!skin->Load(iniPath)) { delete skin; return nullptr; }
    m_Skins[config] = skin;
    return skin;
}

void CRainmeter::DeactivateSkin(Skin* skin)
{
    // TODO(Phase1): 销毁皮肤窗口、从集合移除。
    for (auto it = m_Skins.begin(); it != m_Skins.end(); ++it) {
        if (it->second == skin) { delete skin; m_Skins.erase(it); return; }
    }
}

void CRainmeter::ExecuteCommand(const std::wstring& command, Skin* skin)
{
    if (m_CommandHandler) m_CommandHandler->Execute(command, skin);
}

}  // namespace raindock
