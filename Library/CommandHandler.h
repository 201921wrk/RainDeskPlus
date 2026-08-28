/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/CommandHandler.h（Bang 命令解析与执行，直接复用）。
 */
#ifndef RAINDOCK_LIBRARY_COMMANDHANDLER_H_
#define RAINDOCK_LIBRARY_COMMANDHANDLER_H_

#include <functional>
#include <string>
#include <unordered_map>

namespace raindock {

class Skin;

class CommandHandler
{
public:
    CommandHandler();
    ~CommandHandler();

    // 解析并执行 Bang 命令，例如 "!ActivateConfig ConfigName Variant"。
    void Execute(const std::wstring& command, Skin* skin);

private:
    using BangFn = std::function<void(const std::wstring& args, Skin* skin)>;
    void RegisterBang(const std::wstring& name, BangFn fn);

    std::unordered_map<std::wstring, BangFn> m_Bangs;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_COMMANDHANDLER_H_
