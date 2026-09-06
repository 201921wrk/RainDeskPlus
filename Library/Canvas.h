/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Canvas.h（渲染上下文）。本项目按 MODULE_EXTRACTION.md §3
 * 以 Direct2D/DirectWrite 取代上游 GDI+ 渲染栈。
 *
 * M3 范围：
 *   - 共享工厂单例：ID2D1Factory / IDWriteFactory / IWICImagingFactory
 *   - 离屏 WIC 软件渲染目标（Smoke 验证 / 单测用）
 *   - WIC 位图 → PNG 落盘（人工核验渲染结果）
 * M4 将扩展：HWND/DC 窗口渲染目标、皮肤级画布生命周期。
 */
#pragma once

#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>    // CoCreateInstance（WIC 工厂）；WIN32_LEAN_AND_MEAN 不自动引入
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

namespace raindock {

class Canvas
{
public:
    // 共享工厂（进程级单例，首次调用时创建，进程退出由 FinalizeAll 释放）。
    static ID2D1Factory*       GetD2DFactory();
    static IDWriteFactory*     GetWriteFactory();
    static IWICImagingFactory* GetWICFactory();

    // 离屏 WIC 软件渲染目标（DPI 固定 96，1 DIP = 1 px）。
    // 返回的 rt / bitmap 由调用方 Release()。
    static bool CreateOffscreenTarget(uint32_t width, uint32_t height,
                                      ID2D1RenderTarget** outRT,
                                      IWICBitmap** outBitmap);

    // WIC 位图 → PNG 落盘（32bppPBGRA 原生兼容 PNG）。
    static bool SaveBitmapToPng(IWICBitmap* bitmap, const std::wstring& path);

    // 进程退出时释放共享工厂（幂等）。
    static void FinalizeAll();
};

}  // namespace raindock
