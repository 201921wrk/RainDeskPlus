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
#include "Measure.h"
#include "CommandHandler.h"
// Batch-2: global Section base type (used by ExecuteActionCommand(ctx)).
#include "Section.h"

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

// ===========================================================================
// Batch-2 upstream adapter: ExecuteActionCommand overloads
// ===========================================================================
// Global ::Section context — used by Section::DoUpdateAction().  At this
// stage local raindock::Skin is not derived from nor convertible to global
// ::Skin (Section::GetSkin return type).  We therefore pass nullptr skin
// context to ExecuteCommand() — sufficient for Batch-2 compile assertion;
// runtime context-sensitivity (e.g. bang targets a given skin/window) will
// be restored when Measure/Meter derive from ::Section in Batch-3/4.
void CRainmeter::ExecuteActionCommand(const WCHAR* command, ::Section* /*ctx*/)
{
    if (!command || !*command) return;
    ExecuteCommand(std::wstring(command), nullptr);
}

// Local raindock::Measure context — used by IfActions::DoIfActions().  Here
// we can safely recover the skin pointer from the measure.
void CRainmeter::ExecuteActionCommand(const WCHAR* command, Measure* ctx)
{
    if (!command || !*command) return;
    Skin* skin = ctx ? ctx->GetSkin() : nullptr;
    ExecuteCommand(std::wstring(command), skin);
}

}  // namespace raindock
