/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * DockWindow 实现。事件流详见 docs/DOCK_DESIGN.md §7.2 / §7.5：
 *   鼠标移动 → DockBar::OnMouseMove  → [ON] 起 60ms 动画表
 *   WM_TIMER → DockBar::TickAnimation → 把 DockBar 的矩形同步给 Duilib 控件并重绘
 *   mouseleave / click → DockBar::OnMouseLeave / OnItemClick
 */
#include "DockWindow.h"

#include "PathUtil.h"   // GetFolderFromFilePath：拖放加项的图标落盘目录（D15）

#include <shellapi.h>   // DragAcceptFiles / DragQueryFileW / DragFinish（D15）

#ifdef RAINDOCK_USE_DUILIB
// 仅允许在 .cpp 顶部 using（§4.4 尾注）；头文件一律写 DuiLib:: 限定。
using namespace DuiLib;
#endif

namespace raindock {

#ifdef RAINDOCK_USE_DUILIB

namespace {

// 60ms 动画表：与 §7.1 的一阶逼近配合，约 3 帧到位。
constexpr UINT_PTR kAnimTimerId        = 1;
constexpr UINT     kAnimTimerInterval  = 60;
// 运行态低频表：与动画表分离，避免每帧枚举窗口（§7.3）。
constexpr UINT_PTR kAppTimerId         = 2;
constexpr UINT     kAppTimerInterval   = 1000;

// 自动隐藏时保留的触发带宽度（像素），见 §7.2-3。
constexpr int kAutoHideBand = 2;

// 由 item id 生成控件名，与 GetSkinFile() 里写入的 name 保持一致。
CDuiString ItemControlName(const std::wstring& id)
{
    CDuiString name;
    name.Format(_T("item:%s"), id.c_str());
    return name;
}

// DockBar 是唯一布局真源：每帧把它的矩形灌进 Duilib 控件（含 fixed 尺寸，
// 因为 float 容器重排时用 fixed 值还原位置）。
void SyncItemRects(CPaintManagerUI& pm, const DockBar& bar)
{
    const std::vector<DockItem>& items = bar.GetItems();
    const std::vector<RECT>&     rects = bar.GetItemRects();
    const size_t count = (items.size() < rects.size()) ? items.size() : rects.size();

    for (size_t i = 0; i < count; ++i)
    {
        CControlUI* pControl = pm.FindControl(ItemControlName(items[i].id).GetData());
        if (pControl == nullptr) continue;

        const RECT& rc = rects[i];
        SIZE xy = { rc.left, rc.top };
        pControl->SetFixedXY(xy);
        pControl->SetFixedWidth(rc.right - rc.left);
        pControl->SetFixedHeight(rc.bottom - rc.top);
        pControl->SetPos(rc, false);
    }
}

// 把单个 DockItem 的元数据（图标、名称）绑定到已建好的图标控件上。
void BindItemControl(CPaintManagerUI& pm, const DockItem& item)
{
    CControlUI* pControl = pm.FindControl(ItemControlName(item.id).GetData());
    if (pControl == nullptr) return;

    if (!item.iconPath.empty()) pControl->SetBkImage(item.iconPath.c_str());
    if (!item.name.empty())     pControl->SetToolTip(item.name.c_str());
}

// 全量绑定（XML 建树后的首次绑定，InitWindow 调用）。
void BindItems(CPaintManagerUI& pm, const DockBar& bar)
{
    for (const DockItem& item : bar.GetItems()) BindItemControl(pm, item);
}

// 拖放加项：为新增项增量建 Button 控件（§7.6.6 方案②「DOM 增量」）。
// 属性与 GetSkinFile() 的 XML 模板逐字段对齐，二者必须保持等价。
// 返回 true 表示根容器存在（已尽力建项）；单个 id 未命中则跳过。
bool AddItemControls(CPaintManagerUI& pm, DockBar& bar, const std::vector<std::wstring>& ids)
{
    CControlUI* pRoot = pm.FindControl(_T("dock_root"));
    if (pRoot == nullptr) return false;

    auto* pContainer = static_cast<CContainerUI*>(pRoot);
    for (const std::wstring& id : ids)
    {
        DockItem* item = bar.FindItem(id);
        if (item == nullptr) continue;
        // 幂等：同 id 控件已存在（重复拖放被去重挡掉，此处仅防御）则不重复建。
        if (pm.FindControl(ItemControlName(id).GetData()) != nullptr) continue;

        auto* pButton = new CButtonUI();
        pButton->SetName(ItemControlName(id).GetData());
        pButton->SetRichEvent(true);   // mouseenter/mouseleave 通知的前提
        pButton->SetFloat(true);
        pContainer->Add(pButton);      // Add 内部即 InitControls（UIContainer.cpp:92）
        BindItemControl(pm, *item);
    }
    return true;
}

// 按 Position 把窗口贴到工作区边缘并居中；自动隐藏时移出可见区（§7.2-3）。
void PlaceDockWindow(HWND hWnd, const DockBar& bar)
{
    RECT wnd = { 0 };
    if (!::GetWindowRect(hWnd, &wnd)) return;
    const int cx = wnd.right - wnd.left;
    const int cy = wnd.bottom - wnd.top;

    RECT work = { 0 };
    if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0))
    {
        work.left   = 0;
        work.top    = 0;
        work.right  = ::GetSystemMetrics(SM_CXSCREEN);
        work.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    }

    const bool autoHide = bar.GetAutoHide();
    int x = work.left + ((work.right - work.left) - cx) / 2;
    int y = work.top + ((work.bottom - work.top) - cy) / 2;

    switch (bar.GetPosition())
    {
    case DockPosition::Top:
        y = work.top - (autoHide ? (cy - kAutoHideBand) : 0);
        break;
    case DockPosition::Bottom:
        y = work.bottom - cy + (autoHide ? (cy - kAutoHideBand) : 0);
        break;
    case DockPosition::Left:
        x = work.left - (autoHide ? (cx - kAutoHideBand) : 0);
        break;
    case DockPosition::Right:
        x = work.right - cx + (autoHide ? (cx - kAutoHideBand) : 0);
        break;
    }

    ::SetWindowPos(hWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace

#endif  // RAINDOCK_USE_DUILIB

// ---------------------------------------------------------------------------
// 生命周期（两条线共用）
// ---------------------------------------------------------------------------

DockWindow::DockWindow() = default;

DockWindow::~DockWindow() = default;

bool DockWindow::Start(const std::wstring& iniPath)
{
    m_IniPath = iniPath;
    // 配置缺失不致命：DockBar 保持默认值继续运行（§7.4）。
    m_DockBar.LoadConfig(iniPath);

#ifdef RAINDOCK_USE_DUILIB
    // WS_EX_TOOLWINDOW：不进 Alt-Tab；WS_EX_LAYERED：逐像素 alpha（§7.2-1）。
    // WS_EX_NOACTIVATE：创建/点击都不激活窗口，避免抢焦点（§7.2-4）；
    // XML 里的 noactivate="true" 进一步抑制 CPaintManagerUI 的 SetFocus。
    HWND hWnd = Create(nullptr, _T("RainDeskPlus Dock"), UI_WNDSTYLE_FRAME,
                       WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
    if (hWnd == nullptr) return false;

    PlaceDockWindow(m_hWnd, m_DockBar);
    // bTakeFocus = false：SW_SHOWNOACTIVATE，避免抢焦点（§7.2-4）。
    ShowWindow(true, false);

    // 注册接受文件拖放：使窗口能收到 WM_DROPFILES（§7.6.1）。
    ::DragAcceptFiles(m_hWnd, TRUE);

    m_DockBar.ShowRunningApps();
    ::SetTimer(m_hWnd, kAppTimerId, kAppTimerInterval, nullptr);
#endif

    return true;
}

void DockWindow::Stop()
{
#ifdef RAINDOCK_USE_DUILIB
    if (m_hWnd != nullptr && ::IsWindow(m_hWnd))
    {
        if (m_AnimTimer != 0)
        {
            ::KillTimer(m_hWnd, m_AnimTimer);
            m_AnimTimer = 0;
        }
        ::KillTimer(m_hWnd, kAppTimerId);

        // DuiLib 的 CWindowWnd::Close() 走的是 PostMessage(WM_CLOSE)：窗口销毁发生在
        // 之后的某个消息循环里，而调用方紧接着就会 CPaintManagerUI::Term() 释放
        // ImageHash/CustomFonts/StyleHash 等共享资源。于是队列里残留的 WM_CLOSE 会被
        // 派发到「共享资源已释放」的窗口上，OnFinalMessage → m_pm.ReapObjects() 触发
        // 0xC0000005（访问已释放资源）。此处改为同步关闭：Stop() 返回时窗口必已销毁、
        // OnFinalMessage 已在 DuiLib 仍有效的状态下执行完毕，DestroyWindow 也会顺带
        // 丢弃队列中该窗口的残留消息。
        ::SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }
#endif

    if (!m_IniPath.empty()) m_DockBar.SaveConfig(m_IniPath);
}

// ---------------------------------------------------------------------------
// 平台无关转发入口
// ---------------------------------------------------------------------------

void DockWindow::HandleMouseMove(const POINT& pt)
{
    m_DockBar.OnMouseMove(pt);

#ifdef RAINDOCK_USE_DUILIB
    // 鼠标进入后启动动画表；收敛时由 HandleMessage 停表（§7.1）。
    if (m_hWnd != nullptr && m_AnimTimer == 0)
    {
        m_AnimTimer = ::SetTimer(m_hWnd, kAnimTimerId, kAnimTimerInterval, nullptr);
    }
#endif
}

void DockWindow::HandleMouseLeave()
{
    m_DockBar.OnMouseLeave();

#ifdef RAINDOCK_USE_DUILIB
    // 离开只复位 target：cur 仍需动画表平滑衰减回基准，故此处不停表，
    // 由 HandleMessage 在 TickAnimation 收敛后停表（§7.1「平滑过渡」+「静止时 CPU 为 0」）。
    if (m_hWnd != nullptr && m_AnimTimer == 0)
    {
        m_AnimTimer = ::SetTimer(m_hWnd, kAnimTimerId, kAnimTimerInterval, nullptr);
    }
    if (m_pm.GetPaintWindow() != nullptr)
    {
        SyncItemRects(m_pm, m_DockBar);
        m_pm.Invalidate();
    }
#endif
}

void DockWindow::HandleItemClick(const std::wstring& id)
{
    m_DockBar.OnItemClick(id);
}

size_t DockWindow::HandleFilesDropped(const std::vector<std::wstring>& paths)
{
    if (paths.empty()) return 0;

    // 图标落盘目录 == Dock.ini 所在目录；m_IniPath 为空时传空串，
    // 由 AddItemFromPath 的入参守卫挡掉（§7.6.7「裸测试不落库」）。
    const std::wstring dockFolder =
        m_IniPath.empty() ? std::wstring() : PathUtil::GetFolderFromFilePath(m_IniPath);

    size_t added = 0;
#ifdef RAINDOCK_USE_DUILIB
    std::vector<std::wstring> addedIds;
#endif

    for (const std::wstring& path : paths)
    {
        const std::wstring id = m_DockBar.AddItemFromPath(path, dockFolder);
        if (id.empty()) continue;   // 非文件 / 无法规范化 / 同 targetPath 已存在

        ++added;
#ifdef RAINDOCK_USE_DUILIB
        addedIds.push_back(id);
#endif
    }

    if (added == 0) return 0;

#ifdef RAINDOCK_USE_DUILIB
    // §7.6.6 刷新序列：补控件 → 同步矩形 → 窗口自适应 → 重贴边 → 重绘。
    if (m_pm.GetPaintWindow() != nullptr && AddItemControls(m_pm, m_DockBar, addedIds))
    {
        SyncItemRects(m_pm, m_DockBar);

        const SIZE size = m_DockBar.GetPreferredSize();
        if (size.cx > 0 && size.cy > 0)
        {
            ::SetWindowPos(m_hWnd, nullptr, 0, 0, size.cx, size.cy,
                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            // 与 GetSkinFile() 的 size 属性同源，防下次 ReloadSkin 回退（§7.6.6）。
            m_pm.SetInitSize(size.cx, size.cy);
        }
        PlaceDockWindow(m_hWnd, m_DockBar);
        m_pm.Invalidate();
    }
#endif

    // 拖放是用户显式操作，立刻持久化，不等退出（§7.6.1）。
    if (!m_IniPath.empty()) m_DockBar.SaveConfig(m_IniPath);

    return added;
}

// ---------------------------------------------------------------------------
// Duilib 分支实现
// ---------------------------------------------------------------------------

#ifdef RAINDOCK_USE_DUILIB

CDuiString DockWindow::GetSkinFile()
{
    const SIZE size = m_DockBar.GetPreferredSize();
    const int cx = (size.cx > 0) ? size.cx : 64;
    const int cy = (size.cy > 0) ? size.cy : 64;
    const int alpha = m_DockBar.GetTransparency();

    // 内联 XML：根节点 Window 承载窗口级属性（分层透明 / 不激活），
    // 第一个子节点即 m_pm 的根控件。图标按钮由模型数据生成，name 前缀即路由键。
    // 面板底色来自主题（DockBar::GetPanelColor()，#AARRGGBB），Dock.ini / Themes\<名>.ini 均可覆盖。
    CDuiString xml;
    xml.Format(_T("<Window size=\"%d,%d\" bkcolor=\"%s\" ")
               _T("layered=\"true\" layeredopacity=\"%d\" noactivate=\"true\">")
               _T("  <VerticalLayout name=\"dock_root\" bkcolor=\"#00000000\">"),
               cx, cy, m_DockBar.GetPanelColor().c_str(), alpha);

    for (const DockItem& item : m_DockBar.GetItems())
    {
        if (item.id.empty()) continue;
        // richevent="true" 是 mouseenter/mouseleave 通知的前提（CButtonUI 默认不发）。
        CDuiString node;
        node.Format(_T("    <Button name=\"item:%s\" richevent=\"true\" float=\"true\" />"),
                    item.id.c_str());
        xml += node;
    }

    xml += _T("  </VerticalLayout></Window>");
    return xml;
}

LPCTSTR DockWindow::GetWindowClassName() const
{
    return _T("RainDeskPlusDockWnd");
}

void DockWindow::InitWindow()
{
    BindItems(m_pm, m_DockBar);
    SyncItemRects(m_pm, m_DockBar);

    if (m_AnimTimer == 0)
    {
        m_AnimTimer = ::SetTimer(m_hWnd, kAnimTimerId, kAnimTimerInterval, nullptr);
    }
}

void DockWindow::Notify(TNotifyUI& msg)
{
    if (msg.pSender != nullptr)
    {
        if (msg.sType == DUI_MSGTYPE_CLICK)
        {
            const CDuiString name = msg.pSender->GetName();
            if (name.Left(5) == _T("item:"))
            {
                HandleItemClick(std::wstring(name.GetData() + 5));
            }
        }
        else if (msg.sType == DUI_MSGTYPE_MOUSELEAVE)
        {
            HandleMouseLeave();
        }
    }

    WindowImplBase::Notify(msg);
}

LRESULT DockWindow::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    // WM_MOUSEMOVE 的 lParam 是客户区坐标（有符号 16 位打包）。
    POINT pt = { static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
                 static_cast<LONG>(static_cast<short>(HIWORD(lParam))) };
    m_LastMouse = pt;
    HandleMouseMove(pt);

    // 交回基类链，让 CPaintManagerUI 继续处理控件 hover 事件（§7.5）。
    bHandled = FALSE;
    return 0;
}

LRESULT DockWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // 拖放取路径是纯 ON 线适配：DragQueryFileW 循环 → 交给平台无关的业务入口，
    // 业务入口内部再决定是否补控件 / 落库（§7.6.1 / DD-13）。
    if (uMsg == WM_DROPFILES)
    {
        auto* hDrop = reinterpret_cast<HDROP>(wParam);
        const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            const UINT length = ::DragQueryFileW(hDrop, i, nullptr, 0);
            if (length == 0) continue;

            std::wstring path(length + 1, L'\0');   // +1 容纳结尾 NUL
            const UINT copied = ::DragQueryFileW(hDrop, i, &path[0], length + 1);
            path.resize(copied);
            paths.push_back(path);
        }
        ::DragFinish(hDrop);

        HandleFilesDropped(paths);
        return 0;
    }

    // WindowImplBase::HandleMessage 没有 WM_TIMER 分支，动画 tick 必须在此拦截。
    if (uMsg == WM_TIMER)
    {
        if (wParam == kAnimTimerId)
        {
            if (m_DockBar.TickAnimation())
            {
                SyncItemRects(m_pm, m_DockBar);
                m_pm.Invalidate();
            }
            else
            {
                // 已收敛（悬停到位或已衰减回基准）：停表，静止时 CPU 占用为 0（§7.1）。
                ::KillTimer(m_hWnd, kAnimTimerId);
                m_AnimTimer = 0;
            }
            return 0;
        }
        if (wParam == kAppTimerId)
        {
            m_DockBar.ShowRunningApps();
            return 0;
        }
    }

    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

void DockWindow::OnFinalMessage(HWND hWnd)
{
    if (m_AnimTimer != 0)
    {
        ::KillTimer(hWnd, m_AnimTimer);
        m_AnimTimer = 0;
    }
    ::KillTimer(hWnd, kAppTimerId);

    // 基类负责移除 notifier / 过滤器和回收控件树；这里绝不能 delete this。
    WindowImplBase::OnFinalMessage(hWnd);
}

#endif  // RAINDOCK_USE_DUILIB

}  // namespace raindock
