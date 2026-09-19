/*
 * RainDeskPlus - D15 Dock 拖放加项 Smoke 测试（两条构建线共用）
 * 对应 docs/DOCK_DESIGN.md §7.6 / §9（D15）：
 *   路径 → 派生 id/name/targetPath/workingDir → 图标落盘 PNG → 追加项 → 落库 items.ini。
 *
 * 断言覆盖：
 *   1. §7.6.3 元数据派生（含「非 [a-z0-9_-] 替换为 _」与「保留原始大小写」）；
 *   2. §7.6.3 二维去重（同 targetPath 跳过 / id 冲突追加 _2）；
 *   3. §7.6.7 边界（目录 / 不存在的路径 / 空串 / 空列表一律被拒）；
 *   4. §7.6.4 图片文件本身就是图标（直接复制，不重新编码）；
 *   5. §7.6.6 [ON] 拖入后补控件 + 窗口宽度仍 == GetPreferredSize()（D3 尺寸契约加强版）；
 *   6. §7.6.5 写回格式（Dock.ini：[Theme] Name= + [Dock] 6 字段且不写 Transparency；
 *      items.ini：段名 [ItemN] / 6 字段 / Icon 相对路径 / 无 BOM）；
 *   7. §7.6.5 SaveConfig → LoadConfig 往返一致（运行态不落库）。
 *
 * 业务入口 HandleFilesDropped 平台无关（DD-13）：OFF 线只改内存模型，
 * 因此本测试在两条构建线都可运行；ON 线额外验证真实的界面增量与窗口自适应。
 *
 * 测试产物写在可执行文件所在目录（构建输出目录，非 C 盘）下「进程号+时间戳」唯一的
 * 子目录里，每次运行都从零开始，不依赖上一次的清理是否生效。
 */
#include "DockWindow.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef RAINDOCK_USE_DUILIB
using namespace DuiLib;
#endif

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

static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);

    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path.resize(pos + 1);
    return path;
}

// 测试用的暂存目录：与可执行文件同目录（构建输出目录），不落 C 盘。
// 每次运行使用「进程号 + 时间戳」唯一的子目录：本环境下进程内的删除并不可靠
// （DeleteFileW 返回成功，文件却仍可见，实测于 §DBG 诊断），若复用同一目录，
// 上一次运行的 items.ini 会被 Start → LoadConfig 读回，拖入项随即命中 HasTarget
// 去重而返回 0，断言失真。唯一目录让每次运行天然从零开始，与删除是否生效解耦。
static std::wstring ScratchDir()
{
    static const std::wstring dir =
        ExeDir() + L"dock_drop_scratch_" + std::to_wstring(::GetCurrentProcessId()) +
        L"_" + std::to_wstring(::GetTickCount64()) + L"\\";
    return dir;
}

static std::wstring MakeFile(const std::wstring& path)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file << "RainDeskPlus D15 drop probe";
    file.close();
    return path;
}

static bool FileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static unsigned long long FileSizeOf(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
}

// 递归删除目录内容（含子目录本身）：测试卫生的关键——只删单个文件不足以
// 保证「每次运行初始状态一致」，一旦上次运行的 items.ini 残留，
// DockWindow::Start 就会把它读回来，拖入项随即被去重挡掉。
// 入参 dir 必须以 '\\' 结尾。best effort：失败静默，不影响断言结果。
static void RemoveDirRecursive(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd = {};
    const HANDLE h = ::FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring name(fd.cFileName);
            if (name == L"." || name == L"..") continue;

            const std::wstring child = dir + name;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                RemoveDirRecursive(child + L"\\");
            else
                ::DeleteFileW(child.c_str());
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }

    // 目录本来就不存在（ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND）属正常情况。
    ::RemoveDirectoryW(dir.c_str());
}

// 路径等价（忽略大小写）；空串与空串视为等价。
static bool SamePath(const std::wstring& a, const std::wstring& b)
{
    return ::CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

static std::string ReadAll(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

#ifdef RAINDOCK_USE_DUILIB
// 有界消息泵（同 D3 Smoke）：拖入后的重排/重绘需要消息循环才能落到真实窗口。
static void PumpMessages(DWORD milliseconds)
{
    const DWORD start = ::GetTickCount();
    MSG msg = {};
    while (::GetTickCount() - start < milliseconds)
    {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(10);
    }
}
#endif

int main()
{
    printf("=== D15 Dock 拖放加项 Smoke Test ===\n\n");

#ifdef RAINDOCK_USE_DUILIB
    CPaintManagerUI::SetInstance(::GetModuleHandle(nullptr));
#endif

    const std::wstring scratch  = ScratchDir();
    const std::wstring srcDir   = scratch + L"src\\";
    const std::wstring otherDir = scratch + L"other\\";

    // 兜底清空（正常路径下该唯一目录本就不存在）：上一次运行残留的 items.ini 会被
    // Start → LoadConfig 读回，使拖入的项命中 HasTarget 去重而返回 0。
    RemoveDirRecursive(scratch);
    ::CreateDirectoryW(scratch.c_str(), nullptr);
    ::CreateDirectoryW(srcDir.c_str(), nullptr);
    ::CreateDirectoryW(otherDir.c_str(), nullptr);

    const std::wstring ini      = scratch + L"Dock.ini";
    const std::wstring itemsIni = scratch + L"items.ini";

    const std::wstring alpha     = MakeFile(srcDir   + L"alpha.exe");
    const std::wstring myTool    = MakeFile(srcDir   + L"My Tool.exe");
    const std::wstring chromeSrc = MakeFile(srcDir   + L"chrome.exe");
    const std::wstring chromeOth = MakeFile(otherDir + L"chrome.exe");
    const std::wstring photo     = MakeFile(srcDir   + L"photo.png");
    const std::wstring subDir    = srcDir + L"subdir";
    ::CreateDirectoryW(subDir.c_str(), nullptr);

    raindock::DockWindow wnd;
    raindock::DockBar&   bar = wnd.GetDockBar();

    CHECK(wnd.Start(ini), "DockWindow::Start（OFF 线仅加载配置，ON 线另建窗口）");

    // D26-30：主题声明同样走持久化——显式设置后 Stop() 回写、LoadConfig 读回应一致。
    bar.SetThemeName(L"Dark");

#ifdef RAINDOCK_USE_DUILIB
    HWND hWnd = wnd.GetHWND();
    CHECK(hWnd != nullptr && ::IsWindow(hWnd), "ON 线：Dock 窗口已创建");
    PumpMessages(300);
#else
    // OFF 线不建窗口：HandleFilesDropped 只改内存模型（DD-13）。
    HWND hWnd = nullptr;
    (void)hWnd;
#endif

    // -----------------------------------------------------------------------
    printf("\n1. §7.6.3 元数据派生：id / name / targetPath / workingDir\n");
    {
        CHECK(wnd.HandleFilesDropped({ alpha }) == 1, "拖入 alpha.exe 新增 1 项");

        const raindock::DockItem* item = bar.FindItem(L"alpha");
        CHECK(item != nullptr, "id = 文件名去扩展名并转小写（alpha）");
        if (item != nullptr)
        {
            CHECK(item->name == L"alpha", "name 保留原始大小写");
            CHECK(SamePath(item->targetPath, alpha), "targetPath = 规范化后的绝对路径");
            CHECK(SamePath(item->workingDir, srcDir), "workingDir = 文件所在目录");
            CHECK(item->arguments.empty(), "arguments 默认为空");
            printf("  [INFO] iconPath = %ls\n", item->iconPath.c_str());
            CHECK(!item->iconPath.empty() && FileExists(item->iconPath),
                  "图标已提取并落盘（§7.6.4）");
            CHECK(SamePath(item->iconPath, scratch + L"Icons\\alpha.png"),
                  "图标路径 = <dockFolder>Icons\\<id>.png");
        }

        CHECK(wnd.HandleFilesDropped({ myTool }) == 1, "拖入含空格的文件名");
        const raindock::DockItem* spaced = bar.FindItem(L"my_tool");
        CHECK(spaced != nullptr, "id 派生：非 [a-z0-9_-] 字符替换为 _（My Tool → my_tool）");
        if (spaced != nullptr) CHECK(spaced->name == L"My Tool", "name 保留空格与原始大小写");
    }

    // -----------------------------------------------------------------------
    printf("\n2. §7.6.3 二维去重\n");
    {
        const size_t before = bar.GetItems().size();

        CHECK(wnd.HandleFilesDropped({ alpha }) == 0, "同 targetPath 再次拖入 → 跳过（返回 0）");
        CHECK(bar.GetItems().size() == before, "重复拖入不改变项数");
        CHECK(bar.FindItem(L"alpha") != nullptr && bar.FindItem(L"alpha_2") == nullptr,
              "重复拖入不产生第二项，也不覆盖既有的 name");

        CHECK(wnd.HandleFilesDropped({ alpha, alpha }) == 0,
              "同一批（模拟单个 HDROP）内路径重复 → 天然幂等");

        CHECK(wnd.HandleFilesDropped({ chromeSrc }) == 1, "拖入 src\\chrome.exe");
        CHECK(bar.FindItem(L"chrome") != nullptr, "首个 chrome 占用 id=chrome");
        CHECK(wnd.HandleFilesDropped({ chromeOth }) == 1,
              "拖入 other\\chrome.exe（同文件名、不同路径）");
        const raindock::DockItem* second = bar.FindItem(L"chrome_2");
        CHECK(second != nullptr, "id 冲突时改名不改路径（chrome → chrome_2）");
        CHECK(bar.GetItems().size() == before + 2, "两项都保留");
        if (second != nullptr)
        {
            CHECK(SamePath(second->targetPath, chromeOth), "chrome_2 指向 other\\chrome.exe");
        }
    }

    // -----------------------------------------------------------------------
    printf("\n3. §7.6.7 边界：目录 / 不存在的路径 / 空串 / 空列表\n");
    {
        const size_t before = bar.GetItems().size();
        CHECK(wnd.HandleFilesDropped({ subDir }) == 0, "拖入目录被拒（目录支持归 Phase 2）");
        CHECK(wnd.HandleFilesDropped({ srcDir + L"ghost.exe" }) == 0, "不存在的路径被拒");
        CHECK(wnd.HandleFilesDropped({ std::wstring() }) == 0, "空路径被拒");
        CHECK(wnd.HandleFilesDropped({}) == 0, "空路径列表返回 0");
        CHECK(bar.GetItems().size() == before, "边界输入不改变项集合");
    }

    // -----------------------------------------------------------------------
    printf("\n4. §7.6.4 图片文件本身就是图标（直接复制，不重新编码）\n");
    {
        CHECK(wnd.HandleFilesDropped({ photo }) == 1, "拖入 photo.png");
        const raindock::DockItem* item = bar.FindItem(L"photo");
        CHECK(item != nullptr, "id = photo");
        if (item != nullptr)
        {
            CHECK(!item->iconPath.empty() && FileExists(item->iconPath), "图标文件已生成");
            CHECK(SamePath(item->iconPath, scratch + L"Icons\\photo.png"),
                  "图标落盘路径 = Icons\\photo.png");
            CHECK(FileSizeOf(item->iconPath) == FileSizeOf(photo),
                  "内容与源图片逐字节同源（复制而非重新编码）");
        }
    }

    // -----------------------------------------------------------------------
#ifdef RAINDOCK_USE_DUILIB
    printf("\n5. §7.6.6 ON 线：拖入后补控件 + 窗口宽度仍 == GetPreferredSize()\n");
    {
        RECT wr = { 0 };
        ::GetWindowRect(hWnd, &wr);
        const long before = wr.right - wr.left;

        const std::wstring extra = MakeFile(srcDir + L"extra.exe");
        CHECK(wnd.HandleFilesDropped({ extra }) == 1, "ON 线拖入 extra.exe");
        PumpMessages(400);

        ::GetWindowRect(hWnd, &wr);
        const long after  = wr.right - wr.left;
        const SIZE expect = bar.GetPreferredSize();
        printf("  [INFO] 窗口宽度 %ld → %ld，期望 %ld\n",
               before, after, static_cast<long>(expect.cx));

        CHECK(after > before, "窗口已按新增项变宽（§7.6.6 刷新序列生效）");
        CHECK(after == expect.cx, "拖入后宽度仍 == GetPreferredSize()（D3 断言加强版）");
        CHECK(::IsWindowVisible(hWnd), "拖入后窗口仍可见");

        ::DeleteFileW(extra.c_str());
    }
#else
    printf("\n5. §7.6.6 ON 线断言跳过（当前为 OFF 线构建）\n");
#endif

    // -----------------------------------------------------------------------
    const size_t total = bar.GetItems().size();
    wnd.Stop();   // 回写 Dock.ini + items.ini；ON 线同时销毁窗口

#ifdef RAINDOCK_USE_DUILIB
    PumpMessages(300);
#endif

    printf("\n6. §7.6.5 items.ini 写回格式\n");
    {
        CHECK(FileExists(ini), "Dock.ini 已写回");
        CHECK(FileExists(itemsIni), "items.ini 已写回");

        const std::string dockText = ReadAll(ini);
        // D26-30：主文件必须写 [Theme] Name=，否则重载后主题声明丢失（!Refresh 切不回来）；
        // [Dock] 落 6 字段；Transparency 改由主题 [Colors] 管辖，写死会遮住主题。
        CHECK(Contains(dockText, "[Theme]\r\nName=Dark\r\n"),
              "Dock.ini 写 [Theme] Name= 主题声明");
        CHECK(Contains(dockText, "[Dock]\r\n") && Contains(dockText, "IconSize=") &&
              Contains(dockText, "Position=") && Contains(dockText, "AutoHide=") &&
              Contains(dockText, "Spacing=") && Contains(dockText, "MaxScale=") &&
              Contains(dockText, "MaxRadius="),
              "Dock.ini 仍写 [Dock] 6 字段（与 items.ini 同时落盘）");
        CHECK(!Contains(dockText, "Transparency="),
              "[Dock] 不写 Transparency（透明度归主题管辖）");

        const std::string text = ReadAll(itemsIni);
        CHECK(Contains(text, "; RainDeskPlus Dock items (generated)"), "首行写生成来源注释");
        CHECK(text.compare(0, 3, "\xEF\xBB\xBF") != 0, "UTF-8 无 BOM");
        CHECK(Contains(text, "[Item1]\r\n"), "段名固定为 [ItemN]");
        CHECK(Contains(text, "\r\n"), "行尾为 CRLF");
        CHECK(Contains(text, "Id=alpha\r\n"), "写入 Id 字段");
        CHECK(Contains(text, "Name=My Tool\r\n"), "写入 Name 字段（保留原始大小写）");
        CHECK(Contains(text, "Target="), "写入 Target 字段（绝对路径）");
        CHECK(Contains(text, "WorkingDir="), "写入 WorkingDir 字段");
        CHECK(Contains(text, "Arguments=\r\n"), "写入 Arguments 字段（空值）");
        CHECK(Contains(text, "Icon=Icons\\alpha.png"), "Icon 写成相对 Dock.ini 目录的路径");
    }

    // -----------------------------------------------------------------------
    printf("\n7. §7.6.5 SaveConfig → LoadConfig 往返一致\n");
    {
        raindock::DockBar reloaded;
        CHECK(reloaded.LoadConfig(ini), "LoadConfig 读回 Dock.ini + items.ini");
        CHECK(reloaded.GetItems().size() == total, "项数一致");
        CHECK(reloaded.GetThemeName() == bar.GetThemeName(),
              "[Theme] Name= 往返一致（主题声明不丢）");

        const std::vector<raindock::DockItem>& a = bar.GetItems();
        const std::vector<raindock::DockItem>& b = reloaded.GetItems();
        const size_t n = (a.size() < b.size()) ? a.size() : b.size();

        size_t mismatches = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (a[i].id != b[i].id || a[i].name != b[i].name ||
                !SamePath(a[i].targetPath, b[i].targetPath) ||
                !SamePath(a[i].iconPath, b[i].iconPath) ||
                !SamePath(a[i].workingDir, b[i].workingDir) ||
                a[i].arguments != b[i].arguments)
            {
                ++mismatches;
                printf("  [INFO] 第 %zu 项往返不一致（%ls / %ls）\n",
                       i, a[i].id.c_str(), b[i].id.c_str());
            }
        }
        CHECK(mismatches == 0, "每项 6 个配置字段往返一致");

        CHECK(!b.empty() && !b[0].isRunning, "运行态 isRunning 不落库（读回为默认值）");
        CHECK(b.empty() || b[0].hWnd == nullptr, "运行态 hWnd 不落库（读回为 nullptr）");
    }

    // -----------------------------------------------------------------------
#ifdef RAINDOCK_USE_DUILIB
    CPaintManagerUI::Term();
#endif

    // 清理本次生成的探针文件与目录（best effort，不影响断言结果）。
    RemoveDirRecursive(scratch);

    printf("\n%s\n", g_Pass ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_Pass ? 0 : 1;
}
