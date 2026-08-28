# Dock 栏设计

> Dock 为全新实现（Rainmeter 无此功能）。设计参考 Cedro Modern Dock（GPL v3）与 RocketDock，不使用专有 Nexus 源码。对应路线图 Phase 1 末尾 / Phase 2。

## 1. 目标功能

- 添加 / 移除 / 清空图标项。
- 鼠标悬停放大效果（距离衰减）。
- 点击启动应用（`ShellExecute`）。
- 图标大小、透明度、停靠位置、自动隐藏可配置。
- 显示运行中程序（任务管理风格）、最小化到 Dock。

## 2. 类设计

### 2.1 DockItem

```cpp
// Dock/DockItem.h
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
```

### 2.2 DockBar

```cpp
// Dock/DockBar.h
enum class DockPosition { Top, Bottom, Left, Right };

class DockBar   // 实际继承 Duilib 窗口基类：class DockBar : public DuilibWindowBase
{
public:
    // 项管理
    void AddItem(const DockItem& item);
    void RemoveItem(const std::wstring& id);
    void ClearItems();
    const std::vector<DockItem>& GetItems() const;

    // 交互
    void OnMouseMove(const POINT& pt);
    void OnMouseLeave();
    void OnItemClick(const std::wstring& id);

    // 配置
    void SetIconSize(int size);          // 16..128 px
    void SetTransparency(int alpha);     // 0..255
    void SetPosition(DockPosition pos);
    void SetAutoHide(bool enable);

    // 窗口/任务管理
    void ShowRunningApps();
    void MinimizeToDock(HWND hWnd);

private:
    std::vector<DockItem> m_Items;
    int          m_IconSize = 48;
    int          m_Transparency = 220;
    DockPosition m_Position = DockPosition::Bottom;
    bool         m_AutoHide = false;
    DockMagnify  m_Magnify;   // 放大效果计算器
};
```

### 2.3 DockMagnify（放大效果算法）

参考 RocketDock：计算鼠标到图标中心距离，距离越近缩放越大；超过影响半径不放大。

```cpp
// Dock/DockMagnify.h
class DockMagnify
{
public:
    void SetMaxScale(float s) { m_MaxScale = s; }      // 例如 1.8
    void SetMaxRadius(float r){ m_MaxRadius = r; }     // 例如 150.0f

    float CalculateScale(const POINT& mousePos,
                         const RECT& iconRect) const;
private:
    float m_MaxScale  = 1.8f;
    float m_MaxRadius = 150.0f;
};
```

实现见 `Dock/DockMagnify.cpp`，伪代码：

```cpp
float dx = mousePos.x - (iconRect.left + iconRect.right) / 2.0f;
float dy = mousePos.y - (iconRect.top + iconRect.bottom) / 2.0f;
float dist = std::sqrt(dx*dx + dy*dy);
if (dist > m_MaxRadius) return 1.0f;
return 1.0f + (m_MaxScale - 1.0f) * (1.0f - dist / m_MaxRadius);
```

### 2.4 DockWindow

Duilib 窗口基类派生，承载 `DockBar` 渲染与透明/异形窗口特性。当 `RAINDOCK_USE_DUILIB=OFF` 时退化为一个空壳类（仅占位，便于先编译 Dock 逻辑）。

```cpp
// Dock/DockWindow.h
class DockWindow : public DuilibWindowBase
{
public:
    DockBar& GetDockBar();
    // Duilib 生命周期回调（Create / OnLButtonDown / OnMouseMove / OnClick）
};
```

## 3. 与 Rainmeter 核心的关系

- Dock 与挂件**并列**，都由 `CRainmeter` 生命周期管理，但 Dock 不属于 `Skin` 体系。
- `App/main.cpp` 初始化 `CRainmeter` 后，额外创建 `DockWindow`。
- 主题系统统一覆盖 Dock 与挂件（颜色 / 字体 / 圆角）。

## 4. 关键算法：悬停放大

- 每帧 `OnMouseMove` 触发：遍历可见 `DockItem`，用 `DockMagnify::CalculateScale` 得到各自缩放比，重排并重绘。
- 平滑过渡：对每个图标维护当前缩放 `cur`，按帧向目标缩放 `target` 逼近（`cur += (target-cur)*0.2`），避免突变。
- 性能：仅鼠标在 Dock 影响矩形内时重绘；离开 `OnMouseLeave` 全部回弹到 1.0。

## 5. 任务管理（参考 Nexus）

- 枚举顶层窗口（`EnumWindows` + `IsWindowVisible` + `GetWindowLong(GW_OWNER)`），匹配可执行路径（`QueryFullProcessImageName`）。
- 点击已运行项 → 前置或最小化（`ShowWindow`）。
- 关闭按钮 → `PostMessage(WM_CLOSE)`。

## 6. 配置存储

- Dock 配置存于 `Skins/<theme>/Dock.ini`（INI，复用 `ConfigParser`）。
- 项列表存于 `Dock/items.ini`：`[Item1]`、`[Item2]` ... 每段一个 `DockItem`。

## 7. 验证里程碑

- [ ] D1：`DockBar` 添加/移除项 + 点击启动 `notepad`。
- [ ] D2：`DockMagnify` 悬停放大效果生效。
- [ ] D3：透明/异形窗口在 Duilib 下工作。
- [ ] D4：枚举运行中程序并显示。
