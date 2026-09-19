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
 * D24 媒体控制：
 *   !CommandMeasure "MeasureName" "Command"（测量项的 Command() 落点）
 * D31-35 持久化：
 *   !WriteKeyValue Section Key Value [File]（落盘到 INI，可配合 !Refresh 生效）
 * 高级 Bang（!MoveMeter / !SetWallpaper / …）按后续阶段接入。
 */
#include "CommandHandler.h"

#include "Skin.h"
#include "ConfigParser.h"
#include "Logger.h"
#include "Measure.h"
#include "PathUtil.h"
#include "Rainmeter.h"
#include "StringUtil.h"

#include <algorithm>
#include <cwctype>
#include <vector>

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

// Bang 名大小写不敏感（Rainmeter 语义，!Refresh 与 !refresh 等价），
// 注册与查找统一按小写键进行（详见 D10 审查 #21）。
std::wstring CanonicalBangName(const std::wstring& s)
{
    std::wstring out = s;
    for (wchar_t& c : out) c = static_cast<wchar_t>(::towlower(c));
    return out;
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

// 去掉两侧成对的引号（!CommandMeasure "MeasureName" "Command" 的参数口径）。
std::wstring Unquote(const std::wstring& s)
{
    std::wstring out = Trim(s);
    if (out.size() >= 2 && out.front() == L'"' && out.back() == L'"') {
        out = out.substr(1, out.size() - 2);
    }
    return out;
}

// 引号感知分词，最多切出 maxTokens 个字段：
//   - "..." 内的内容整体作为一个字段（引号剥离），从而支持「值/路径内含空格」；
//   - 第 maxTokens 个字段吞掉剩余全部内容，便于把可选的尾部 File 参数与值区分开。
// 供 !WriteKeyValue Section Key Value [File] 使用（Rainmeter 同口径）。
void SplitTokensQuoteAware(const std::wstring& s, size_t maxTokens,
                           std::vector<std::wstring>& out)
{
    size_t i = 0;
    while (i < s.size() && out.size() < maxTokens)
    {
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
        if (i >= s.size()) break;

        std::wstring token;
        if (s[i] == L'"')
        {
            const size_t close = s.find(L'"', i + 1);
            if (close == std::wstring::npos)
            {
                token = s.substr(i + 1);
                i = s.size();
            }
            else
            {
                token = s.substr(i + 1, close - i - 1);
                i = close + 1;
            }
        }
        else if (out.size() + 1 == maxTokens)
        {
            token = Trim(s.substr(i));
            i = s.size();
        }
        else
        {
            const size_t end = s.find_first_of(L" \t", i);
            if (end == std::wstring::npos)
            {
                token = s.substr(i);
                i = s.size();
            }
            else
            {
                token = s.substr(i, end - i);
                i = end;
            }
        }
        out.push_back(std::move(token));
    }
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

    RegisterBang(L"WriteKeyValue", [](const std::wstring& args, Skin* skin) {
        if (!skin) return;
        // 语法：!WriteKeyValue Section Key Value [File]
        //   File 缺省为当前皮肤 ini；显式给出时相对皮肤目录解析。
        // 语义与 Rainmeter 一致：**只落盘**，不改变运行中皮肤的内存值（需配合 !Refresh 生效）。
        std::vector<std::wstring> tok;
        SplitTokensQuoteAware(args, 4, tok);
        if (tok.size() < 3) return;

        const std::wstring section = StripBrackets(tok[0]);
        const std::wstring key     = tok[1];
        const std::wstring value   = tok[2];
        if (section.empty() || key.empty()) return;

        const std::wstring iniPath = skin->GetIniPath();
        std::wstring target = (tok.size() >= 4) ? tok[3] : std::wstring();
        if (target.empty())
        {
            target = iniPath;
        }
        else if (!PathUtil::IsAbsolute(target))
        {
            target = PathUtil::GetFolderFromFilePath(iniPath) + target;
        }
        if (target.empty()) return;

        // 裸读（不做主题合并）→ 只改目标键 → 落盘：段序/键序/首行注释均得以保留。
        // 若走 LoadFile，主题文件的键会被展开进主文件，污染 [Theme] Name= 声明本身。
        ConfigParser disk;
        if (!disk.LoadFileRaw(target))
        {
            LogWarningF(L"!WriteKeyValue 目标文件不可读，已跳过落盘：%s", target.c_str());
            return;
        }
        disk.SetValue(section, key, value);
        if (!disk.SaveFile(target))
        {
            LogWarningF(L"!WriteKeyValue 落盘失败：%s", target.c_str());
        }
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

    RegisterBang(L"CommandMeasure", [](const std::wstring& args, Skin* skin) {
        if (!skin) return;
        // 语法：!CommandMeasure "MeasureName" "Command"。
        // 引号内允许空格，故优先按引号切分；无引号时退回按空白切分（兼容裸写）。
        const auto SplitFirst = [](const std::wstring& s, std::wstring& head,
                                   std::wstring& tail) -> bool {
            const std::wstring t = Trim(s);
            if (t.empty()) return false;
            if (t.front() == L'"') {
                const auto close = t.find(L'"', 1);
                if (close == std::wstring::npos) return false;
                head = t.substr(1, close - 1);
                tail = Trim(t.substr(close + 1));
                return true;
            }
            const auto sp = t.find_first_of(L" \t");
            head = (sp == std::wstring::npos) ? t : t.substr(0, sp);
            tail = (sp == std::wstring::npos) ? L"" : Trim(t.substr(sp + 1));
            return true;
        };

        std::wstring name;
        std::wstring command;
        if (!SplitFirst(args, name, command)) return;
        command = Unquote(command);
        if (name.empty() || command.empty()) return;

        // 段名大小写不敏感（与 INI 段名口径一致，!CommandMeasure MeasureX 等价 measurex）。
        for (const auto& measure : skin->GetMeasures()) {
            if (measure && StringUtil::EqualsIgnoreCase(measure->GetName(), name)) {
                measure->Command(command);
                return;
            }
        }
        // 目标 Measure 不存在：静默忽略（与未知 Bang 的处理一致）。
    });

    RegisterBang(L"Quit", [](const std::wstring&, Skin*) {
        ::PostQuitMessage(0);
    });
}

CommandHandler::~CommandHandler() = default;

void CommandHandler::RegisterBang(const std::wstring& name, BangFn fn)
{
    m_Bangs[CanonicalBangName(name)] = std::move(fn);
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
        auto it = m_Bangs.find(CanonicalBangName(name));
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
