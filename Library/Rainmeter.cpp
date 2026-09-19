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

namespace {

// 从 exe 目录开始，逐级向上（含自身，最多 5 级）查找名为 "Skins" 的目录，
// 返回其绝对路径；找不到返回空串。用于 Initialize 自动定位皮肤根目录。
std::wstring AutoDetectSkinsRoot()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!::GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return {};

    std::wstring dir(exePath);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    dir.resize(slash);   // 去掉文件名，得到 exe 目录

    for (int level = 0; level <= 5; ++level) {
        const std::wstring candidate = dir + L"\\Skins";
        const DWORD attr = ::GetFileAttributesW(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return candidate;
        }
        slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
    }
    return {};
}

}  // namespace

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
    // M5：初始化核心组件，并自动定位/扫描 Skins 根目录。
    //   - 注册窗口类、隐藏消息窗口、Rainmeter.ini 读取等留待后续阶段。
    m_hInstance = hInstance;
    m_CommandHandler = std::make_unique<CommandHandler>();
    m_SkinRegistry = std::make_unique<SkinRegistry>();

    // 未显式指定 Skins 根目录时，从 exe 目录向上自动探测。
    if (m_SkinRootPath.empty()) {
        m_SkinRootPath = AutoDetectSkinsRoot();
    }
    if (!m_SkinRootPath.empty()) {
        m_SkinRegistry->Refresh(m_SkinRootPath);
    }

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
    // 同名 config 已激活：先停用旧皮肤，避免重复激活导致句柄/窗口泄漏。
    auto existing = m_Skins.find(config);
    if (existing != m_Skins.end()) {
        delete existing->second;
        m_Skins.erase(existing);
    }

    auto* skin = new Skin();
    if (!skin->Load(iniPath)) { delete skin; return nullptr; }
    m_Skins[config] = skin;

    // M5：加载成功即显示挂件窗口。窗口/渲染目标创建失败时静默降级，
    // 皮肤仍已登记，Bang 命令（!Hide/!Show/!Toggle 等）按有无窗口做 no-op。
    skin->Show(m_hInstance, SW_SHOWNOACTIVATE);
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
// Global ::Section context — used by Section::DoUpdateAction().  Section
// holds a raindock::Skin* (aliased to global Skin via `using` in Section.h),
// so we can recover the owning skin directly for context-sensitive dispatch.
void CRainmeter::ExecuteActionCommand(const WCHAR* command, ::Section* ctx)
{
    if (!command || !*command) return;
    Skin* skin = ctx ? ctx->GetSkin() : nullptr;
    ExecuteCommand(std::wstring(command), skin);
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
