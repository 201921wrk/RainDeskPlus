# Dock 栏设计

> **状态**：D11-12 定稿（2026-09-14）｜D13-14 已落地｜D15-16 增补（2026-09-15）｜**产出角色**：架构师 Agent｜**对应路线图**：[ROADMAP.md](ROADMAP.md) `D11-12`/`D15`/`D16-17`
> **文档性质**：描述**目标架构（to-be）**。`Dock/` 下现有源码是 M1 阶段的**编译骨架（as-is）**逐轮落地的结果，
> 二者差异集中在 [§4.6 现状差异表](#46-现状差异表as-is--to-be) 与 [§10 待办与开放问题](#10-待办与开放问题)。
> **许可证**：Dock 为 RainDeskPlus 全新实现，GPL v2。设计思路参考 Cedro Modern Dock（GPL v3）与 RocketDock 的**交互形态**，
> **不复制其源码**，不使用 Nexus 专有代码。

---

## 0. 范围

| 项 | 内容 |
|----|------|
| 交付物 | 类设计（UML 文字）、核心接口（C++ 头文件框架）、模块依赖、与 Rainmeter 集成方式、关键实现思路 |
| 覆盖类 | `DockItem` / `DockMagnify` / `DockBar` / `DockWindow` / `UI::DuilibWindowBase` 适配层 / `Dock::IconExtractor`（D15 新增） |
| 不在范围 | 图标拖拽**排序**、皮肤/主题系统统一（Phase 2）、`!Dock*` Bang 命令（Phase 2） |
| 前置基线 | D10 已收尾：`vs2026-x64` 与 `vs2026-x64-dock-ui` 两条线 **0 error / 0 warning**，4 个 Smoke 全绿。D13-14 收尾：两线 0 error，6 个 Smoke 合计 146 断言 / 0 FAIL |

---

## 1. 目标功能

- 添加 / 移除 / 清空 / 去重图标项。
- **拖拽文件到 Dock 自动加项**（D15）：取路径 → 提取图标 → 追加项 → 重排 → 落库。
- 鼠标悬停放大效果（距离衰减 + 平滑过渡 + 每帧重排）。
- 点击启动应用（`ShellExecuteW`）；点击运行中项前置/最小化切换。
- 图标大小、透明度、停靠位置（上/下/左/右）、自动隐藏可配置。
- 显示运行中程序（任务管理风格）、最小化到 Dock。

**非目标**：图标拖拽**排序**（拖入项追加到尾部，不支持插入/交换位置）、托盘集成、插件式 Dock 项、多显示器差异化布局（均后续里程碑）。

---

## 2. 关键设计决策

| 编号 | 决策 | 理由 | 影响面 |
|------|------|------|--------|
| **DD-1** | `DockBar` 是**纯逻辑容器**，**不**继承 `DuilibWindowBase`（纠正旧稿 §2.2 注释「实际继承 Duilib 窗口基类」） | 项管理、布局、放大算法可脱离 UI 单测（D1/D2 验收不需要窗口）；窗口职责单一归属 `DockWindow`；保持 `Dock` 库零 Duilib 符号 | `DockBar.h` 不引入 UI 头 |
| **DD-2** | Duilib 依赖**只**出现在 `DockWindow`，并以 `#ifdef RAINDOCK_USE_DUILIB` 包裹；`RainDeskPlusDock` 默认不链接 `Duilib` | 维持当前 `RAINDOCK_USE_DUILIB=OFF` 的 0 error 基线；D13-14 可先在 OFF 下交付可测逻辑，再接 UI | `DockWindow.h/.cpp` 增加条件编译块；`CMakeLists.txt` 增加条件 link/define |
| **DD-3** | 适配层真实分支定为 `#include "UIlib.h"` + `using DuilibWindowBase = DuiLib::WindowImplBase;` | 上游真实文件名是 `third_party/duilib/DuiLib/Utils/WinImplBase.h`，且该头**不自包含任何依赖**（无 include），必须由 `UIlib.h` 提供上下文——`UIlib.h:69` 正是 `#include "Utils/WinImplBase.h"`；此方案与 `CMakeLists.txt:183` 注释一致 | `UI/DuilibWindowBase.h:16-17` 改为 `UIlib.h` + 限定名 |
| **DD-4** | 含 Duilib 头的 TU **必须**撤销目录级 `NOMINMAX`/`WIN32_LEAN_AND_MEAN` 并降 `/W0` | 目录级 `add_compile_definitions` 在**生成阶段**对目录内所有 target 生效（见 `CMakeLists.txt:56-65` 已记录的坑）：`StdAfx.h` 的 `#define MAX max` 在 `NOMINMAX` 下解析失败（C2672）；`UIIPAddress.cpp` 依赖 `winsock.h` 的 `WSADATA` | `CMakeLists.txt` 对 `Dock/DockWindow*.cpp` 加 `set_source_files_properties(... COMPILE_OPTIONS)`,见 [§5.3](#53-cmake-变更清单d13-14-前置) |
| **DD-5** | 放大**动画的时间推进**由 `DockBar::TickAnimation()` 承担，由 `DockWindow` 的 `WM_TIMER`（约 60ms）驱动 | 仅靠 `OnMouseMove` 无法完成 `cur → target` 的逐帧逼近；鼠标静止时也需继续收敛 | `DockBar` 增 `TickAnimation()`；`DockWindow` 增定时器 + `HandleMessage` 拦截 |
| **DD-6** | 布局计算收敛进 `DockBar::ComputeLayout()`，并对外暴露**只读**矩形/缩放比 | 悬停放大本质是「重排」而非「缩放」；必须有可断言的几何输出才能验收 D2 | `DockBar` 增 `GetItemRects()/GetItemScales()/GetBarRect()/GetPreferredSize()` |
| **DD-7** | 配置读写复用 `Library/ConfigParser`，不自造 INI 解析器 | `RainDeskPlusDock` 已 `PUBLIC` 链接 `RainDeskPlusCore`（含 `ConfigParser`），零新增依赖 | `DockBar` 增 `LoadConfig/SaveConfig` + `#include "ConfigParser.h"` |
| **DD-8** | 图标绘制走 Duilib `CRenderEngine::DrawImage`（GDI+），Dock **不**引入 Direct2D | 挂件 `Skin` 用 D2D 是因为 `Meter` 体系（Bar/Line 需矢量绘制）；Dock 图标是静态位图，Duilib 已有完整控件 + 图片管线（D5 已验证），避免维护两套渲染栈 | 不新增依赖；`DockWindow` 用 `CButtonUI` 或自绘 `CControlUI` |
| **DD-9** | 放大曲线保留**线性衰减**，替换点集中在 `DockMagnify` 单文件 | 线性最易解释与断言；将来换余弦/高斯只改一个 `.cpp`，调用方不变 | `DockMagnify.cpp` 已实现，D11-12 不改代码 |
| **DD-10** | **反转** UI 适配层的依赖方向：`RainDeskPlusDock` →(PUBLIC) `RainDeskPlusUI`；删除 `RainDeskPlusUI` → `RainDeskPlusDock` | 适配层是**基础设施**，被 `DockWindow`（Dock/）与将来的 `Skin.m_Window` 共同依赖，符合 [ARCHITECTURE.md](ARCHITECTURE.md) §5 的箭头方向（`DuilibWindowBase ←─ DockWindow`）。现有 CMake 反向链接形成「头文件 Dock→UI / 库 UI→Dock」的环 | `CMakeLists.txt:194-196` 删除；Dock 段增加 PUBLIC 链接 UI |
| **DD-11** | Dock 的**生命周期由 `App/main.cpp` 持有**，**不**塞进 `CRainmeter` | `CRainmeter` 是 Rainmeter 派生核心，改动它会持续扩大与上游的 diff；Dock 与挂件是并列的顶层对象 | 不改 `Library/Rainmeter.*`；`App/main.cpp` 增 Dock 启停 |
| **DD-12** | macro `RAINDOCK_USE_DUILIB=1` 必须**全局一致**（目录级定义），不得只加在单个 target | 适配层在同一份配置下要么是 `using` 别名、要么是占位类，**两者的 ODR 不同**；若 `Dock` 与 `UI` 两个 TU 用不同分支编译再链接，即为未定义行为 | `CMakeLists.txt` 在 Duilib 段之后加 `add_compile_definitions(RAINDOCK_USE_DUILIB=1)` |
| **DD-13** | 拖放的**业务入口**是平台无关的 `DockWindow::HandleFilesDropped(paths)`；`WM_DROPFILES` 只是 ON 线的**取路径适配器** | 与 DD-5/DD-6 同一思路：把「能断言的逻辑」与「只有窗口才能做的事」切开，D15 的加项/去重/落库可在 OFF 线单测，无需拖放操作 | `DockWindow` 增 `HandleFilesDropped`；`Start()` 增 `DragAcceptFiles`；`HandleMessage` 增 `WM_DROPFILES` 分支 |
| **DD-14** | 图标提取独立成 `Dock/IconExtractor`（platform-only，不含 Duilib），PNG 编码**复用** `Canvas::GetWICFactory()` + `Canvas::SaveBitmapToPng()` | `DockBar.h` 必须零平台/UI 依赖（DD-1），把 shell/WIC 头文件塞进去会破坏该约束；GDI+ 在当前代码库**从未 `GdiplusStartup`**（`gdiplus` 只是链路遗留），而 WIC 已被 `Canvas` 持有工厂，复用即零新增初始化与零新增依赖 | 新增 `Dock/IconExtractor.h/.cpp`（被 `Dock/*.cpp` 的 GLOB 自动纳入） |

---

## 3. 类设计（UML 文字描述）

### 3.1 类图

```
                    ┌──────────────────────────────────────────┐
                    │ DockWindow : DuilibWindowBase            │
                    │  — UI 宿主（唯一接触 Duilib 的类）        │
                    │  - m_DockBar      : DockBar              │
                    │  - m_LastMouse    : POINT                │
                    │  - m_AnimTimer    : UINT_PTR  (ON 模式)   │
                    │  + Start(iniPath) / Stop()               │
                    │  + HandleMouseMove/Leave/ItemClick(...)  │
                    └───────────────────┬──────────────────────┘
                                        │ 1 拥有 1（值成员，组合 ◆——）
                    ┌───────────────────▼──────────────────────┐
                    │ DockBar            （纯逻辑，无 UI 依赖） │
                    │  - m_Items   : vector<DockItem>          │
                    │  - m_Visuals : vector<ItemVisual>  1..n  │
                    │  - m_IconSize / m_Transparency           │
                    │  - m_Position / m_AutoHide / m_Spacing   │
                    │  - m_LastMouse : POINT                   │
                    │  + 项管理 / 布局 / 交互 / 配置 / 任务管理  │
                    └───────┬───────────────────────┬──────────┘
                            │ 0..* 聚合 ◇——          │ 1 拥有 1 组合 ◆——
            ┌───────────────▼──────────┐   ┌────────▼─────────────────┐
            │ DockItem  （纯数据 POD）  │   │ DockMagnify （纯算法）    │
            │  id/name/iconPath        │   │  - m_MaxScale  = 1.8f    │
            │  targetPath/workingDir   │   │  - m_MaxRadius = 150.0f  │
            │  arguments               │   │  + CalculateScale(pos,   │
            │  isRunning / hWnd        │   │                    rect) │
            └──────────────────────────┘   └──────────────────────────┘
```

**关系说明**

| 关系 | 双方 | 多重性 | 语义 |
|------|------|--------|------|
| 继承 | `DockWindow` ──▷ `DuilibWindowBase` | — | OFF 模式=占位基类；ON 模式=`DuiLib::WindowImplBase` |
| 组合 | `DockWindow` ◆── `DockBar` | 1 : 1 | 值成员，同生共死 |
| 组合 | `DockBar` ◆── `DockMagnify` | 1 : 1 | 值成员，无独立生命周期 |
| 聚合 | `DockBar` ◇── `DockItem` | 1 : 0..* | 值语义 `vector<DockItem>`；项由配置驱动，可增删 |
| 依赖 | `DockBar` ⇢ `ConfigParser` | — | 仅在 `LoadConfig/SaveConfig` 内使用 |

### 3.2 职责边界（单一职责）

| 类 | 负责 | **不**负责 |
|----|------|-----------|
| `DockItem` | 承载一项的配置数据 + 运行态快照 | 任何行为、任何绘制 |
| `DockMagnify` | 输入「鼠标点 + 基准矩形」→ 输出缩放比（纯函数风格，无状态副作用） | 动画插值、重排、重绘 |
| `DockBar` | 项集合管理、布局计算、放大目标解算、动画推进、配置读写、运行态刷新 | 窗口创建、消息循环、控件绘制、Duilib 类型 |
| `DockWindow` | 窗口创建/透明属性、控件构建、消息→`DockBar` 转发、`Invalidate` 节流 | 布局/放大/配置的业务逻辑 |

### 3.3 所有权与生命周期

```
App/main.cpp
   │ 1. 解析启动参数 → 定位 Dock.ini
   │ 2. DockWindow w;  w.Start(iniPath);
   │       ├─ DockBar.SetDefaults() → LoadConfig() → AddItem() × N → ComputeLayout()
   │       └─ [ON] Create() → InitWindow() → 建控件 + SetTimer(60ms) → ShowWindow()
   │ 3. 消息循环（[ON] CPaintManagerUI::MessageLoop）
   └─ 4. w.Stop() → DockBar.SaveConfig() → KillTimer → SendMessageW(WM_CLOSE) → CPaintManagerUI::Term()
```

> **关闭必须是同步的**（D36-40 修复）：DuiLib 的 `CWindowWnd::Close()` 内部是 `PostMessage(WM_CLOSE)`，
> 真正的窗口销毁要等到之后的某个消息循环；而调用方 `Stop()` 返回后立即 `CPaintManagerUI::Term()`
> 释放 `ImageHash` / `CustomFonts` / `StyleHash` 等共享资源。于是队列里残留的 `WM_CLOSE` 会被派发到
> 「共享资源已释放」的窗口上，`__WndProc → WM_NCDESTROY → OnFinalMessage → m_pm.ReapObjects()`
> 访问已释放资源，触发 `0xC0000005`。因此 `DockWindow::Stop()` 改用 `::SendMessageW(m_hWnd, WM_CLOSE, 0, 0)`：
> `Stop()` 返回时窗口必已销毁、`OnFinalMessage` 已在 DuiLib 仍有效的状态下执行完毕，
> 且 `DestroyWindow` 会顺带丢弃队列中该窗口的残留消息。

- `DockBar` 无堆资源（`vector` 自管理），无析构顺序陷阱。
- `ItemVisual` 与 `DockItem` **等长且同序**：任何 `m_Items` 的增删都必须同步 `m_Visuals`（在 `AddItem/RemoveItem/ClearItems` 内成对维护，见 §7.1 不变量）。

### 3.4 `DockItem` 运行态（文字状态机）

```
       ┌──────────┐  ShowRunningApps() 匹配到 targetPath   ┌────────────┐
       │  Idle    │ ─────────────────────────────────────▶ │  Running   │
       │isRunning=│ ◀───────────────────────────────────── │isRunning=1 │
       │false,hWnd│        进程退出 / 窗口销毁               │  hWnd≠0    │
       └──────────┘                                        └─────┬──────┘
                                                                  │ GetForegroundWindow()==hWnd
                                                                  ▼
                                                          ┌────────────────┐
                                                          │  Foreground    │
                                                          │（派生态，不落库）│
                                                          └────────────────┘
```

`Foreground` **不是** `DockItem` 的持久字段，而是每帧用 `GetForegroundWindow()` 与 `hWnd` 比较得到的派生结果，用于决定点击行为（前置 vs 最小化）与描边高亮。

---

## 4. 核心接口定义（C++ 头文件框架）

> 以下为**终态**接口。`DockItem.h` 与 `DockMagnify.h` 与现状一致（无需改动），故只列差异说明。

### 4.1 `Dock/DockItem.h`（终态，与现状一致）

```cpp
namespace raindock {

struct DockItem
{
    std::wstring id;          // 唯一标识（去重键、控件 name、配置段内 Id）
    std::wstring name;        // 显示名（Tooltip / 无障碍）
    std::wstring iconPath;    // 图标路径（PNG/ICO，相对 Dock 目录或绝对路径）
    std::wstring targetPath;  // 目标程序路径（运行态匹配的键）
    std::wstring workingDir;  // 工作目录（空 = 当前目录）
    std::wstring arguments;   // 启动参数（空 = 无参）
    bool         isRunning = false;
    HWND         hWnd = nullptr;   // 运行中程序的关联顶层窗口
};

}  // namespace raindock
```

**约定**
- `id` 非空且唯一；`AddItem` 以 `id` 为替换键。
- `isRunning/hWnd` 是**运行态快照**，不写入 `items.ini`（配置只存前 6 个字段）。
- 视觉状态（缩放/矩形）**不放进 `DockItem`**，见 `DockBar::ItemVisual`。

### 4.2 `Dock/DockMagnify.h`（终态，与现状一致）

```cpp
namespace raindock {

class DockMagnify
{
public:
    void  SetMaxScale(float s);      // 最大放大倍率，建议 1.2 ~ 2.5
    void  SetMaxRadius(float r);     // 影响半径（px），超出不放大
    float GetMaxScale()  const;
    float GetMaxRadius() const;

    // 返回 >= 1.0 的缩放比；dist >= m_MaxRadius 或 m_MaxRadius <= 0 时返回 1.0。
    float CalculateScale(const POINT& mousePos, const RECT& iconRect) const;

private:
    float m_MaxScale  = 1.8f;
    float m_MaxRadius = 150.0f;
};

}  // namespace raindock
```

**约定**：本类**只管静态衰减曲线**，不做平滑插值（插值属 `DockBar`，见 DD-5/DD-6）。

### 4.3 `Dock/DockBar.h`（终态 —— 主要变更点）

```cpp
namespace raindock {

enum class DockPosition { Top, Bottom, Left, Right };

class DockBar
{
private:
    // 每项的「视觉状态」。与 DockItem 分离：DockItem 是配置数据（落库），
    // ItemVisual 是每帧中间量（不落库）。两者**等长同序**。
    struct ItemVisual
    {
        RECT  rect{};         // 当前布局矩形（客户区坐标）
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

    // ---------- 配置持久化（DD-7：复用 Library/ConfigParser） ----------
    bool LoadConfig(const std::wstring& iniPath);    // Dock.ini + 同目录 items.ini
    bool SaveConfig(const std::wstring& iniPath) const;  // 写 Dock.ini（7 字段）+ items.ini（每项 6 字段）

    // ---------- 拖放加项（D15，见 §7.6） ----------
    std::wstring AddItemFromPath(const std::wstring& path, const std::wstring& dockFolder);
    bool         HasTarget(const std::wstring& targetPath) const;

    // ---------- 任务管理 ----------
    void ShowRunningApps();                  // 枚举顶层窗口 → 刷新 isRunning/hWnd
    void MinimizeToDock(HWND hWnd);

private:
    void ComputeLayout();                    // 依据 iconSize/spacing/position/target 重排 rect
    POINT ToBarAxis(const POINT& pt) const;  // 竖直 Dock 时交换 x/y，统一布局为一维

    std::vector<DockItem>   m_Items;
    std::vector<ItemVisual> m_Visuals;       // 与 m_Items 等长同序（不变量）
    int          m_IconSize     = 48;
    int          m_Transparency = 220;
    int          m_Spacing      = 8;
    DockPosition m_Position     = DockPosition::Bottom;
    bool         m_AutoHide     = false;
    DockMagnify  m_Magnify;
    POINT        m_LastMouse    = {0, 0};
    bool         m_MouseInside  = false;
};

}  // namespace raindock
```

**较现状的新增**：`FindItem`、`GetItemRects/GetItemScale/GetBarRect/GetPreferredSize`、`TickAnimation`、`SetSpacing/GetSpacing`、`LoadConfig/SaveConfig`、`m_Visuals`、`m_MouseInside`、私有 `ComputeLayout/ToBarAxis`。
**`ComputeLayout` 保持私有**：由构造与全部 setter 内部调用，调用方无需显式触发。

### 4.4 `Dock/DockWindow.h`（终态 —— 签名变更点）

```cpp
namespace raindock {

class DockWindow : public DuilibWindowBase
{
public:
    DockWindow();
    ~DockWindow() override;

    DockBar&       GetDockBar();
    const DockBar& GetDockBar() const;

    // ---------- 平台无关入口（OFF 模式亦可测：仅加载数据，不建窗口） ----------
    bool Start(const std::wstring& iniPath);   // 读配置 → [ON] 建窗口 + 起动画定时器
    void Stop();                              // [ON] 停表 + 同步 SendMessageW(WM_CLOSE)；始终 SaveConfig

    // DockBar 的转发入口（消息与测试都走这三个，避免 Duilib 类型外泄）
    void HandleMouseMove(const POINT& pt);
    void HandleMouseLeave();
    void HandleItemClick(const std::wstring& id);

    // 拖放加项入口（D15/DD-13）：OFF 线亦可测；返回实际新增的项数
    size_t HandleFilesDropped(const std::vector<std::wstring>& paths);

#ifdef RAINDOCK_USE_DUILIB
    // ---------- DuiLib::WindowImplBase 契约（真实 Duilib 分支） ----------
    // 注意：这些 override 只在 RAINDOCK_USE_DUILIB 下存在，OFF 模式不声明，
    //       因此不会与占位基类的接口冲突（DD-2）。
    CDuiString GetSkinFile() override;                  // 返回内联 XML（首字符 '<' 判定）
    LPCTSTR    GetWindowClassName() const override;      // _T("RainDeskPlusDockWnd")
    void       InitWindow() override;                    // 建图标控件、绑事件、SetTimer(60ms)
    void       Notify(TNotifyUI& msg) override;          // item click / mouseenter / mouseleave
    LRESULT    OnMouseMove(UINT, WPARAM, LPARAM, BOOL&) override;
    LRESULT    HandleMessage(UINT, WPARAM, LPARAM) override;  // 拦截 WM_TIMER（动画）与 WM_DROPFILES（拖放）
    void       OnFinalMessage(HWND hWnd) override;       // 解绑/清理

private:
    POINT    m_LastMouse{};
    UINT_PTR m_AnimTimer = 0;
#endif

private:
    DockBar      m_DockBar;
    std::wstring m_IniPath;   // Stop()/拖放后回写配置用
};

}  // namespace raindock
```

**类型限定约定**：`DockWindow.h` 内一律写 `DuiLib::` 限定（`DuiLib::CDuiString` / `DuiLib::TNotifyUI`），**不**在头文件里 `using namespace DuiLib;`；`using namespace DuiLib;` 只允许出现在 `DockWindow.cpp` 顶部（与 `Tests/test_duilib_hello.cpp:17` 一致）。

### 4.5 `UI/DuilibWindowBase.h`（终态 —— 修正 include）

```cpp
#pragma once

#ifdef RAINDOCK_USE_DUILIB
// 真实 Duilib：UIlib.h:69 已包含 Utils/WinImplBase.h，且该头不自包含任何依赖，
// 故必须由 UIlib.h 提供上下文（见 DD-3）。
#include "UIlib.h"
using DuilibWindowBase = DuiLib::WindowImplBase;
#else
// 占位基类：仅保留「可构造 + 可虚析构」，不再伪造 Duilib 回调签名。
// DockWindow/Skin 的 Duilib override 由各自的 #ifdef 提供（DD-2）。
class DuilibWindowBase
{
public:
    virtual ~DuilibWindowBase() = default;
};
#endif
```

> 现状差异：当前 OFF 分支还声明了 `InitWindow/Notify(void*)/OnClick(void*)/OnMouseMove(void*,void*)/Create()/Close()`，
> 而 ON 分支 `WindowImplBase` 并没有这些签名 → 一旦打开开关必编译失败。终态由「占位类只留析构 + 子类用 `#ifdef` 声明 override」解决。

### 4.6 现状差异表（as-is → to-be）

| 文件 | 现状 | 终态 | 归属 |
|------|------|------|------|
| `Dock/DockItem.h` | 8 字段 | 不变 | — |
| `Dock/DockMagnify.h` | 已完整 | 不变 | — |
| `Dock/DockBar.h` | 无布局/动画/配置接口；无 `m_Visuals` | 见 §4.3 | D13-14 |
| `Dock/DockBar.cpp` | `OnMouseMove/OnMouseLeave/ShowRunningApps/MinimizeToDock` 为 TODO | 实现 §7.1 / §7.3 | D13-14（+D16-17 放大） |
| `Dock/DockWindow.h` | `Notify(void*) override`、`OnMouseMove(void*,void*) override`、`m_DockBar` | 见 §4.4（签名不兼容，必须重写） | D13-14 |
| `Dock/DockWindow.cpp` | 三个回调占位转发 | 见 §7.2 / §7.5 | D13-14 |
| `UI/DuilibWindowBase.h` | 真实分支 include 错、OFF 分支伪造签名 | 见 §4.5 | D13-14 前置 |
| `CMakeLists.txt` | Dock 不链 Duilib；UI→Dock 反向链接 | 见 §5.3 | D13-14 前置 |

---

## 5. 模块依赖关系

### 5.1 target 依赖图

```
RainDeskPlus (exe, WIN32)
 ├─ RainDeskPlusCore   [Library/*.cpp + Common/*.cpp + PCRE]        ← 无 Duilib
 ├─ RainDeskPlusUI     [UI/*.cpp]  (适配层 = 基础设施)
 │     └─ PUBLIC → RainDeskPlusCore
 └─ RainDeskPlusDock   [Dock/*.cpp]
       ├─ PUBLIC → RainDeskPlusCore     (ConfigParser / PathUtil)
       ├─ PUBLIC → RainDeskPlusUI       (DuilibWindowBase)   ← DD-10：方向反转
       └─ PRIVATE → Duilib              ← 仅 RAINDOCK_USE_DUILIB=ON

Duilib (STATIC, C++14, /UNOMINMAX /UWIN32_LEAN_AND_MEAN)
 └─ 被 RainDeskPlusDuilibTest 与（ON 时）RainDeskPlusDock 消费
```

**删除的边**：`RainDeskPlusUI → RainDeskPlusDock`（现状 `CMakeLists.txt:194-196`），它使 `UI` 与 `Dock` 互相依赖。

### 5.2 开关矩阵

| `RAINDOCK_USE_DUILIB` | `RAINDOCK_BUILD_DOCK` | `Dock` 库 | `DockWindow` 分支 | 可验收里程碑 |
|---|---|---|---|---|
| **OFF**（默认） | ON | 不链 `Duilib`，无 `RAINDOCK_USE_DUILIB` 宏 | 占位：纯数据 + `Handle*` 转发 | D1、D2、D4（纯逻辑单测）；构建基线保持 0 error |
| **ON** | ON | `PRIVATE` 链 `Duilib` | 真实窗口 / 控件 / `WM_TIMER` | D1–D4 全部（含 D3 透明窗口） |
| OFF | OFF | 不参与构建 | 不参与构建 | 仅 Core/UI 基线 |
| ON | OFF | 不参与构建 | 不参与构建 | 仅 `RainDeskPlusDuilibTest` |

### 5.3 CMake 变更清单（D13-14 前置）

```cmake
# ① 开关宏全局一致（放在 Duilib 段之后、add_compile_options 之前或之后均可，
#    但必须在 L86 之后，避免污染上游 Duilib 库编译）——DD-12
if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
    add_compile_definitions(RAINDOCK_USE_DUILIB=1)
endif()

# ② Dock 段：反转适配层依赖 + 条件接入 Duilib
if(RAINDOCK_BUILD_DOCK)
    file(GLOB ... )                       # 维持现状
    add_library(RainDeskPlusDock STATIC ...)
    target_include_directories(RainDeskPlusDock PUBLIC ${CMAKE_SOURCE_DIR}/Dock)
    target_link_libraries(RainDeskPlusDock PUBLIC RainDeskPlusCore)
    if(TARGET RainDeskPlusUI)             # 适配层是底层，Dock 依赖它（DD-10）
        target_link_libraries(RainDeskPlusDock PUBLIC RainDeskPlusUI)
    endif()
    if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
        target_link_libraries(RainDeskPlusDock PRIVATE Duilib)
        # ③ 含 Duilib 头的 TU 必须撤销目录级 NOMINMAX/WIN32_LEAN_AND_MEAN 并降告警——DD-4
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/Dock/DockWindow.cpp
            PROPERTIES COMPILE_OPTIONS "/UNOMINMAX;/UWIN32_LEAN_AND_MEAN;/W0;/utf-8")
    endif()
endif()

# ④ 删除 UI 段中 target_link_libraries(RainDeskPlusUI PUBLIC RainDeskPlusDock)
```

> `UI/DuilibWindowBase.h` 是纯头文件适配层（`.cpp` 为空占位），Dock 仅需其头 + include 路径，
> 事实上 ② 中的 `PUBLIC RainDeskPlusUI` 也可退化为「只加 include 目录」；此处保留库级链接以表达层次意图。

---

## 6. 与 Rainmeter 原有模块的集成方式

| 既有模块 | 集成方式 | 修改点 | 说明 |
|----------|----------|--------|------|
| `Library/Rainmeter.*`（`CRainmeter`） | **不集成**（DD-11） | 无 | Dock 由 `App/main.cpp` 与挂件并列持有，避免持续扩大与上游的 diff |
| `Library/ConfigParser.*` | 直接复用（DD-7） | 无 | 读 `Dock.ini` / `items.ini`，见 §8 |
| `Library/Skin.*` | **不复用** | 无 | Dock 不属 `Skin` 体系；Dock 项不是 `Measure`/`Meter`，无 INI→工厂绑定需求 |
| `Library/CommandHandler.*` | 暂不集成 | 无 | `!DockAdd/!DockRemove` 等 Bang 列 Phase 2（[需要确认]） |
| `Common/PathUtil.*` | 直接复用 | 无 | 运行态匹配时规范化 exe 路径（大小写/反斜杠/短路径） |
| `UI/DuilibWindowBase.h` | **共同基类**（DD-3/DD-10） | include 修正 | 挂件窗口将来接入 Duilib 时与 `DockWindow` 共用此适配层 |
| 主题系统 | 扩展 | 无（Phase 2） | Dock 与挂件统一取色/字体/圆角；Dock 侧留 `Dock.ini [Colors]` 段位 |

---

## 7. 关键实现思路

### 7.1 悬停放大：每帧「重排」而非「缩放」

**不变量**：`m_Items.size() == m_Visuals.size()`，且下标一一对应。`AddItem/RemoveItem/ClearItems` 内成对维护。

```cpp
// DockBar::OnMouseMove —— 只解算 target，不做插值
void DockBar::OnMouseMove(const POINT& pt)
{
    m_LastMouse  = pt;
    m_MouseInside = true;

    for (size_t i = 0; i < m_Items.size(); ++i)
    {
        // 关键：用「未放大的基准矩形」求 target，而不是用上一帧已放大的 rect。
        // 否则 rect 变大→dist 变小→缩放更大→rect 更大，形成正反馈振荡。
        const RECT  base  = BaseRectOf(i);                         // iconSize 下的静态格位
        const float raw   = m_Magnify.CalculateScale(pt, base);
        m_Visuals[i].target = raw;                                  // 可能 = 1.0
    }
    ComputeLayout();   // 按 target 重新排布 m_Visuals[i].rect
}

// DockBar::ComputeLayout —— 沿 Dock 轴累加「缩放后的宽度 + 间距」，正交方向居中
void DockBar::ComputeLayout()
{
    const int icon = m_IconSize;
    int cursor = 0;
    for (size_t i = 0; i < m_Visuals.size(); ++i)
    {
        const int w = static_cast<int>(icon * m_Visuals[i].cur + 0.5f);
        m_Visuals[i].rect = { cursor, 0, cursor + w, icon };        // 一维化：统一按水平 Dock 计算
        cursor += w + m_Spacing;
    }
    // 竖直 Dock（Left/Right）在 ToBarAxis() 里把 x/y 换回；bar 厚度取 icon 的最大值
}

// DockBar::TickAnimation —— WM_TIMER 驱动（DD-5），返回是否仍需重绘
bool DockBar::TickAnimation()
{
    bool changed = false;
    for (auto& v : m_Visuals)
    {
        if (std::abs(v.target - v.cur) > 1e-3f)
        {
            v.cur += (v.target - v.cur) * 0.2f;                     // 一阶逼近，约 3 帧到 60%
            changed = true;
        }
        else v.cur = v.target;                                       // 吸附，避免无限次重绘
    }
    if (changed) ComputeLayout();
    return changed;
}
```

**性能**：仅当 `TickAnimation()` 返回 true 或鼠标移动时才 `Invalidate`。鼠标离开 Dock 影响矩形后 `OnMouseLeave()` 把全部 `target=1.0`，**但不停表**——`cur` 仍需逐帧平滑衰减回基准；动画表由 `DockWindow::HandleMessage` 在 `TickAnimation()` 返回 false（已收敛）时才 `KillTimer`，静止时 CPU 占用为 0。
**竖直 Dock**：`ToBarAxis()` 交换 x/y 后复用同一套一维布局代码，避免 4 份分支。

### 7.2 透明 / 异形窗口（Duilib）

1. **窗口创建**（`Start()` 内，ON 分支）：
   `Create(nullptr, _T("RainDeskPlus Dock"), UI_WNDSTYLE_FRAME, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE)`
   —— `WS_EX_TOOLWINDOW` 不进 Alt-Tab；`WS_EX_LAYERED` 提供逐像素 alpha；`WS_EX_NOACTIVATE` 创建即不激活（见 §7.2-4）。参考 `third_party/duilib/Demos/transwnd/MainWnd.h`。
2. **皮肤**：`GetSkinFile()` 返回**内联 XML**（`CDialogBuilder::Create` 以首字符 `<` 判定内联，D5 已验证），根 `<Window>` 的 `bkcolor` 用 8 位 `#AARRGGBB`；图标控件为 `<Button name="item:<id>">`，其 `name` 前缀即路由键。
3. **自动隐藏**：`WS_EX_TOPMOST` + `SetWindowPos` 移出可见边缘（保留 2px 触发带）；靠边缘进入。位置由 `GetPreferredSize()` 与 `SystemParametersInfo(SPI_GETWORKAREA)` 计算。
4. **不抢焦点**：`WS_EX_NOACTIVATE` + `ShowWindow(true, false)`（`SW_SHOWNOACTIVATE`），并在皮肤 XML 根节点加 `noactivate="true"` 进一步抑制 `CPaintManagerUI` 的 `SetFocus`——D3 已实测通过，保留该组合（备选方案「首次点击仅激活、二次点击才触发 item」未启用，见 §9 开放问题）。
5. **点击穿透**：Dock 矩形外的空白区不接收点击（窗口尺寸严格贴合 `GetBarRect()`）。

### 7.3 任务管理（`ShowRunningApps`）

```
EnumWindows(cb, this)
  ├─ !IsWindowVisible(hwnd)                        → skip
  ├─ GetWindowLongPtr(hwnd, GWLP_HWNDPARENT) != 0  → skip（只取顶层；SDK 无 GWL_OWNER 常量）
  ├─ GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW → skip
  ├─ GetWindowThreadProcessId(hwnd, &pid)
  ├─ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)   // RAII 句柄
  ├─ QueryFullProcessImageNameW(...) → exePath
  └─ 遍历 m_Items：PathUtil 规范化后比较 targetPath == exePath
        命中 → isRunning = true; hWnd = hwnd
未命中项 → isRunning = false; hWnd = nullptr
```

- 与 `DockItem::isRunning/hWnd` 的状态机对应（§3.4）。
- 触发时机：`Start()` 后首次 + `WM_TIMER` 低频（如每 1s，与 60ms 动画表分离，另开一个 timer ID）。
- `OnItemClick` 运行中分支：`GetForegroundWindow() == hWnd` → `ShowWindow(SW_MINIMIZE)`；否则 `ShowWindow(SW_RESTORE) + SetForegroundWindow`（补上现状代码里的 TODO）。
- 权限：`PROCESS_QUERY_LIMITED_INFORMATION` 可查大部分进程；系统级受保护进程 `OpenProcess` 失败即跳过（不报错）。

### 7.4 配置读写（`ConfigParser` 映射）

| INI | 键 | API | 字段 |
|-----|----|-----|------|
| `Dock.ini [Dock]` | `IconSize` | `ReadInt(section, key, 48)` | `m_IconSize`（再 clamp 16..128） |
| | `Transparency` | `ReadInt` | `m_Transparency`（clamp 0..255） |
| | `Position` | `ReadString` → `Top/Bottom/Left/Right` | `m_Position` |
| | `AutoHide` | `ReadBool` | `m_AutoHide` |
| | `Spacing` | `ReadInt` | `m_Spacing` |
| | `MaxScale` / `MaxRadius` | `ReadFloat` | `m_Magnify` |
| `items.ini [ItemN]` | `Id/Name/Icon/Target/WorkingDir/Arguments` | `GetSections()` + `ReadString` | 逐个 `AddItem` |

- `LoadConfig` 用 `GetSections()` 顺序遍历段（保证项顺序稳定），`Id` 缺失则以段名兜底。
- `SaveConfig` 写 `Dock.ini` 的 **7 个**配置字段，并**同时**写同目录 `items.ini`（每项 6 个字段，§7.6.5）；运行态（`isRunning/hWnd`）**不落库**。
- `Icon` 相对路径以 `Dock.ini` 所在目录为基准解析（`PathUtil`）。

### 7.5 事件流

```
鼠标移动   WM_MOUSEMOVE → DockWindow::OnMouseMove
              → DockBar::OnMouseMove(pt)   [更新 target + ComputeLayout]
              → 若 target 变化则 SetTimer 启动动画
动画       WM_TIMER(60ms) → DockWindow::HandleMessage
              → DockBar::TickAnimation()   [cur→target]
              → 变化 → m_pm.Invalidate() → Duilib 重绘各图标控件（DoPaint 按 ItemVisual.rect 画图标）
鼠标离开   DUI_MSGTYPE_MOUSELEAVE → DockWindow::Notify
              → DockBar::OnMouseLeave()    [target=1.0，KillTimer]
点击       DUI_MSGTYPE_CLICK → DockWindow::Notify
              → 取 sender->GetName() = "item:<id>" → DockBar::OnItemClick(id)
              → 未运行：ShellExecuteW   |   运行中：SW_RESTORE / SW_MINIMIZE 切换
拖入文件   WM_DROPFILES → DockWindow::HandleMessage（§7.6）
              → DragQueryFileW × N → paths[]
              → DockWindow::HandleFilesDropped(paths)
              → DockBar::AddItemFromPath(path, folder) × N   [去重 / 派生 id·name / 提图标]
              → 新增项各建一个 Duilib Button 控件 → SyncItemRects
              → 窗口尺寸自适应 → SaveConfig（Dock.ini + items.ini）
```

### 7.6 图标拖拽添加（D15）

目标：把「资源管理器里拖来的文件」变成一个 Dock 项——**取路径 → 去重 → 派生元数据 → 提取图标 → 追加项 → 重排 → 落库**。

#### 7.6.1 契约与分层（DD-13）

```
[ON 线] WM_DROPFILES (HDROP)
           └─ DragAcceptFiles(m_hWnd, TRUE)   ← Start() 时注册
           └─ HandleMessage: DragQueryFileW 循环取路径 → DragFinish
                    │  std::vector<std::wstring> paths
                    ▼
[两条线共用] DockWindow::HandleFilesDropped(paths)   ← 业务入口，OFF 可测
                    │
                    ├─ 每路径：DockBar::AddItemFromPath(path, dockFolder)
                    │            ├─ IsFile / 规范化 → 已存在同 targetPath ? 跳过
                    │            ├─ 派生 id（唯一化）/ name
                    │            ├─ IconExtractor::ExtractToPng(...) → iconPath
                    │            └─ AddItem(item)   ← 复用现有入口（同 id 替换 / 否则追加）
                    ├─ [ON] 为新增项动态建 Button 控件 + SyncItemRects + 窗口自适应
                    └─ m_IniPath 非空 → SaveConfig(m_IniPath)   ← 立刻持久化
```

**为什么把 `HandleFilesDropped` 放 `DockWindow` 而非 `DockBar`**：它需要「Dock 目录」这一部署信息（图标落盘位置、items.ini 位置），这正是 `DockWindow` 已持有的 `m_IniPath` 上下文；而 `DockBar` 一旦读配置就必须显式传入路径，再存一份成员会让「纯逻辑」这个约束变模糊。

#### 7.6.2 接口

```cpp
// Dock/DockBar.h（DD-1：仍是纯逻辑，只多一个「按路径加项」入口）
    // ---------- 拖放加项（D15） ----------
    // 成功返回新项 id；路径不合法 / 已存在同 targetPath 时返回空串。
    // dockFolder 决定图标落盘位置（<dockFolder>Icons\<id>.png）。
    std::wstring AddItemFromPath(const std::wstring& path, const std::wstring& dockFolder);
    bool         HasTarget(const std::wstring& targetPath) const;   // 去重判据（规范化后比较）

// Dock/DockWindow.h（DD-13：平台无关，OFF 线亦可测）
    // 拖入 N 个文件 → 返回实际新增（去重后）的项数；任一失败不影响其余。
    size_t HandleFilesDropped(const std::vector<std::wstring>& paths);

// Dock/IconExtractor.h（DD-14：platform-only，无 Duilib 依赖）
namespace raindock {
class IconExtractor
{
public:
    // 从 path 提取图标并写成 PNG 到 outPngPath（目录不存在时创建）。
    // .png/.jpg/.jpeg/.bmp/.gif 视作图标文件本身（直接落盘），其余走 shell 提取。
    static bool ExtractToPng(const std::wstring& path, const std::wstring& outPngPath);
};
}  // namespace raindock
```

#### 7.6.3 元数据派生规则

| 字段 | 规则 |
|------|------|
| `targetPath` | `PathUtil::ExpandEnvironmentVariables` + `GetFullPathNameW` 规范化（复用 `DockBar.cpp` 内的 `NormalizePath`） |
| `id` | 取文件名（去扩展名）→ 转小写 → 非 `[a-z0-9_-]` 一律替换为 `_`；结果为空则以 `item` 兜底 |
| `name` | 文件名（去扩展名），**保留原始大小写** |
| `workingDir` | 该文件所在目录（`PathUtil::GetFolderFromFilePath`） |
| `arguments` | 空 |
| `iconPath` | `<dockFolder>Icons\<id>.png`（相对 `Dock.ini` 目录写入 items.ini，故 `Icon=Icons\<id>.png`） |

**去重（两个维度，缺一不可）**：

1. **同 `targetPath`** → **跳过**（返回空 id）。重复拖同一个 exe 不应产生第二项，也不应覆盖用户已改过的 `Name`。
2. **`id` 冲突但 `targetPath` 不同** → **改名不改路径**：依次追加 `_2`、`_3`… 直到 `FindItem(id) == nullptr`。例：`chrome.exe` 与 `C:\Other\chrome.exe` → `chrome` / `chrome_2`。

> 与 `AddItem` 的「同 id 替换」语义刻意区分：`AddItem` 是配置装载路径（按作者意图改写），`AddItemFromPath` 是用户交互路径（不静默毁掉既有项）。

#### 7.6.4 图标提取策略（`IconExtractor`）

| 拖入文件类型 | 处理 |
|--------------|------|
| `.png` / `.jpg` / `.jpeg` / `.bmp` / `.gif` | 该文件**本身就是图标**：直接复制为 `<dst>`（不做 shell 提取，避免把图片当成「PNG 文件类型图标」） |
| `.ico` | `LoadImageW(nullptr, path, IMAGE_ICON, size, size, LR_LOADFROMFILE)` |
| 其余（`.exe` / `.lnk` / 目录快捷方式…） | `SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX)` → `SHGetImageList(SHIL_JUMBO /*256*/)` → `IImageList::GetIcon`；失败则回退 `SHGetFileInfoW(..., SHGFI_ICON | SHGFI_LARGEICON)` |

拿到 `HICON` 后统一走同一条落盘路径（**不引入 GDI+ 初始化**，DD-14）：

```
1. 建 32bpp 顶朝下 DIB，边长 kIconPx = 128（< 256 的 jumbo，保证缩放而非放大）
2. CreateCompatibleDC + SelectObject(DIB) + DrawIconEx(..., DI_NORMAL)   ← 由 GDI 完成缩放
3. 读 DIB 像素（BGRA，premultiplied 由 DrawIconEx 保证）
4. Canvas::GetWICFactory()->CreateBitmapFromMemory(GUID_WICPixelFormat32bppPBGRA, ...)
5. Canvas::SaveBitmapToPng(bitmap, outPngPath)
6. DestroyIcon / DeleteDC / DeleteObject —— 逐条与创建配对
```

**为什么是 PNG 而不是 ICO**：`DockItem::iconPath` 注释即「PNG/ICO」；Duilib 的图片管线用 `stb_image`（`third_party/duilib/DuiLib/Utils/stb_image.h`），原生支持 PNG 且带 alpha；PNG 无需维护 ICO 目录结构，落盘即用。

**失败降级**：任一步失败 → `iconPath` 留空、`AddItem` **照常执行**（项仍可用，只是无图标）。失败原因记 `LogWarningF`，**不**因图标问题丢弃用户拖入的项。

#### 7.6.5 items.ini 写回（DD-7 例外已取消）

D31-35 起 `Library/ConfigParser` 补齐了落盘与删除接口（`SaveFile` / `LoadFileRaw` / `SetHeaderComment` / `RemoveValue` / `RemoveSection`），因此**不再**需要裸 `std::ofstream`——DD-7 的既定例外**作废**，`DockBar::SaveConfig` 与 `WriteItemsIni` 统一改走 `ConfigParser`（`DockBar.cpp` 已移除 `<fstream>`）。

- `DockBar::SaveConfig(iniPath)` 的职责是**同时落两个文件**：`iniPath`（`Dock.ini`，7 个 `[Dock]` 字段）+ 同目录 `items.ini`（每项一段，6 个字段）。这样 `LoadConfig/SaveConfig` 严格对称，调用方（`DockWindow::Stop()`）无需记住「要存两次」。
- 段名固定为 `[Item1] [Item2] …`（按 `m_Items` 顺序），保证往返稳定。
- 只写 6 个**配置**字段（`Id/Name/Icon/Target/WorkingDir/Arguments`）；运行态 `isRunning/hWnd` **不落库**。
- 编码 UTF-8 **无 BOM**、行尾 `CRLF`，与 `Dock.ini` 完全一致（`ConfigParser::SaveFile` 内部经 `ToUtf8`）。
- **保序口径**：`ConfigParser` 内部的 `m_Sections` 是 `std::map`（字典序），故另存 `m_SectionOrder`（段出现序）与 `m_KeyOrder`（段内键插入序）；`SaveFile` 严格按插入序输出，从而生成文件与既有手写格式**逐字节一致**（首行注释经 `SetHeaderComment` 保留）。
- `Icon` 写成**相对 `Dock.ini` 目录**的路径（与 `LoadConfig` 的 `ResolveRelativePath` 对称）；`Target` 写成绝对路径。


```ini
; RainDeskPlus Dock items (generated)
[Item1]
Id=notepad
Name=notepad
Icon=Icons\notepad.png
Target=C:\Windows\notepad.exe
WorkingDir=C:\Windows
Arguments=
```

#### 7.6.6 ON 线：DOM 增量而非整树重建

`GetSkinFile()` 的控件树是**在窗口创建时一次性**从 `m_DockBar.GetItems()` 生成的（`CDialogBuilder` 只在 `Create()` 时跑一次）。拖放加项后**不能**只改 `DockBar` 就指望界面出现新图标，必须补控件：

| 方案 | 取舍 |
|------|------|
| ① 重建内联 XML（`ReapObjects` + 重新 `InitControls`） | 需自行管理旧控件回收与 notifier 解绑，且会丢失当前 hover/动画中间量 → **不采用** |
| ② **增量 `new CButtonUI` 挂到 `dock_root`**（采用） | `CContainerUI::Add()` 内部会 `m_pManager->InitControls(pControl, this)`（`UIContainer.cpp:92`），等价于 XML 建出的控件；只需对新控件再跑一遍 `SyncItemRects` |

新增控件的属性与 XML 模板逐字段对齐（`GetSkinFile()` ↔ 动态创建，二者必须保持等价）：

| XML 属性 | 动态 API |
|----------|----------|
| `name="item:<id>"` | `SetName(ItemControlName(id))` |
| `richevent="true"` | `SetRichEvent(true)` |
| `float="true"` | `SetFloat(true)` |
| （后续 `SetBkImage` / `SetToolTip`） | 复用现有 `BindItems` 的同一套调用 |

新增完毕后的刷新序列：

```
SyncItemRects(m_pm, m_DockBar)              // 拉伸/定位全部项（含新控件）
sz = m_DockBar.GetPreferredSize()
SetWindowPos(m_hWnd, 0,0, sz.cx, sz.cy, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)
m_pm.SetInitSize(sz.cx, sz.cy)              // 与 GetSkinFile() 的 size 属性同源，防下次 ReloadSkin 回退
PlaceDockWindow(m_hWnd, m_DockBar)          // 变宽后重新贴边居中
m_pm.Invalidate()
```

> `GetSkinFile()` 的根 `<Window size=...>` 只决定**创建时**的初始尺寸（`SetInitSize`），运行时变宽必须显式 `SetWindowPos`——这是 D3「窗口尺寸 == `GetPreferredSize()`」断言在 D15 之后的加强版：**拖入项后该等式仍须成立**。

#### 7.6.7 失败与边界

| 情形 | 行为 |
|------|------|
| 拖入非文件（目录） | 跳过并在返回计数中不计；目录支持归 Phase 2 [需要确认] |
| 路径无法规范化（`GetFullPathNameW` 失败） | 跳过，`LogWarningF` |
| 同一 `HDROP` 里含重复路径 | 第 2 次被「同 `targetPath`」规则挡掉，天然幂等 |
| `Icons\` 目录不可写 | 图标留空、项仍加入（§7.6.4 降级） |
| `m_IniPath` 为空（未配置路径的裸测试） | 只改内存模型，不落库 |
| 拖入时 `m_DockBar` 已处于悬停放大态 | `AddItem` 内 `ComputeLayout()` 会按当前 `cur` 重排；新项 `cur=target=1.0`，不影响既有动画 |

---

## 8. 配置格式规范

`Skins/<theme>/Dock.ini`（或 `Dock/Dock.ini`）

```ini
[Dock]
IconSize=48
Transparency=220
Position=Bottom
AutoHide=0
Spacing=8
MaxScale=1.8
MaxRadius=150
```

`items.ini`（与 `Dock.ini` 同目录）

```ini
[Item1]
Id=notepad
Name=记事本
Icon=notepad.png
Target=C:\Windows\notepad.exe
WorkingDir=
Arguments=

[Item2]
Id=explorer
Name=资源管理器
Icon=explorer.png
Target=C:\Windows\explorer.exe
Arguments=/e,
```

**约束**：`Id` 非空且全局唯一（重复则后者替换前者）；`Position` ∈ `Top|Bottom|Left|Right`；数值键超范围时按 §4.3 的 clamp 规则收敛并记 `[WARN]`。

**目录布局（D15 起）**：拖放提取的图标落在 `Dock.ini` 同目录的 `Icons\` 子目录，故 `Icon=` 写成相对路径（§7.6.3/§7.6.4）：

```
Skins/example/
├─ Dock.ini          ; [Dock] 7 字段
├─ items.ini         ; 每项一段，6 字段；由 DockBar::SaveConfig 生成
└─ Icons/
   ├─ notepad.png    ; 128×128 PNG（Dock 图标源）
   └─ explorer.png
```

> 已存在的 `Skins/example/skin.ini` 与 `Icons/` 互不影响：`SkinRegistry::Refresh` 只按**子目录**登记 config（优先 `skin.ini`），Dock 的配置文件路径由 `App` 显式传入，不参与该登记（§10 开放问题）。

---

## 9. 验证里程碑

| 里程碑 | 验收标准 | 载体 | 开关要求 | 状态 |
|--------|----------|------|----------|------|
| **D1** | `AddItem` 同 id 替换 / `RemoveItem` / `ClearItems` 正确；`OnItemClick`（未运行项）能启动 `notepad`；`m_Items` 与 `m_Visuals` 始终等长同序 | `Tests/test_dock_core.cpp`（纯断言 + `ShellExecute` 后 `FindWindow` 轮询） | 无（OFF 即可） | [x] D13-14 |
| **D2** | `DockMagnify`：dist=0 → 1.8；dist ≥ radius → 1.0；区间内单调递减；`ComputeLayout` 后相邻 rect 不重叠且总宽 = Σ(icon×scale)+间距 | 同上（纯几何断言，无窗口） | 无 | [x] D13-14 |
| **D3** | 透明/异形窗口在 Duilib 下可见、`WS_EX_TOOLWINDOW` 不在 Alt-Tab、不抢焦点；窗口尺寸 == `GetPreferredSize()` | `Tests/test_dock_window.cpp`（仿 `test_duilib_hello.cpp` 的有界消息泵） | `RAINDOCK_USE_DUILIB=ON` | [x] D13-14（实测 104×48） |
| **D4** | `ShowRunningApps()` 能枚举运行中程序并按 `targetPath` 命中，`isRunning/hWnd` 正确置位与复位 | `Tests/test_dock_core.cpp` | 无 | [x] D13-14 |
| **D15** | `AddItemFromPath` 派生 `id/name/targetPath` 正确、同 `targetPath` 去重、`id` 冲突改名；`IconExtractor` 产出可解码 PNG；`SaveConfig` 写回的 `items.ini` 能被 `LoadConfig` 原样读回（往返一致） | 新增 `Tests/test_dock_drop.cpp`（OFF 线，纯断言 + 文件往返） | 无 | [x] D15（2026-09-15） |
| **D16** | 真实程序（ON 线）启动后可见 Dock；悬停图标放大、移出复位 | `App/main.cpp` 持有 `DockWindow`（DD-11）+ `Skins/example/Dock.ini` | `RAINDOCK_USE_DUILIB=ON` | [x] D16（2026-09-15，ON 线 Smoke `dock-visible` + `dock-resident-exit` 通过） |
| **回归** | 两条构建线 `vs2026-x64` / `vs2026-x64-dock-ui` **0 error / 0 warning**；`RainDeskPlusRenderTest` / `SkinTest` / `BangTest` / `DockCoreTest` / `DuilibTest` / `DockWindowTest` 全绿 | D10 既有设施 | 两条都要 | 现状成立，须保持 |

---

## 10. 待办与开放问题

### D13-14 前置清单（按依赖顺序）—— 已全部落地（2026-09-15）

1. `UI/DuilibWindowBase.h`：真实分支改 `#include "UIlib.h"` + `using DuilibWindowBase = DuiLib::WindowImplBase;`；OFF 分支只保留虚析构（§4.5）。
2. `CMakeLists.txt`：删除 `RainDeskPlusUI → RainDeskPlusDock`；`Dock` 改 `PUBLIC RainDeskPlusUI`（DD-10）。
3. `CMakeLists.txt`：`if(RAINDOCK_USE_DUILIB AND TARGET Duilib) add_compile_definitions(RAINDOCK_USE_DUILIB=1)`（DD-12）。
4. `CMakeLists.txt`：`Dock` 条件 `PRIVATE Duilib` + 对 `Dock/DockWindow.cpp` 加 `/UNOMINMAX /UWIN32_LEAN_AND_MEAN /W0 /utf-8`（DD-4）。
5. `Dock/DockBar.h/.cpp`：按 §4.3 补齐布局 / 动画 / 配置 API 与实现（§7.1、§7.4）。
6. `Dock/DockWindow.h/.cpp`：删除 `Notify(void*)`/`OnMouseMove(void*,void*)`，改 §4.4 签名；`.cpp` 顶部 `using namespace DuiLib;`（§7.2、§7.5）。
7. `Dock/DockBar.cpp`：实现 `ShowRunningApps` / `MinimizeToDock` / 点击前置-最小化切换（§7.3）。
8. 新增 `Tests/test_dock_core.cpp`（D1/D2/D4）与 `Tests/test_dock_window.cpp`（D3），并在 `CMakeLists.txt` 注册。

### D15-16 前置清单（按依赖顺序）

1. `Dock/IconExtractor.h/.cpp`：新增（DD-14），`ExtractToPng(path, outPngPath)`；复用 `Canvas::GetWICFactory()` + `Canvas::SaveBitmapToPng()`（§7.6.4）。
2. `Dock/DockBar.h/.cpp`：增 `AddItemFromPath(path, dockFolder)` + `HasTarget(targetPath)`；`id/name` 派生与去重规则见 §7.6.3。
3. `Dock/DockBar.cpp`：`SaveConfig` 扩展为同时写 `items.ini`（§7.6.5）。
4. `Dock/DockWindow.h/.cpp`：增 `HandleFilesDropped(paths)`（DD-13）；`Start()` 加 `::DragAcceptFiles(m_hWnd, TRUE)`；`HandleMessage` 加 `WM_DROPFILES` 分支（`DragQueryFileW` 循环 + `DragFinish`）；ON 线增量建控件 + 窗口自适应（§7.6.6）。
5. `Tests/test_dock_drop.cpp`：新增 OFF 线 Smoke（D15）并在 `CMakeLists.txt` 的 `if(RAINDOCK_BUILD_DOCK)` 内注册。
6. `Skins/example/Dock.ini` + `Skins/example/items.ini`：新增默认配置（`MaxScale=1.8` / `MaxRadius=150`），使 ON 线首启即可见悬停放大。
7. `App/main.cpp`：按 DD-11 持有 `DockWindow`（`Start(<Skins 下 Dock.ini>)` / 退出前 `Stop()`），以 `#ifdef RAINDOCK_BUILD_DOCK` 保护。
8. 回归：两线全量构建 + 全量 Smoke + 归档 `logs/build_*.log`。

### 开放问题

- **[需要确认]** `WS_EX_NOACTIVATE` 与 `CPaintManagerUI` 焦点/键盘消息的兼容性 → D3 已实测通过（`WS_EX_NOACTIVATE` 保留）。
- **[需要确认]** 「显示运行中程序」的口径：本设计按 **叠加式**（用户配置项 + 运行态标记），不做 Nexus 式的自动填充；若需自动列出全部运行程序，需额外定义「临时项」的增删与排序规则。
- **[需要确认]** 是否引入 `!Dock*` Bang（扩展 `CommandHandler`）→ 建议归属 Phase 2。
- **[需要确认]** 高 DPI 与多显示器：`Skin` 现状为 96 DPI SOFTWARE 渲染目标，Dock 若先做 DPI 感知会与挂件不一致 → 建议 Phase 2 统一处理，D11-12 按 96 DPI 设计。
- **[需要确认]** `Dock.ini` 的归属：`Skins/<theme>/Dock.ini`（随主题）还是 `Dock/Dock.ini`（随模块）。本设计按「同目录 `items.ini`」实现，路径由 `App` 传入，两种布局均可承载。
