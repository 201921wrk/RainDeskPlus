/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2，不使用 Nexus 专有源码）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 单个 Dock 图标项。详见 docs/DOCK_DESIGN.md。
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // HWND

#include <string>

namespace raindock {

struct DockItem
{
    std::wstring id;          // 唯一标识
    std::wstring name;        // 显示名
    std::wstring iconPath;    // 图标路径（PNG/ICO）
    std::wstring targetPath;  // 目标程序路径
    std::wstring workingDir;  // 工作目录
    std::wstring arguments;   // 启动参数
    bool         isRunning = false;
    HWND         hWnd = nullptr;   // 运行中程序的关联窗口
};

}  // namespace raindock
