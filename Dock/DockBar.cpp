/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * DockBar 实现：项管理 + 一维布局（悬停放大的每帧重排）+ 动画中间量 + 配置持久化
 * + 运行中进程匹配。纯逻辑实现，不含任何 Duilib 依赖（DD-1）。
 * 详见 docs/DOCK_DESIGN.md §7.1 / §7.3 / §7.4。
 */
#include "DockBar.h"

#include "ConfigParser.h"
#include "IconExtractor.h"
#include "Logger.h"
#include "PathUtil.h"
#include "StringUtil.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <utility>

#include <shellapi.h>

namespace raindock {

namespace {

constexpr float kApproach  = 0.2f;    // 一阶逼近系数：每帧收敛剩余差的 20%
constexpr float kSnapEps   = 1e-3f;   // 吸附阈值：差值小于它就直接吸附，避免无限次重绘

// 图标尺寸/透明度的合法区间（§8 约束：越范围 clamp 并记 [WARN]）
constexpr int kIconSizeMin = 16;
constexpr int kIconSizeMax = 128;

inline int ClampInt(int value, int lo, int hi)
{
    return (value < lo) ? lo : ((value > hi) ? hi : value);
}

inline bool IsVertical(DockPosition pos)
{
    return pos == DockPosition::Left || pos == DockPosition::Right;
}

const WCHAR* PositionName(DockPosition pos)
{
    switch (pos)
    {
    case DockPosition::Top:   return L"Top";
    case DockPosition::Left:  return L"Left";
    case DockPosition::Right: return L"Right";
    case DockPosition::Bottom:return L"Bottom";
    }
    return L"Bottom";
}

DockPosition ParsePosition(const std::wstring& text)
{
    if (StringUtil::EqualsIgnoreCase(text, L"Top"))    return DockPosition::Top;
    if (StringUtil::EqualsIgnoreCase(text, L"Left"))   return DockPosition::Left;
    if (StringUtil::EqualsIgnoreCase(text, L"Right"))  return DockPosition::Right;
    if (StringUtil::EqualsIgnoreCase(text, L"Bottom")) return DockPosition::Bottom;

    LogWarningF(L"RainDeskPlus Dock: Position=\"%s\" 非法（应为 Top/Bottom/Left/Right），回退为 Bottom",
                text.c_str());
    return DockPosition::Bottom;
}

// 取窗口所属进程的可执行文件全路径。受保护进程（OpenProcess 失败）返回空串，不报错。
std::wstring QueryWindowImagePath(HWND hWnd)
{
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hWnd, &pid);
    if (pid == 0) return std::wstring();

    HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc == nullptr) return std::wstring();

    WCHAR buf[MAX_PATH * 4] = {};
    DWORD size = static_cast<DWORD>(_countof(buf));
    const BOOL ok = ::QueryFullProcessImageNameW(hProc, 0, buf, &size);
    ::CloseHandle(hProc);   // 与本作用域内所有返回路径同域释放，不存在泄漏
    if (!ok) return std::wstring();

    return std::wstring(buf, size);
}

// 规范化为「全路径 + 去尾反斜杠 + 展开环境变量」，用于忽略大小写的路径比较。
std::wstring NormalizePath(const std::wstring& path)
{
    if (path.empty()) return std::wstring();

    std::wstring expanded = path;
    PathUtil::ExpandEnvironmentVariables(expanded);

    WCHAR buf[MAX_PATH * 4] = {};
    const DWORD n = ::GetFullPathNameW(expanded.c_str(), static_cast<DWORD>(_countof(buf)),
                                       buf, nullptr);
    if (n == 0 || n >= _countof(buf)) return std::wstring();

    std::wstring full(buf, n);
    // 卷根（如 "C:\"）去掉尾反斜杠会退化成 "C:"，反而不再等价，必须保留。
    if (full.size() > 3) PathUtil::RemoveTrailingBackslash(full);
    return full;
}

// 相对路径以配置文件所在目录（已带尾反斜杠）为基准解析（§7.4 尾注）。
std::wstring ResolveRelativePath(const std::wstring& baseFolder, const std::wstring& path)
{
    if (path.empty() || baseFolder.empty() || PathUtil::IsAbsolute(path)) return path;
    return baseFolder + path;
}

// 取文件名（含扩展名）；无分隔符时返回整串。
std::wstring FileNameOf(const std::wstring& path)
{
    size_t start = 0;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (PathUtil::IsSeparator(path[i])) start = i + 1;
    }
    return path.substr(start);
}

// 取文件名（去扩展名）。前导点（如 ".gitignore"）不算扩展名分隔符。
std::wstring FileNameWithoutExtension(const std::wstring& path)
{
    std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.erase(dot);
    return name;
}

// id 派生（§7.6.3）：文件名去扩展名 → 转小写 → 非 [a-z0-9_-] 一律替换为 '_'。
std::wstring MakeItemId(const std::wstring& name)
{
    std::wstring id = name;
    StringUtil::ToLowerCase(id);
    for (WCHAR& ch : id)
    {
        const bool keep = (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') ||
                          ch == L'_' || ch == L'-';
        if (!keep) ch = L'_';
    }
    return id;
}

// 拖入项必须是「已存在的普通文件」：目录与非文件一律拒绝（§7.6.7）。
bool IsRegularFile(const std::wstring& path)
{
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 写 items.ini 时把图标绝对路径转回相对 Dock.ini 目录（与 ResolveRelativePath 对称，§7.6.5）。
std::wstring IconPathToIni(const std::wstring& folder, const std::wstring& iconPath)
{
    if (iconPath.empty() || folder.empty()) return iconPath;
    if (!PathUtil::IsAbsolute(iconPath)) return iconPath;

    // 只有落在 Dock.ini 目录内的图标才写相对路径；用户手工指定的外部图标
    // 原样写绝对路径，因为 LoadConfig 的 ResolveRelativePath 只对相对路径拼接。
    if (iconPath.size() > folder.size() &&
        StringUtil::EqualsIgnoreCase(std::wstring_view(iconPath).substr(0, folder.size()),
                                     std::wstring_view(folder)))
    {
        return iconPath.substr(folder.size());
    }
    return iconPath;
}

// 写 items.ini：每项一段 [ItemN]，仅落 6 个配置字段（运行态不落库），§7.6.5。
// D31-35 起统一走 ConfigParser 落盘（DD-7 例外取消），不再自行拼装 UTF-8 字节流。
bool WriteItemsIni(const std::wstring& itemsIni, const std::wstring& folder,
                   const std::vector<DockItem>& items)
{
    ConfigParser cfg;
    cfg.SetHeaderComment(L"; RainDeskPlus Dock items (generated)");

    for (size_t i = 0; i < items.size(); ++i)
    {
        const DockItem& item = items[i];
        const std::wstring section = L"Item" + std::to_wstring(i + 1);
        cfg.SetValue(section, L"Id",         item.id);
        cfg.SetValue(section, L"Name",       item.name);
        cfg.SetValue(section, L"Icon",       IconPathToIni(folder, item.iconPath));
        cfg.SetValue(section, L"Target",     item.targetPath);
        cfg.SetValue(section, L"WorkingDir", item.workingDir);
        cfg.SetValue(section, L"Arguments",  item.arguments);
    }

    return cfg.SaveFile(itemsIni);
}

struct WindowImage
{
    HWND         hwnd = nullptr;
    std::wstring exePath;   // 已规范化，可直接比较
};

// EnumWindows 回调：收集「可见 + 无属主 + 非工具窗口」的顶层窗口及其进程路径（§7.3）。
BOOL CALLBACK CollectTopLevelWindows(HWND hWnd, LPARAM lParam)
{
    auto* out = reinterpret_cast<std::vector<WindowImage>*>(lParam);
    if (out == nullptr) return FALSE;

    if (!::IsWindowVisible(hWnd)) return TRUE;
    // 顶层窗口的属主用 GWLP_HWNDPARENT 读取（SDK 无 GWL_OWNER 常量）。
    if (::GetWindowLongPtrW(hWnd, GWLP_HWNDPARENT) != 0) return TRUE;   // 有属主：弹窗，跳过
    if ((::GetWindowLongPtrW(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;

    WindowImage image;
    image.exePath = NormalizePath(QueryWindowImagePath(hWnd));
    if (image.exePath.empty()) return TRUE;

    image.hwnd = hWnd;
    out->push_back(std::move(image));
    return TRUE;
}

}  // namespace

DockBar::DockBar()
{
    ComputeLayout();   // 初始为空布局，保证 m_RectCache 与 m_Visuals 同长
}

// -----------------------------------------------------------------------
// 项管理（始终保持 m_Items.size() == m_Visuals.size() 且下标一一对应）
// -----------------------------------------------------------------------

void DockBar::AddItem(const DockItem& item)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&item](const DockItem& i) { return i.id == item.id; });
    if (it != m_Items.end())
    {
        const size_t index = static_cast<size_t>(std::distance(m_Items.begin(), it));
        // 只替换配置数据；视觉中间量复位，避免沿用同一 id 上一条目的旧动画值。
        *it = item;
        m_Visuals[index].cur    = 1.0f;
        m_Visuals[index].target = 1.0f;
    }
    else
    {
        m_Items.push_back(item);
        m_Visuals.push_back(ItemVisual{});
    }
    ComputeLayout();
}

void DockBar::RemoveItem(const std::wstring& id)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&id](const DockItem& i) { return i.id == id; });
    if (it == m_Items.end()) return;

    const size_t index = static_cast<size_t>(std::distance(m_Items.begin(), it));
    m_Items.erase(it);
    m_Visuals.erase(m_Visuals.begin() + static_cast<std::ptrdiff_t>(index));
    ComputeLayout();
}

void DockBar::ClearItems()
{
    m_Items.clear();
    m_Visuals.clear();
    ComputeLayout();
}

const std::vector<DockItem>& DockBar::GetItems() const
{
    return m_Items;
}

DockItem* DockBar::FindItem(const std::wstring& id)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&id](const DockItem& i) { return i.id == id; });
    return (it != m_Items.end()) ? &(*it) : nullptr;
}

// -----------------------------------------------------------------------
// 拖放加项（D15，§7.6）
// -----------------------------------------------------------------------

bool DockBar::HasTarget(const std::wstring& targetPath) const
{
    const std::wstring target = NormalizePath(targetPath);
    if (target.empty()) return false;

    return std::any_of(m_Items.begin(), m_Items.end(), [&target](const DockItem& i) {
        return StringUtil::EqualsIgnoreCase(NormalizePath(i.targetPath), target);
    });
}

std::wstring DockBar::AddItemFromPath(const std::wstring& path, const std::wstring& dockFolder)
{
    if (path.empty() || dockFolder.empty()) return std::wstring();

    const std::wstring target = NormalizePath(path);
    if (target.empty())
    {
        LogWarningF(L"RainDeskPlus Dock: 拖入路径无法规范化（\"%s\"），已跳过", path.c_str());
        return std::wstring();
    }

    if (!IsRegularFile(target))
    {
        LogWarningF(L"RainDeskPlus Dock: 拖入项不是文件（\"%s\"），已跳过", target.c_str());
        return std::wstring();
    }

    // 去重维度一：同 targetPath 直接跳过——重复拖同一个 exe 不应产生第二项，
    // 也不应覆盖用户已改过的 Name（§7.6.3）。
    if (HasTarget(target)) return std::wstring();

    DockItem item;
    item.targetPath = target;
    item.name       = FileNameWithoutExtension(target);   // 保留原始大小写
    item.workingDir = PathUtil::GetFolderFromFilePath(target);

    // 去重维度二：id 冲突（targetPath 不同）时改名不改路径，依次追加 _2、_3…
    std::wstring baseId = MakeItemId(item.name);
    if (baseId.empty()) baseId = L"item";
    std::wstring id = baseId;
    for (int suffix = 2; FindItem(id) != nullptr; ++suffix)
    {
        id = baseId + L"_" + std::to_wstring(suffix);
    }
    item.id = id;

    // 图标落盘失败不丢弃用户拖入的项，只是无图标（§7.6.4 失败降级）。
    const std::wstring iconAbs = dockFolder + L"Icons\\" + id + L".png";
    if (IconExtractor::ExtractToPng(target, iconAbs)) item.iconPath = iconAbs;

    AddItem(item);   // 复用现有入口：同 id 替换 / 否则追加 + ComputeLayout
    return item.id;
}

// -----------------------------------------------------------------------
// 布局出口（DD-6：只读）
// -----------------------------------------------------------------------

const std::vector<RECT>& DockBar::GetItemRects() const
{
    return m_RectCache;
}

float DockBar::GetItemScale(size_t index) const
{
    if (index >= m_Visuals.size()) return 1.0f;
    return m_Visuals[index].cur;
}

RECT DockBar::GetBarRect() const
{
    RECT bar = {0, 0, 0, 0};
    if (m_RectCache.empty()) return bar;

    bar = m_RectCache.front();
    for (const RECT& r : m_RectCache)
    {
        if (r.left   < bar.left)   bar.left   = r.left;
        if (r.top    < bar.top)    bar.top    = r.top;
        if (r.right  > bar.right)  bar.right  = r.right;
        if (r.bottom > bar.bottom) bar.bottom = r.bottom;
    }
    return bar;
}

SIZE DockBar::GetPreferredSize() const
{
    const RECT bar = GetBarRect();
    SIZE size{};
    size.cx = bar.right - bar.left;
    size.cy = bar.bottom - bar.top;
    return size;
}

// -----------------------------------------------------------------------
// 布局与动画（§7.1）
// -----------------------------------------------------------------------

void DockBar::OnMouseMove(const POINT& pt)
{
    m_LastMouse   = pt;
    m_MouseInside = true;

    const POINT barPt = ToBarAxis(pt);
    for (size_t i = 0; i < m_Items.size(); ++i)
    {
        // 关键：用「未放大的基准矩形」求 target，而不是上一帧已放大的 rect，
        // 否则 rect 变大 → dist 变小 → 缩放更大 → rect 更大，形成正反馈振荡。
        const RECT base = BaseRectOf(i);
        m_Visuals[i].target = m_Magnify.CalculateScale(barPt, base);
    }
    ComputeLayout();
}

void DockBar::OnMouseLeave()
{
    m_MouseInside = false;
    m_LastMouse   = POINT{0, 0};
    for (ItemVisual& v : m_Visuals)
    {
        v.target = 1.0f;
    }
    ComputeLayout();
}

bool DockBar::TickAnimation()
{
    bool changed = false;
    for (ItemVisual& v : m_Visuals)
    {
        if (std::fabs(v.target - v.cur) > kSnapEps)
        {
            v.cur += (v.target - v.cur) * kApproach;
            changed = true;
        }
        else
        {
            v.cur = v.target;   // 吸附，避免浮点尾差导致无限次重绘
        }
    }

    if (changed) ComputeLayout();
    return changed;
}

void DockBar::ComputeLayout()
{
    const int  icon     = m_IconSize;
    const bool vertical = IsVertical(m_Position);

    m_RectCache.resize(m_Visuals.size());

    // 一维化：沿 Dock 轴累加「缩放后的尺寸 + 间距」，正交方向固定为 icon（bar 厚度）。
    // 竖直 Dock 的 x/y 交换只体现在写入坐标时，布局算法本身不分支（§7.1 尾注）。
    int cursor = 0;
    for (size_t i = 0; i < m_Visuals.size(); ++i)
    {
        const int scaled = static_cast<int>(static_cast<float>(icon) * m_Visuals[i].cur + 0.5f);

        RECT rect{};
        if (vertical)
        {
            rect.left   = 0;
            rect.top    = cursor;
            rect.right  = icon;
            rect.bottom = cursor + scaled;
        }
        else
        {
            rect.left   = cursor;
            rect.top    = 0;
            rect.right  = cursor + scaled;
            rect.bottom = icon;
        }

        m_Visuals[i].rect = rect;
        m_RectCache[i]    = rect;
        cursor += scaled + m_Spacing;
    }
}

POINT DockBar::ToBarAxis(const POINT& pt) const
{
    POINT bar{};
    if (IsVertical(m_Position))
    {
        bar.x = pt.y;   // 竖直 Dock：bar 轴是屏幕 y
        bar.y = pt.x;
    }
    else
    {
        bar.x = pt.x;
        bar.y = pt.y;
    }
    return bar;
}

RECT DockBar::BaseRectOf(size_t index) const
{
    // bar 轴坐标下的静态格位：水平/竖直 Dock 共用同一套一维公式，
    // 因此放大解算与布局解算的坐标系严格一致。
    const int offset = static_cast<int>(index) * (m_IconSize + m_Spacing);
    RECT base{};
    base.left   = offset;
    base.top    = 0;
    base.right  = offset + m_IconSize;
    base.bottom = m_IconSize;
    return base;
}

// -----------------------------------------------------------------------
// 点击与任务管理（§7.3）
// -----------------------------------------------------------------------

void DockBar::OnItemClick(const std::wstring& id)
{
    auto it = std::find_if(m_Items.begin(), m_Items.end(),
                           [&id](const DockItem& i) { return i.id == id; });
    if (it == m_Items.end()) return;

    if (it->isRunning && it->hWnd != nullptr && ::IsWindow(it->hWnd))
    {
        if (::GetForegroundWindow() == it->hWnd)
        {
            ::ShowWindow(it->hWnd, SW_MINIMIZE);        // 已在前台 → 最小化
        }
        else
        {
            if (::IsIconic(it->hWnd)) ::ShowWindow(it->hWnd, SW_RESTORE);
            ::SetForegroundWindow(it->hWnd);            // 否则恢复并前置
        }
        return;
    }

    if (it->targetPath.empty()) return;

    // 未运行：启动。
    const std::wstring params  = it->arguments;
    const std::wstring workDir = it->workingDir;
    const INT_PTR result = reinterpret_cast<INT_PTR>(::ShellExecuteW(
        nullptr, L"open", it->targetPath.c_str(),
        params.empty()  ? nullptr : params.c_str(),
        workDir.empty() ? nullptr : workDir.c_str(),
        SW_SHOWNORMAL));

    if (result <= 32)   // ShellExecuteW 约定：返回值 <= 32 表示失败
    {
        LogWarningF(L"RainDeskPlus Dock: 启动 \"%s\" 失败（ShellExecuteW 返回 %lld）",
                    it->targetPath.c_str(), static_cast<long long>(result));
    }
}

void DockBar::ShowRunningApps()
{
    std::vector<WindowImage> windows;
    windows.reserve(64);
    ::EnumWindows(&CollectTopLevelWindows, reinterpret_cast<LPARAM>(&windows));

    for (DockItem& item : m_Items)
    {
        item.isRunning = false;
        item.hWnd      = nullptr;
        if (item.targetPath.empty()) continue;

        const std::wstring target = NormalizePath(item.targetPath);
        if (target.empty()) continue;

        auto hit = std::find_if(windows.begin(), windows.end(),
                                [&target](const WindowImage& w)
                                { return StringUtil::EqualsIgnoreCase(w.exePath, target); });
        if (hit != windows.end())
        {
            item.isRunning = true;
            item.hWnd      = hit->hwnd;
        }
    }
}

void DockBar::MinimizeToDock(HWND hWnd)
{
    if (hWnd == nullptr || !::IsWindow(hWnd)) return;

    ::ShowWindow(hWnd, SW_MINIMIZE);

    // 顺手把该窗口绑定到进程路径匹配的项，使下次点击可直接前置。
    const std::wstring exe = NormalizePath(QueryWindowImagePath(hWnd));
    if (exe.empty()) return;

    for (DockItem& item : m_Items)
    {
        if (item.targetPath.empty()) continue;
        if (StringUtil::EqualsIgnoreCase(NormalizePath(item.targetPath), exe))
        {
            item.isRunning = true;
            item.hWnd      = hWnd;
            break;
        }
    }
}

// -----------------------------------------------------------------------
// 配置属性
// -----------------------------------------------------------------------

void DockBar::SetIconSize(int size)
{
    m_IconSize = ClampInt(size, kIconSizeMin, kIconSizeMax);
    ComputeLayout();
}

void DockBar::SetTransparency(int alpha)
{
    m_Transparency = ClampInt(alpha, 0, 255);
}

void DockBar::SetPosition(DockPosition pos)
{
    m_Position = pos;
    ComputeLayout();
}

void DockBar::SetAutoHide(bool enable)
{
    m_AutoHide = enable;
}

void DockBar::SetSpacing(int px)
{
    m_Spacing = (px < 0) ? 0 : px;
    ComputeLayout();
}

void DockBar::SetPanelColor(const std::wstring& color)
{
    if (!color.empty()) m_PanelColor = color;
}

void DockBar::SetThemeName(const std::wstring& name)
{
    m_ThemeName = name;
}

int                DockBar::GetIconSize() const     { return m_IconSize; }
int                DockBar::GetTransparency() const { return m_Transparency; }
DockPosition       DockBar::GetPosition() const     { return m_Position; }
bool               DockBar::GetAutoHide() const     { return m_AutoHide; }
int                DockBar::GetSpacing() const      { return m_Spacing; }
const DockMagnify& DockBar::GetMagnify() const      { return m_Magnify; }
const std::wstring& DockBar::GetPanelColor() const  { return m_PanelColor; }
const std::wstring& DockBar::GetThemeName() const   { return m_ThemeName; }

// -----------------------------------------------------------------------
// 配置持久化（§7.4）
// -----------------------------------------------------------------------

bool DockBar::LoadConfig(const std::wstring& iniPath)
{
    ConfigParser cfg;
    if (!cfg.LoadFile(iniPath)) return false;

    // 数值越范围时 clamp 并告警（§8 约束）。
    const int rawIconSize = cfg.ReadInt(L"Dock", L"IconSize", m_IconSize);
    if (rawIconSize < kIconSizeMin || rawIconSize > kIconSizeMax)
    {
        LogWarningF(L"RainDeskPlus Dock: IconSize=%d 超出 %d..%d，已钳制为 %d",
                    rawIconSize, kIconSizeMin, kIconSizeMax,
                    ClampInt(rawIconSize, kIconSizeMin, kIconSizeMax));
    }
    m_IconSize = ClampInt(rawIconSize, kIconSizeMin, kIconSizeMax);

    // 主题（D26-30）：[Theme] Name= 声明主题；主题文件的 [Colors] 会由 ConfigParser
    // 以「低优先级默认值」并入，故这里照常读 [Colors] 即可同时覆盖两种情况——
    // 主题提供默认值，Dock.ini 显式写出则优先。
    m_ThemeName  = cfg.ReadString(L"Theme",  L"Name",       m_ThemeName);
    m_PanelColor = cfg.ReadString(L"Colors", L"PanelColor", m_PanelColor);

    // 透明度自 D26-30 起归主题管辖：优先取主题/Dock.ini 的 [Colors] Transparency，
    // 未提供时兼容旧的 [Dock] Transparency（老配置文件），最后回退成员默认值。
    constexpr int kAlphaAbsent = INT_MIN;
    int rawAlpha = cfg.ReadInt(L"Colors", L"Transparency", kAlphaAbsent);
    if (rawAlpha == kAlphaAbsent)
    {
        rawAlpha = cfg.ReadInt(L"Dock", L"Transparency", m_Transparency);
    }
    if (rawAlpha < 0 || rawAlpha > 255)
    {
        LogWarningF(L"RainDeskPlus Dock: Transparency=%d 超出 0..255，已钳制为 %d",
                    rawAlpha, ClampInt(rawAlpha, 0, 255));
    }
    m_Transparency = ClampInt(rawAlpha, 0, 255);

    m_Position = ParsePosition(cfg.ReadString(L"Dock", L"Position", L"Bottom"));
    m_AutoHide = cfg.ReadBool(L"Dock", L"AutoHide", m_AutoHide);

    const int rawSpacing = cfg.ReadInt(L"Dock", L"Spacing", m_Spacing);
    if (rawSpacing < 0)
    {
        LogWarningF(L"RainDeskPlus Dock: Spacing=%d 为负，已钳制为 0", rawSpacing);
    }
    m_Spacing = (rawSpacing < 0) ? 0 : rawSpacing;

    const double maxScale  = cfg.ReadFloat(L"Dock", L"MaxScale", m_Magnify.GetMaxScale());
    const double maxRadius = cfg.ReadFloat(L"Dock", L"MaxRadius", m_Magnify.GetMaxRadius());
    if (maxScale >= 1.0) m_Magnify.SetMaxScale(static_cast<float>(maxScale));
    if (maxRadius > 0.0) m_Magnify.SetMaxRadius(static_cast<float>(maxRadius));

    ComputeLayout();

    // items.ini 与 Dock.ini 同目录；段顺序即项顺序，保证装载结果稳定（§7.4 尾注）。
    const std::wstring folder   = PathUtil::GetFolderFromFilePath(iniPath);
    const std::wstring itemsIni = folder + L"items.ini";

    ConfigParser itemsCfg;
    if (!itemsCfg.LoadFile(itemsIni)) return true;   // 无 items.ini 时仅加载 Dock 段

    for (const std::wstring& section : itemsCfg.GetSections())
    {
        DockItem item;
        // Id 缺失时以段名兜底，避免出现无名项。
        item.id          = itemsCfg.ReadString(section.c_str(), L"Id", section);
        item.name        = itemsCfg.ReadString(section.c_str(), L"Name", item.id);
        item.iconPath    = ResolveRelativePath(folder, itemsCfg.ReadString(section.c_str(), L"Icon", L""));
        item.targetPath  = itemsCfg.ReadString(section.c_str(), L"Target", L"");
        item.workingDir  = ResolveRelativePath(folder, itemsCfg.ReadString(section.c_str(), L"WorkingDir", L""));
        item.arguments   = itemsCfg.ReadString(section.c_str(), L"Arguments", L"");
        PathUtil::ExpandEnvironmentVariables(item.targetPath);

        if (!item.id.empty()) AddItem(item);
    }

    return true;
}

bool DockBar::SaveConfig(const std::wstring& iniPath) const
{
    // D31-35：统一走 ConfigParser::SaveFile 落盘（DD-7 例外取消），编码/行尾/键序由它保证。
    // 本函数职责：**同时**落 Dock.ini（[Theme] 声明 + [Dock] 6 字段）与同目录 items.ini
    // （每项 6 字段），与 LoadConfig 严格对称，调用方无需记住「要存两次」（§7.6.5）。
    // 仅落配置字段：运行态（isRunning / hWnd）不落库（§7.4 尾注）；Transparency 归主题，不写主文件。
    char maxScale[32]  = {};
    char maxRadius[32] = {};
    std::snprintf(maxScale,  sizeof(maxScale),  "%.2f", static_cast<double>(m_Magnify.GetMaxScale()));
    std::snprintf(maxRadius, sizeof(maxRadius), "%.2f", static_cast<double>(m_Magnify.GetMaxRadius()));

    ConfigParser cfg;
    cfg.SetHeaderComment(L"; RainDeskPlus Dock configuration (generated)");
    // 主题声明必须落在主文件里才能被 ConfigParser 识别（主题文件自身不再递归引用主题）。
    cfg.SetValue(L"Theme", L"Name", m_ThemeName);

    cfg.SetValue(L"Dock", L"IconSize", std::to_wstring(m_IconSize));
    // 不落 Transparency：自 D26-30 起该值由主题 [Colors] Transparency 管辖，
    // 若在此写死会遮住主题，导致 !Refresh 切换主题时透明度不再跟随。
    cfg.SetValue(L"Dock", L"Position", PositionName(m_Position));
    cfg.SetValue(L"Dock", L"AutoHide", m_AutoHide ? L"1" : L"0");
    cfg.SetValue(L"Dock", L"Spacing", std::to_wstring(m_Spacing));
    cfg.SetValue(L"Dock", L"MaxScale", StringUtil::WidenUTF8(maxScale));
    cfg.SetValue(L"Dock", L"MaxRadius", StringUtil::WidenUTF8(maxRadius));

    if (!cfg.SaveFile(iniPath)) return false;

    const std::wstring folder = PathUtil::GetFolderFromFilePath(iniPath);
    return WriteItemsIni(folder + L"items.ini", folder, m_Items);
}

}  // namespace raindock
