/*
 * RainDeskPlus - D5 Duilib 环境 Smoke 测试（Hello World）
 * 验证：Duilib 静态库可编译链接、CPaintManagerUI 运行时可用、
 *       内联 XML 皮肤可解析成控件树、窗口可创建/显示并正常销毁。
 *
 * 无交互：自动泵消息约 1.5 秒后主动关闭窗口并退出，便于脚本化验证。
 */
#include "UIlib.h"

#include <cstdio>

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

// ---------- Hello World 窗口 ----------
class CHelloWindow : public WindowImplBase
{
public:
    CHelloWindow() = default;

    bool HasRoot() const { return m_pm.GetRoot() != nullptr; }
    CControlUI* FindByName(LPCTSTR name) { return m_pm.FindControl(name); }

protected:
    LPCTSTR GetWindowClassName() const override
    {
        return _T("RainDeskPlusHelloWnd");
    }

    // D5 不依赖外部皮肤文件：直接返回内联 XML。
    // CDialogBuilder::Create 以首字符 '<' 判定为内联串，根节点必须是 Window。
    CDuiString GetSkinFile() override
    {
        return _T("<Window size=\"480,320\">")
               _T("  <VerticalLayout bkcolor=\"#FF1E1E1E\">")
               _T("    <Label name=\"hello\" text=\"Hello, Duilib!\" ")
               _T("           textcolor=\"#FFFFFFFF\" align=\"center\" valign=\"center\" />")
               _T("  </VerticalLayout>")
               _T("</Window>");
    }
};

// 有界消息泵：避免 CPaintManagerUI::MessageLoop() 永久阻塞
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

int main()
{
    printf("=== D5 Duilib Hello World Smoke Test ===\n\n");

    CPaintManagerUI::SetInstance(::GetModuleHandle(nullptr));

    CHelloWindow wnd;
    HWND hWnd = wnd.Create(nullptr, _T("RainDeskPlus - Duilib Hello World"),
                           UI_WNDSTYLE_FRAME, WS_EX_WINDOWEDGE);
    CHECK(hWnd != nullptr && ::IsWindow(hWnd), "Duilib 窗口创建成功");

    if (hWnd != nullptr)
    {
        CHECK(wnd.HasRoot(), "内联 XML 皮肤解析成功（根控件已挂载）");
        CHECK(wnd.FindByName(_T("hello")) != nullptr, "按 name 查找到 Label 控件");

        RECT rc = {0};
        ::GetClientRect(hWnd, &rc);
        printf("  [INFO] 客户区尺寸: %ld x %ld\n",
               rc.right - rc.left, rc.bottom - rc.top);

        wnd.CenterWindow();
        wnd.ShowWindow();
        PumpMessages(1500);
        CHECK(::IsWindowVisible(hWnd), "消息泵期间窗口保持可见");

        wnd.Close();
        PumpMessages(500);
        CHECK(!::IsWindow(hWnd), "窗口已正常销毁");
    }

    CPaintManagerUI::Term();

    printf("\n%s\n", g_Pass ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_Pass ? 0 : 1;
}
