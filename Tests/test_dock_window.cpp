/*
 * RainDeskPlus - D13-14 D3 Duilib 透明/异形 Dock 窗口 Smoke 测试
 * 对应 docs/DOCK_DESIGN.md §9：
 *   D3  透明/异形窗口在 Duilib 下可见、WS_EX_TOOLWINDOW 不在 Alt-Tab、不抢焦点。
 * 载体要求：仿 test_duilib_hello.cpp 的「有界消息泵」，只在 RAINDOCK_USE_DUILIB=ON 时构建。
 *
 * 断言依据（详见 docs/DOCK_DESIGN.md §7.2 / §7.5 / §5.3）：
 *   1. 分层透明：WS_EX_LAYERED + XML layered="true" → 逐像素 alpha；
 *   2. 不进 Alt-Tab：WS_EX_TOOLWINDOW；
 *   3. 不抢焦点：WS_EX_NOACTIVATE + XML noactivate="true" + ShowWindow(SW_SHOWNOACTIVATE)；
 *   4. 尺寸契约：XML 根节点 size 来自 DockBar::GetPreferredSize()，窗口创建后即等于该值；
 *   5. 交互链路：HandleMouseMove → 60ms 动画表 → WM_TIMER 推进 → 图标放大/回落。
 *
 * 无交互：所有断言在约 3 秒内自动完成并主动销毁窗口。
 */
#include "DockWindow.h"   // 经 include_directories(Dock) 可达；已含 UIlib.h（宏开启时）

#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace DuiLib;

static bool g_Pass = true;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s\n", msg); \
        g_Pass = false; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while (0)

// 有界消息泵：避免 CPaintManagerUI::MessageLoop() 永久阻塞（同 D5）。
static void PumpMessages(DWORD dwMilliseconds)
{
    const DWORD dwStart = ::GetTickCount();
    MSG msg = {0};
    while (::GetTickCount() - dwStart < dwMilliseconds)
    {
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        ::Sleep(10);
    }
}

// 与 test_dock_core.cpp 同构的测项构造。
static raindock::DockItem MakeItem(const wchar_t* id)
{
    raindock::DockItem item;
    item.id   = id;
    item.name = id;
    return item;
}

// 配置文件写在测试程序所在目录（构建输出目录，非 C 盘）：
// Stop() 会无条件 SaveConfig()，因此该路径必须可写。
static std::wstring MakeIniPath()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);

    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path.resize(pos + 1);
    return path + L"RainDeskPlusDockTest.ini";
}

int main()
{
    printf("=== D13-14 D3 Duilib Dock Window Smoke Test ===\n\n");

    CPaintManagerUI::SetInstance(::GetModuleHandle(nullptr));

    raindock::DockWindow wnd;
    raindock::DockBar&  bar = wnd.GetDockBar();
    bar.SetIconSize(48);
    bar.SetSpacing(8);
    bar.AddItem(MakeItem(L"probe0"));
    bar.AddItem(MakeItem(L"probe1"));

    // 保证从零配置开始：上次运行残留的 Dock.ini 会改变 Position/IconSize 等期望值。
    const std::wstring ini = MakeIniPath();
    ::DeleteFileW(ini.c_str());

    const SIZE expected = bar.GetPreferredSize();
    printf("  [INFO] 期望窗口尺寸 = %ld x %ld\n",
           static_cast<long>(expected.cx), static_cast<long>(expected.cy));

    CHECK(wnd.Start(ini), "DockWindow::Start 成功（配置缺失不致命，§7.4）");

    // 关键：__WndProc 在 WM_NCDESTROY 时会把窗口句柄置空，必须先缓存。
    HWND hWnd = wnd.GetHWND();
    CHECK(hWnd != nullptr && ::IsWindow(hWnd), "Dock 窗口创建成功");

    if (hWnd == nullptr)
    {
        printf("\n[ABORT] 窗口创建失败，后续断言无法进行\n");
        CPaintManagerUI::Term();
        return 1;
    }

    // -----------------------------------------------------------------------
    printf("\n1. D3 窗口样式：可见 / 分层透明 / 不进 Alt-Tab / 不抢焦点\n");
    {
        PumpMessages(800);

        CHECK(::IsWindowVisible(hWnd), "透明/分层窗口在消息泵期间保持可见（§7.2-1）");

        const LONG_PTR ex = ::GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        CHECK((ex & WS_EX_LAYERED) != 0, "WS_EX_LAYERED 已启用（逐像素 alpha）");
        CHECK((ex & WS_EX_TOOLWINDOW) != 0, "WS_EX_TOOLWINDOW 已启用（不进 Alt-Tab，§7.2-2）");
        CHECK((ex & WS_EX_TOPMOST) != 0, "WS_EX_TOPMOST 已启用（始终置顶，§7.2-1）");

        CHECK(::GetForegroundWindow() != hWnd, "不抢前台焦点（§7.2-4）");
    }

    // -----------------------------------------------------------------------
    printf("\n2. D3 尺寸契约：窗口尺寸 == DockBar::GetPreferredSize()（§5.3）\n");
    {
        RECT wr = {0};
        ::GetWindowRect(hWnd, &wr);
        const long cx = wr.right - wr.left;
        const long cy = wr.bottom - wr.top;
        printf("  [INFO] 实际窗口尺寸 = %ld x %ld\n", cx, cy);

        CHECK(cx == expected.cx && cy == expected.cy,
              "XML 根节点 size 由 DockBar 布局驱动并落到真实窗口");
    }

    // -----------------------------------------------------------------------
    printf("\n3. §7.5 交互链路：HandleMouseMove → 动画表 → WM_TIMER 推进 → 放大\n");
    {
        const std::vector<RECT>& rects = bar.GetItemRects();
        CHECK(rects.size() == bar.GetItems().size(), "布局矩形与项数等长同序");

        if (rects.size() == 2)
        {
            POINT hover = { (rects[1].left + rects[1].right) / 2,
                            (rects[1].top + rects[1].bottom) / 2 };
            printf("  [INFO] 悬停点（客户区）= (%ld, %ld)\n",
                   static_cast<long>(hover.x), static_cast<long>(hover.y));

            wnd.HandleMouseMove(hover);
            PumpMessages(700);

            const float scale = bar.GetItemScale(1);
            printf("  [INFO] 悬停 700ms 后 item1 缩放 = %.3f\n",
                   static_cast<double>(scale));
            CHECK(scale > 1.0f, "鼠标悬停后目标项被放大（动画表已推进）");
            CHECK(scale <= bar.GetMagnify().GetMaxScale() + 1e-3f,
                  "缩放不超过 MaxScale（§7.1 上限）");

            wnd.HandleMouseLeave();
            // 衰减按一阶逼近收敛 30 帧左右（60ms/帧），泵满 2.4s 覆盖整个回落过程。
            PumpMessages(2400);
            const float back = bar.GetItemScale(1);
            printf("  [INFO] 移出 2400ms 后 item1 缩放 = %.3f\n",
                   static_cast<double>(back));
            CHECK(back <= 1.0f + 1e-3f, "移出后动画回落至基准缩放（§7.1 衰减）");
        }
    }

    // -----------------------------------------------------------------------
    printf("\n4. 生命周期：Stop() 销毁窗口并回写配置（§7.4）\n");
    {
        wnd.Stop();
        PumpMessages(800);

        CHECK(!::IsWindow(hWnd), "Stop() 后窗口已正常销毁（无残留）");
        CHECK(::GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES,
              "Stop() 已回写 Dock 配置到 ini");
    }

    CPaintManagerUI::Term();

    printf("\n%s\n", g_Pass ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_Pass ? 0 : 1;
}
