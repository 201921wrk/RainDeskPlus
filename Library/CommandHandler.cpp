/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/CommandHandler.cpp（骨架阶段占位）。
 * TODO(Phase1): 提取 Rainmeter 完整实现：
 *   - 命令解析（引号、参数转义、延迟 [Measure]）
 *   - 注册所有内置 Bang：!ActivateConfig / !DeactivateConfig / !Refresh /
 *     !Update / !Hide / !Show / !Toggle / !SetVariable / !Execute ...
 */
#include "CommandHandler.h"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace raindock {

CommandHandler::CommandHandler()
{
    // TODO(Phase1): 注册内置 Bang。骨架阶段仅占位。
    RegisterBang(L"Refresh", [](const std::wstring&, Skin*) {
        // TODO: 触发当前皮肤重载
    });
    RegisterBang(L"Quit", [](const std::wstring&, Skin*) {
        ::PostQuitMessage(0);
    });
}

CommandHandler::~CommandHandler() = default;

void CommandHandler::RegisterBang(const std::wstring& name, BangFn fn)
{
    m_Bangs[name] = std::move(fn);
}

void CommandHandler::Execute(const std::wstring& command, Skin* skin)
{
    if (command.empty()) return;
    // TODO(Phase1): 完整解析（引号、多命令分隔符、变量展开）。
    std::wstring cmd = command;
    // 去前导空白
    auto a = cmd.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return;
    cmd.erase(0, a);

    if (cmd[0] == L'!')
    {
        // Bang：!Name Args
        std::wstring rest = cmd.substr(1);
        auto sp = rest.find_first_of(L" \t");
        std::wstring name = (sp == std::wstring::npos) ? rest : rest.substr(0, sp);
        std::wstring args = (sp == std::wstring::npos) ? L"" : rest.substr(sp + 1);
        auto it = m_Bangs.find(name);
        if (it != m_Bangs.end()) it->second(args, skin);
        // TODO(Phase1): 未知 Bang 警告。
    }
    else
    {
        // 非 Bang：当作可执行命令
        // TODO(Phase1): ShellExecuteW(nullptr, L"open", cmd.c_str(), ...).
    }
}

}  // namespace raindock
