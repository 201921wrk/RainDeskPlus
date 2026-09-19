/*
 * RainDeskPlus - D13-14 D1/D2/D4 DockBar Smoke 测试（纯逻辑，无 Duilib）
 * 对应 docs/DOCK_DESIGN.md §9：
 *   D1  AddItem 同 id 替换 / RemoveItem / ClearItems 正确，且项集合与布局矩形始终等长同序；
 *       OnItemClick（未运行项）能拉起目标程序。
 *   D2  DockMagnify 缩放曲线（dist=0 → MaxScale；dist >= radius → 1.0；区间内单调递减）；
 *       ComputeLayout 后相邻 rect 不重叠，总宽 = Σ(icon×scale) + (n-1)×spacing。
 *   D4  ShowRunningApps() 能枚举运行中程序并按 targetPath 命中，isRunning/hWnd 正确置位与复位。
 *
 * 无外部依赖：验证「启动」与「命中运行中程序」时，把本测试自身当作被启动的程序
 * （Shell 拉起带 --dock-probe-child 的自身副本，该副本创建同名可见顶层窗口）。
 */
#include "DockBar.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace raindock;

static bool g_Pass = true;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s\n", msg); \
        g_Pass = false; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while (0)

// ---------------------------------------------------------------------------
// 通用小工具
// ---------------------------------------------------------------------------

static bool NearlyEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

// ComputeLayout 的取整规则：scaled = (int)(icon * cur + 0.5f)，与实现保持一致。
static int ScaledWidth(int icon, float scale)
{
    return static_cast<int>(static_cast<float>(icon) * scale + 0.5f);
}

static int Count(const DockBar& bar)
{
    return static_cast<int>(bar.GetItems().size());
}

static int RectCount(const DockBar& bar)
{
    return static_cast<int>(bar.GetItemRects().size());
}

static DockItem MakeItem(const wchar_t* id, const wchar_t* target = L"")
{
    DockItem item;
    item.id         = id;
    item.name       = id;
    item.targetPath = target;
    return item;
}

static std::wstring GetSelfPath()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

static bool HasSwitch(const wchar_t* sw)
{
    const std::wstring cmd = ::GetCommandLineW();
    return cmd.find(sw) != std::wstring::npos;
}

// ---------------------------------------------------------------------------
// 探针窗口：既用于验证 OnItemClick 的启动路径（子进程），
// 也用于验证 ShowRunningApps 按进程路径命中（本进程）。
// ---------------------------------------------------------------------------

static const wchar_t* const kProbeClassName = L"RainDeskPlusDockProbeWnd";
static const DWORD kChildHoldMs      = 8000;   // 子进程最长存活（兜底）
static const DWORD kLaunchTimeoutMs  = 10000;  // 等待子进程窗口出现的上限
static const DWORD kShutdownTimeoutMs = 3000;  // 等待子进程窗口关闭的上限

static LRESULT CALLBACK ProbeWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_CLOSE)
    {
        ::DestroyWindow(hWnd);
        return 0;
    }
    return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// 每个进程有自己的窗口类表，重复注册失败（ERROR_CLASS_ALREADY_EXISTS）无害。
static void RegisterProbeClass()
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ProbeWndProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kProbeClassName;
    ::RegisterClassExW(&wc);
}

// 顶层窗口（无属主、无 WS_EX_TOOLWINDOW）——与 DockBar 枚举窗口的过滤条件一致。
static HWND CreateProbeWindow()
{
    RegisterProbeClass();

    HWND hWnd = ::CreateWindowExW(0, kProbeClassName, L"RainDeskPlus Dock Probe",
                                  WS_OVERLAPPEDWINDOW, 100, 100, 320, 200,
                                  nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (hWnd != nullptr)
    {
        ::ShowWindow(hWnd, SW_SHOW);
    }
    return hWnd;
}

// 子进程模式：建可见顶层窗口并泵消息，父进程关掉它即退出。
static int RunProbeChild()
{
    HWND hWnd = CreateProbeWindow();
    if (hWnd == nullptr) return 2;

    MSG msg = {};
    const DWORD start = ::GetTickCount();
    while (::IsWindow(hWnd) && (::GetTickCount() - start) < kChildHoldMs)
    {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(10);
    }
    return 0;
}

int main()
{
    if (HasSwitch(L"--dock-probe-child"))
    {
        return RunProbeChild();
    }

    printf("=== D13-14 D1/D2/D4 DockBar Smoke Test ===\n\n");

    // -----------------------------------------------------------------------
    printf("1. D1 项管理：追加 / 同 id 替换 / 删除 / 清空 + 等长同序不变量\n");
    {
        DockBar bar;
        CHECK(Count(bar) == 0, "初始无项");
        CHECK(RectCount(bar) == 0, "初始无布局矩形");

        bar.AddItem(MakeItem(L"a", L"a.exe"));
        bar.AddItem(MakeItem(L"b", L"b.exe"));
        bar.AddItem(MakeItem(L"c", L"c.exe"));
        CHECK(Count(bar) == 3, "AddItem 追加 3 项");
        CHECK(Count(bar) == RectCount(bar), "不变量：items 与 rects 等长");

        DockItem updated = MakeItem(L"b", L"b2.exe");
        updated.name = L"B2";
        bar.AddItem(updated);
        CHECK(Count(bar) == 3, "同 id AddItem 不新增项");
        CHECK(bar.GetItems()[1].targetPath == L"b2.exe", "同 id AddItem 替换 targetPath");
        CHECK(bar.GetItems()[1].name == L"B2", "同 id AddItem 替换 name");
        CHECK(Count(bar) == RectCount(bar), "不变量：替换后仍等长");

        CHECK(bar.FindItem(L"c") != nullptr, "FindItem 命中返回非空指针");
        CHECK(bar.FindItem(L"zzz") == nullptr, "FindItem 未命中返回 nullptr");

        bar.RemoveItem(L"a");
        CHECK(Count(bar) == 2, "RemoveItem 生效");
        CHECK(bar.GetItems()[0].id == L"b", "RemoveItem 后剩余项保持原顺序");
        CHECK(Count(bar) == RectCount(bar), "不变量：移除后仍等长");

        bar.RemoveItem(L"not-exist");
        CHECK(Count(bar) == 2, "RemoveItem 未命中不改变项数");

        bar.ClearItems();
        CHECK(Count(bar) == 0, "ClearItems 清空项");
        CHECK(RectCount(bar) == 0, "ClearItems 同步清空布局矩形");
    }

    // -----------------------------------------------------------------------
    printf("\n2. D2 未放大时的基准布局（cur == 1.0）\n");
    {
        DockBar bar;
        bar.SetIconSize(64);
        bar.SetSpacing(10);
        CHECK(bar.GetIconSize() == 64, "SetIconSize(64) 生效");
        CHECK(bar.GetSpacing() == 10, "SetSpacing(10) 生效");

        bar.AddItem(MakeItem(L"i0"));
        bar.AddItem(MakeItem(L"i1"));
        bar.AddItem(MakeItem(L"i2"));

        const std::vector<RECT>& r = bar.GetItemRects();
        CHECK(static_cast<int>(r.size()) == 3, "布局矩形数量与项数一致");
        CHECK(bar.GetBarRect().left == 0 && bar.GetBarRect().right == 212, "BarRect 右边界 = 总宽");
        CHECK(bar.GetPreferredSize().cx == 212, "首选宽度 = Σ(icon×1.0) + (n-1)×spacing");
        CHECK(bar.GetPreferredSize().cy == 64, "首选高度 = iconSize（水平 Dock）");

        for (size_t i = 0; i + 1 < r.size(); ++i)
        {
            CHECK(r[i].right + bar.GetSpacing() == r[i + 1].left,
                  "相邻 rect 不重叠且间距恰为 Spacing");
        }
        for (size_t i = 0; i < r.size(); ++i)
        {
            CHECK(r[i].bottom - r[i].top == 64, "正交方向厚度固定为 iconSize");
        }
    }

    // -----------------------------------------------------------------------
    printf("\n3. D2 悬停放大：target 解算 → TickAnimation 收敛 → 重排\n");
    {
        DockBar bar;
        bar.SetIconSize(64);
        bar.SetSpacing(10);
        bar.AddItem(MakeItem(L"i0"));
        bar.AddItem(MakeItem(L"i1"));
        bar.AddItem(MakeItem(L"i2"));

        // 项1 的基准格位是 [74,138)，中心 (106,32)：鼠标正落在其中心。
        const POINT hover = {106, 32};
        bar.OnMouseMove(hover);

        CHECK(NearlyEqual(bar.GetItemScale(1), 1.0f), "OnMouseMove 不直接改 cur（只解算 target）");
        CHECK(bar.GetItemRects()[1].right - bar.GetItemRects()[1].left == 64,
              "未推进动画时布局仍为基准宽度");

        int ticks = 0;
        while (bar.TickAnimation() && ticks < 500) ++ticks;
        printf("  [INFO] 收敛用了 %d 次 tick\n", ticks);
        CHECK(ticks > 0 && ticks < 500, "TickAnimation 在有限帧内收敛并返回 false");

        CHECK(NearlyEqual(bar.GetItemScale(1), 1.8f), "鼠标正下方项放大到 MaxScale(1.8)");
        const float side = 1.0f + 0.8f * (1.0f - 74.0f / 150.0f);
        CHECK(NearlyEqual(bar.GetItemScale(0), side), "相邻项按距离线性衰减（非满放大）");
        CHECK(bar.GetItemScale(0) < bar.GetItemScale(1), "距离越近缩放越大");
        CHECK(NearlyEqual(bar.GetItemScale(0), bar.GetItemScale(2)), "对称位置缩放相同");
        CHECK(!bar.TickAnimation(), "已收敛时 TickAnimation 返回 false（不触发无谓重绘）");

        const std::vector<RECT>& r = bar.GetItemRects();
        const int icon = bar.GetIconSize();
        const int w0 = ScaledWidth(icon, bar.GetItemScale(0));
        const int w1 = ScaledWidth(icon, bar.GetItemScale(1));
        const int w2 = ScaledWidth(icon, bar.GetItemScale(2));
        CHECK(r[0].right - r[0].left == w0, "rect 宽度 = round(icon × cur)（项0）");
        CHECK(r[1].right - r[1].left == w1, "rect 宽度 = round(icon × cur)（项1）");
        CHECK(r[1].right - r[1].left > 64, "放大项宽度大于基准宽度");

        for (size_t i = 0; i + 1 < r.size(); ++i)
        {
            CHECK(r[i].right + bar.GetSpacing() == r[i + 1].left,
                  "放大后相邻 rect 仍不重叠");
        }

        const long total  = r[2].right - r[0].left;
        const long expect = w0 + bar.GetSpacing() + w1 + bar.GetSpacing() + w2;
        CHECK(total == expect, "总宽 = Σ(icon×cur) + (n-1)×spacing");
        CHECK(bar.GetPreferredSize().cx == total, "GetPreferredSize 与 GetBarRect 一致");
    }

    // -----------------------------------------------------------------------
    printf("\n4. D2 移出：target 复位 → 收敛回基准布局\n");
    {
        DockBar bar;
        bar.SetIconSize(64);
        bar.SetSpacing(10);
        bar.AddItem(MakeItem(L"i0"));
        bar.AddItem(MakeItem(L"i1"));

        const POINT hover = {74, 32};
        bar.OnMouseMove(hover);
        int t1 = 0;
        while (bar.TickAnimation() && t1 < 500) ++t1;
        CHECK(bar.GetItemScale(1) > 1.0f, "移出前项1 处于放大状态");

        bar.OnMouseLeave();
        int t2 = 0;
        while (bar.TickAnimation() && t2 < 500) ++t2;
        printf("  [INFO] 复位收敛 tick = %d\n", t2);
        CHECK(NearlyEqual(bar.GetItemScale(1), 1.0f), "OnMouseLeave 后缩放回落到 1.0");
        CHECK(bar.GetPreferredSize().cx == 64 + 10 + 64, "复位后总宽回到基准值");
    }

    // -----------------------------------------------------------------------
    printf("\n5. D2/§8 参数钳制与 Position 影响\n");
    {
        DockBar bar;
        bar.SetIconSize(4);
        CHECK(bar.GetIconSize() == 16, "IconSize 下界钳制为 16");
        bar.SetIconSize(9999);
        CHECK(bar.GetIconSize() == 128, "IconSize 上界钳制为 128");
        bar.SetTransparency(-1);
        CHECK(bar.GetTransparency() == 0, "Transparency 下界钳制为 0");
        bar.SetTransparency(300);
        CHECK(bar.GetTransparency() == 255, "Transparency 上界钳制为 255");
        bar.SetSpacing(-5);
        CHECK(bar.GetSpacing() == 0, "Spacing 负值钳制为 0");
        bar.SetAutoHide(true);
        CHECK(bar.GetAutoHide(), "SetAutoHide(true) 生效");
        bar.SetPosition(DockPosition::Left);
        CHECK(bar.GetPosition() == DockPosition::Left, "SetPosition(Left) 生效");

        // D26-30：主题字段（PanelColor 为 Duilib bkcolor 口径 #AARRGGBB）。
        CHECK(bar.GetPanelColor() == L"#40000000",
              "PanelColor 默认 #40000000（与旧硬编码一致，零视觉回归）");
        CHECK(bar.GetThemeName().empty(), "ThemeName 默认空（未启用主题）");
        bar.SetPanelColor(L"#11223344");
        CHECK(bar.GetPanelColor() == L"#11223344", "SetPanelColor 生效");
        bar.SetPanelColor(L"");
        CHECK(bar.GetPanelColor() == L"#11223344", "SetPanelColor 忽略空值（主题不被清空）");
        bar.SetThemeName(L"Light");
        CHECK(bar.GetThemeName() == L"Light", "SetThemeName 生效");

        bar.SetIconSize(50);
        bar.SetSpacing(6);
        bar.AddItem(MakeItem(L"v0"));
        bar.AddItem(MakeItem(L"v1"));

        const std::vector<RECT>& r = bar.GetItemRects();
        CHECK(r[0].left == 0 && r[0].right == 50 && r[1].top == 56,
              "竖直 Dock：bar 轴（高度）累加，厚度固定为 iconSize");
        CHECK(bar.GetPreferredSize().cx == 50 && bar.GetPreferredSize().cy == 106,
              "竖直 Dock 首选尺寸 = (icon, 总长)");
    }

    // -----------------------------------------------------------------------
    printf("\n6. D1 OnItemClick 的守卫分支\n");
    {
        DockBar bar;
        bar.AddItem(MakeItem(L"nodata"));            // targetPath 为空
        bar.OnItemClick(L"nodata");
        bar.OnItemClick(L"missing");                 // 未知 id
        CHECK(Count(bar) == 1, "空 target / 未知 id 的点击不改变项集合（安全 no-op）");

        HWND hProbe = CreateProbeWindow();
        CHECK(hProbe != nullptr, "创建探针顶层窗口（模拟已运行程序）");
        if (hProbe != nullptr)
        {
            DockItem running = MakeItem(L"running", GetSelfPath().c_str());
            running.isRunning = true;
            running.hWnd      = hProbe;
            bar.AddItem(running);

            bar.OnItemClick(L"running");             // 前置 / 最小化分支
            CHECK(::IsWindow(hProbe), "已运行项点击后窗口仍有效（前置或最小化）");
            ::DestroyWindow(hProbe);
        }
    }

    // -----------------------------------------------------------------------
    printf("\n7. D1 OnItemClick 启动未运行项（真实 ShellExecute）\n");
    {
        DockBar bar;
        DockItem launcher = MakeItem(L"probe", GetSelfPath().c_str());
        launcher.arguments = L"--dock-probe-child";
        bar.AddItem(launcher);

        bar.OnItemClick(L"probe");

        HWND hChild = nullptr;
        const DWORD t0 = ::GetTickCount();
        while ((::GetTickCount() - t0) < kLaunchTimeoutMs)
        {
            hChild = ::FindWindowW(kProbeClassName, nullptr);
            if (hChild != nullptr) break;
            ::Sleep(50);
        }
        CHECK(hChild != nullptr && ::IsWindow(hChild), "未运行项被拉起（子进程探针窗口出现）");

        if (hChild != nullptr)
        {
            ::PostMessageW(hChild, WM_CLOSE, 0, 0);
            const DWORD t1 = ::GetTickCount();
            while (::IsWindow(hChild) && (::GetTickCount() - t1) < kShutdownTimeoutMs)
            {
                ::Sleep(20);
            }
            CHECK(!::IsWindow(hChild), "子进程探针窗口已关闭（无残留）");
        }
    }

    // -----------------------------------------------------------------------
    printf("\n8. D4 ShowRunningApps 置位 / 复位\n");
    {
        DockBar bar;
        // 基准「无匹配」必须用不可能运行的程序路径：第 7 节会真实拉起本测试的自身副本，
        // 该子进程的控制台窗口（标题即 exe 路径）在枚举时可能仍存活，用自身路径会假命中。
        bar.AddItem(MakeItem(L"ghost", L"RainDeskPlusGhostApp_never_running.exe"));
        bar.ShowRunningApps();
        CHECK(!bar.GetItems()[0].isRunning, "无匹配顶层窗口时 isRunning=false");
        CHECK(bar.GetItems()[0].hWnd == nullptr, "无匹配顶层窗口时 hWnd=nullptr");

        HWND hSelf = CreateProbeWindow();
        CHECK(hSelf != nullptr, "创建本进程可见顶层窗口");
        if (hSelf != nullptr)
        {
            bar.AddItem(MakeItem(L"self", GetSelfPath().c_str()));
            bar.ShowRunningApps();
            CHECK(bar.GetItems()[1].isRunning, "按 targetPath 命中运行中程序 → isRunning=true");
            CHECK(bar.GetItems()[1].hWnd == hSelf, "命中的 hWnd 指向该程序的顶层窗口");

            bar.AddItem(MakeItem(L"self", L"ghost_app_never_running.exe"));
            bar.ShowRunningApps();
            CHECK(!bar.GetItems()[1].isRunning, "复位：无可匹配进程 → isRunning=false");
            CHECK(bar.GetItems()[1].hWnd == nullptr, "复位：hWnd=nullptr");

            ::DestroyWindow(hSelf);
        }

        bar.AddItem(MakeItem(L"blank"));   // targetPath 为空
        bar.ShowRunningApps();
        CHECK(!bar.GetItems()[2].isRunning, "targetPath 为空的项恒为未运行");
    }

    printf("\n%s\n", g_Pass ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_Pass ? 0 : 1;
}
