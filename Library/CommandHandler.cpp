/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/CommandHandler.cpp。
 * M5 骨架：实现常用 Bang 全集（当前皮肤上下文）：
 *   !Update / !Redraw / !SetOption / !SetVariable / !Hide / !Show /
 *   !Toggle / !Refresh / !Quit
 * M6 皮肤切换：
 *   !ActivateConfig / !DeactivateConfig
 * 高级 Bang（!MoveMeter / !SetWallpaper / …）按后续阶段接入。
 */
#include "CommandHandler.h"

#include "Skin.h"
#include "ConfigParser.h"
#include "Rainmeter.h"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace raindock {

namespace {

// 去两端空白。
std::wstring Trim(const std::wstring& s)
{
    auto a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    auto b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 去掉段名两侧的可选 [] 括号（!SetOption [Section] Key Value 兼容）。
std::wstring StripBrackets(const std::wstring& s)
{
    std::wstring out = Trim(s);
    if (out.size() >= 2 && out.front() == L'[' && out.back() == L']') {
        out = out.substr(1, out.size() - 2);
    }
    return out;
}

}  // namespace

CommandHandler::CommandHandler()
{
    RegisterBang(L"Update", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Update();
    });

    RegisterBang(L"Redraw", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Redraw();
    });

    RegisterBang(L"SetOption", [](const std::wstring& args, Skin* skin) {
        if (!skin) return;
        // 语法：!SetOption Section Key Value（Value 可为空或含空格）。
        auto sp1 = args.find_first_of(L" \t");
        if (sp1 == std::wstring::npos) return;
        auto sp2 = args.find_first_not_of(L" \t", sp1);
        if (sp2 == std::wstring::npos) return;
        auto sp3 = args.find_first_of(L" \t", sp2);

        const std::wstring section = StripBrackets(args.substr(0, sp1));
        const std::wstring key = (sp3 == std::wstring::npos)
            ? args.substr(sp2)
            : args.substr(sp2, sp3 - sp2);
        const std::wstring value = (sp3 == std::wstring::npos)
            ? L""
            : Trim(args.substr(sp3));
        if (section.empty() || key.empty()) return;
        skin->GetParser().SetValue(section, key, value);
    });

    RegisterBang(L"SetVariable", [](const std::wstring& args, Skin* skin) {
        if (!skin) return;
        // 语法：!SetVariable Name Value。
        auto sp = args.find_first_of(L" \t");
        if (sp == std::wstring::npos) {
            skin->GetParser().SetVariable(Trim(args), L"");
            return;
        }
        const std::wstring name = Trim(args.substr(0, sp));
        const std::wstring value = Trim(args.substr(sp));
        if (!name.empty()) skin->GetParser().SetVariable(name, value);
    });

    RegisterBang(L"Hide", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Hide();
    });

    RegisterBang(L"Show", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Show();
    });

    RegisterBang(L"Toggle", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Toggle();
    });

    RegisterBang(L"Refresh", [](const std::wstring&, Skin* skin) {
        if (skin) skin->Reload();
    });

    RegisterBang(L"ActivateConfig", [](const std::wstring& args, Skin*) {
        // 语法：!ActivateConfig ConfigName [Variant]
        auto sp = args.find_first_of(L" \t");
        const std::wstring config = Trim(args.substr(0, sp));
        if (!config.empty()) CRainmeter::GetInstance().ActivateConfig(config);
    });

    RegisterBang(L"DeactivateConfig", [](const std::wstring& args, Skin*) {
        // 语法：!DeactivateConfig ConfigName
        auto sp = args.find_first_of(L" \t");
        const std::wstring config = Trim(args.substr(0, sp));
        if (!config.empty()) CRainmeter::GetInstance().DeactivateConfig(config);
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
    std::wstring cmd = command;
    auto a = cmd.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return;
    cmd.erase(0, a);

    if (cmd[0] == L'!')
    {
        // Bang：!Name Args
        std::wstring rest = cmd.substr(1);
        auto sp = rest.find_first_of(L" \t");
        std::wstring name = (sp == std::wstring::npos) ? rest : rest.substr(0, sp);
        std::wstring args = (sp == std::wstring::npos) ? L"" : Trim(rest.substr(sp + 1));
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
