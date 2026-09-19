/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * Dock 容器：项管理 + 布局（悬停放大的每帧重排）+ 动画中间量 + 配置持久化。
 * 详见 docs/DOCK_DESIGN.md §2.2 / §4.3 / §7.1 / §7.3 / §7.4。
 *
 * 本类**纯逻辑**，不依赖 Duilib、不创建窗口（DD-1）：
 * 由 DockWindow 转发鼠标/点击事件，并按 TickAnimation() 的返回值决定是否重绘。
 */
#pragma once

#include "DockItem.h"
#include "DockMagnify.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace raindock {

enum class DockPosition { Top, Bottom, Left, Right };

class DockBar
{
private:
    // 每项的「视觉状态」。与 DockItem 分离：DockItem 是配置数据（落库），
    // ItemVisual 是每帧中间量（不落库）。两者**等长同序**。
    struct ItemVisual
    {
        RECT  rect{};         // 当前布局矩形（Bar 轴坐标：水平 Dock 即客户区坐标）
        float cur    = 1.0f;  // 当前缩放（动画中间值）
        float target = 1.0f;  // 目标缩放（由 DockMagnify 解算）
    };

public:
    DockBar();
    ~DockBar() = default;

    // ---------- 项管理 ----------
    void AddItem(const DockItem& item);      // 同 id 替换，否则追加（并同步 ItemVisual）
    void RemoveItem(const std::wstring& id);
    void ClearItems();
    const std::vector<DockItem>& GetItems() const;
    DockItem* FindItem(const std::wstring& id);          // 未命中返回 nullptr

    // ---------- 拖放加项（D15，§7.6.2） ----------
    // 成功返回新项 id；路径不合法 / 已存在同 targetPath 时返回空串。
    // dockFolder 决定图标落盘位置（<dockFolder>Icons\<id>.png）。
    std::wstring AddItemFromPath(const std::wstring& path, const std::wstring& dockFolder);
    bool         HasTarget(const std::wstring& targetPath) const;   // 去重判据（规范化后比较）

    // ---------- 布局（DD-6：只读出口，供渲染与单测） ----------
    const std::vector<RECT>& GetItemRects() const;       // 与 GetItems() 同序等长
    float GetItemScale(size_t index) const;              // 当前缩放（动画值）
    RECT  GetBarRect() const;                            // Dock 窗口应占矩形（客户区）
    SIZE  GetPreferredSize() const;                      // 供 DockWindow/自动隐藏算位移

    // ---------- 交互（UI 无关入口；DockWindow 转发） ----------
    void OnMouseMove(const POINT& pt);       // 更新 target + ComputeLayout
    void OnMouseLeave();                     // 全部 target=1.0
    void OnItemClick(const std::wstring& id);  // 启动 / 前置 / 最小化
    bool TickAnimation();                    // 推进 cur→target；有变化返回 true（DD-5）

    // ---------- 配置 ----------
    void SetIconSize(int size);              // clamp 16..128
    void SetTransparency(int alpha);         // clamp 0..255
    void SetPosition(DockPosition pos);
    void SetAutoHide(bool enable);
    void SetSpacing(int px);                 // 图标间距，默认 8

    int          GetIconSize()     const;
    int          GetTransparency() const;
    DockPosition GetPosition()     const;
    bool         GetAutoHide()     const;
    int          GetSpacing()      const;
    const DockMagnify& GetMagnify() const;

    // ---------- 主题（D26-30：与挂件共用 Themes\<名>.ini）----------
    // PanelColor 为 Duilib bkcolor 口径的 #AARRGGBB；ThemeName 空表示未启用主题。
    void SetPanelColor(const std::wstring& color);
    void SetThemeName(const std::wstring& name);
    const std::wstring& GetPanelColor() const;
    const std::wstring& GetThemeName()  const;

    // ---------- 配置持久化（DD-7：复用 Library/ConfigParser） ----------
    bool LoadConfig(const std::wstring& iniPath);    // Dock.ini + 同目录 items.ini
    bool SaveConfig(const std::wstring& iniPath) const;

    // ---------- 任务管理 ----------
    void ShowRunningApps();                  // 枚举顶层窗口 → 刷新 isRunning/hWnd
    void MinimizeToDock(HWND hWnd);

private:
    void  ComputeLayout();                   // 依据 iconSize/spacing/position/target 重排 rect
    POINT ToBarAxis(const POINT& pt) const;  // 竖直 Dock 时交换 x/y，统一布局为一维
    RECT  BaseRectOf(size_t index) const;    // iconSize 下的静态格位（未放大基准矩形）

    std::vector<DockItem>   m_Items;
    std::vector<ItemVisual> m_Visuals;       // 与 m_Items 等长同序（不变量）
    // GetItemRects() 的只读投影：ItemVisual 是私有嵌套类型，无法把裸 RECT 数组
    // 直接以 const std::vector<RECT>& 暴露，故由 ComputeLayout() 顺带维护一份同序副本。
    std::vector<RECT>       m_RectCache;
    int          m_IconSize     = 48;
    int          m_Transparency = 220;
    int          m_Spacing      = 8;
    DockPosition m_Position     = DockPosition::Bottom;
    bool         m_AutoHide     = false;
    DockMagnify  m_Magnify;
    // 主题：面板底色（#AARRGGBB，默认与旧硬编码 #40000000 一致）+ 主题名（[Theme] Name=）。
    std::wstring m_PanelColor   = L"#40000000";
    std::wstring m_ThemeName;
    POINT        m_LastMouse    = {0, 0};
    bool         m_MouseInside  = false;
};

}  // namespace raindock
