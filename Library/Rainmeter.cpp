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

// ===========================================================================
// SkinRegistry：扫描 Skins 根目录，登记 config -> iniPath
// ===========================================================================
void SkinRegistry::Refresh(const std::wstring& skinsRoot)
{
    m_Skins.clear();
    if (skinsRoot.empty()) return;

    std::wstring root = skinsRoot;
    if (root.back() != L'\\' && root.back() != L'/') root += L'\\';

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = ::FindFirstFileW((root + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (fd.cFileName[0] == L'.') continue;   // 跳过 . / ..

        const std::wstring dir = root + fd.cFileName;
        std::wstring iniPath = dir + L"\\skin.ini";

        DWORD attr = ::GetFileAttributesW(iniPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            // 无 skin.ini：退化为目录下首个 .ini。
            iniPath.clear();
            WIN32_FIND_DATAW ifd{};
            HANDLE ih = ::FindFirstFileW((dir + L"\\*.ini").c_str(), &ifd);
            if (ih != INVALID_HANDLE_VALUE) {
                do {
                    if ((ifd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        iniPath = dir + L"\\" + ifd.cFileName;
                        break;
                    }
                } while (::FindNextFileW(ih, &ifd));
                ::FindClose(ih);
            }
        }

        if (iniPath.empty()) continue;
        m_Skins.emplace_back(fd.cFileName, iniPath);
    } while (::FindNextFileW(hFind, &fd));
    ::FindClose(hFind);
}

bool SkinRegistry::Find(const std::wstring& config, std::wstring& iniPath) const
{
    for (const auto& kv : m_Skins) {
        if (kv.first == config) { iniPath = kv.second; return true; }
    }
    return false;
}

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
    m_SkinRegistry = std::make_unique<SkinRegistry>();
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

void CRainmeter::RefreshSkinRegistry()
{
    if (m_SkinRegistry) m_SkinRegistry->Refresh(m_SkinRootPath);
}

Skin* CRainmeter::ActivateConfig(const std::wstring& config)
{
    if (!m_SkinRegistry) return nullptr;
    std::wstring iniPath;
    if (!m_SkinRegistry->Find(config, iniPath)) return nullptr;
    return ActivateSkin(config, iniPath);
}

void CRainmeter::DeactivateConfig(const std::wstring& config)
{
    auto it = m_Skins.find(config);
    if (it == m_Skins.end()) return;
    delete it->second;
    m_Skins.erase(it);
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
