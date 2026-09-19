/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 图标提取：把拖入的文件/快捷方式的图标统一转成 PNG。
 * platform-only（Win32 + WIC），对 Duilib 零依赖——DockBar 必须保持纯逻辑（DD-1/DD-14）。
 * 详见 docs/DOCK_DESIGN.md §7.6.4。
 */
#pragma once

#include <string>

namespace raindock {

class IconExtractor
{
public:
    // 从 path 提取图标并按 PNG 落盘到 outPngPath（其所在目录不存在时会创建）。
    //   - .png/.jpg/.jpeg/.bmp/.gif：文件本身就是图标，直接复制（不做 shell 提取）；
    //   - .ico：LoadImageW；
    //   - 其余（.exe/.lnk/…）：shell 图标（jumbo 256 → 缩放），失败回退大图标。
    // 任一步失败返回 false（调用方据此留空 iconPath，但项照常加入）。
    static bool ExtractToPng(const std::wstring& path, const std::wstring& outPngPath);
};

}  // namespace raindock
