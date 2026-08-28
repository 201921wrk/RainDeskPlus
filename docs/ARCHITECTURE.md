# RainDeskPlus 架构设计

> 本文为「架构师 Agent」阶段产出，描述 RainDeskPlus 的总体架构、核心类设计、模块依赖与 Rainmeter 集成方式。对应路线图 Phase 1。

## 1. 设计目标

1. **派生而非重写**：核心引擎（Measure / Meter / Skin / ConfigParser / CommandHandler）直接复用 Rainmeter GPL v2 源码，保留版权声明。
2. **新增 Dock 模块**：Rainmeter 无 Dock 功能，Dock 为全新实现，不使用专有 Nexus 源码。
3. **UI 统一于 Duilib**：用 Duilib 替换 Rainmeter 的原生 Win32 窗口体系，挂件与 Dock 共用窗口基类。
4. **插件兼容**：保留 Rainmeter 插件 DLL 加载机制（导出 `Initialize / Reload / Update / Finalize`）。
5. **可独立编译的骨架**：当前阶段每个模块以骨架形式存在，工具链与 Rainmeter 源码就位后逐步填实。

## 2. 总体架构层次

```
┌──────────────────────────────────────────────────────────────┐
│                    App (main.cpp)                            │
│        入口：初始化 CRainmeter、加载皮肤、创建 Dock 窗口       │
└────────────────────────────┬─────────────────────────────────┘
                             │ 依赖
        ┌────────────────────┼────────────────────┐
        ▼                    ▼                    ▼
┌────────────────┐  ┌────────────────┐  ┌──────────────────┐
│  Rainmeter Core│  │   Dock 模块     │  │   UI 适配层      │
│  (Library/)    │  │   (Dock/)       │  │   (UI/)          │
│  Measure/Meter │  │  DockBar/Item   │  │  DuilibWindowBase│
│  Skin/Config   │  │  Magnify/Window │  │                  │
└───────┬────────┘  └────────┬───────┘  └────────┬─────────┘
        │                    │                    │
        └────────────────────┼────────────────────┘
                             ▼
                  ┌───────────────────────┐
                  │  系统层 (Windows API) │
                  │  Direct2D / DirectWrite│
                  │  Pdh / IpHelper / GDI+│
                  └───────────────────────┘
```

## 3. 核心类设计（Rainmeter 派生）

### 3.1 CRainmeter（单例）

```cpp
// Library/Rainmeter.h（简化骨架，对应 Rainmeter Library/Rainmeter.h）
class CRainmeter
{
public:
    static CRainmeter& GetInstance();

    // 生命周期
    bool Initialize(HINSTANCE hInstance);
    void Finalize();

    // 皮肤集合管理
    Skin* ActivateSkin(const std::wstring& config, const std::wstring& iniPath);
    void DeactivateSkin(Skin* skin);
    const std::map<std::wstring, Skin*>& GetSkins() const { return m_Skins; }

    // 命令分发（Bang 命令）
    void ExecuteCommand(const std::wstring& command, Skin* skin);

private:
    CRainmeter() = default;
    CRainmeter(const CRainmeter&) = delete;
    CRainmeter& operator=(const CRainmeter&) = delete;

    std::map<std::wstring, Skin*> m_Skins;
    std::unique_ptr<SkinRegistry>  m_SkinRegistry;
    std::unique_ptr<CommandHandler> m_CommandHandler;
};
```

**集成要点**：替换 Rainmeter 原有的窗口管理逻辑为 Duilib 窗口体系；其余生命周期 / 皮肤集合 / 命令分发逻辑直接复用。

### 3.2 Measure 基类与子类

```cpp
// Library/Measure.h
class Measure
{
public:
    virtual ~Measure() = default;
    virtual void Initialize(ConfigParser& parser, const std::wstring& iniPath);
    virtual void Update();                       // 采集
    virtual double GetValue() const;             // 数值
    virtual const WCHAR* GetString();            // 字符串
    virtual void Finalize();
protected:
    double m_Value = 0.0;
    std::wstring m_StringValue;
};
```

子类（均直接复用 Rainmeter 实现）：

| 类 | 采集内容 | 关键 Windows API |
|----|---------|----------------|
| `MeasureCPU` | CPU 占用率 | Pdh（性能计数器） |
| `MeasureMemory` | 物理内存占用 | `GlobalMemoryStatusEx` |
| `MeasureNet` | 网络上行/下行流量 | IP Helper (`GetIfTable`/`GetIfEntry2`) |
| `MeasureTime` | 时间/日期 | 无外部依赖 |
| `MeasurePlugin` | 第三方插件 | DLL 加载 (`LoadLibrary`) |

### 3.3 Meter 基类与子类

```cpp
// Library/Meter.h
class Meter
{
public:
    virtual ~Meter() = default;
    virtual void Initialize(ConfigParser& parser, Measure* measure);
    virtual void Update();                       // 刷新绑定的 Measure
    virtual void Draw(ID2D1RenderTarget* rt) = 0; // Direct2D 渲染
    virtual UINT GetWidth() const { return m_W; }
    virtual UINT GetHeight() const { return m_H; }
protected:
    Measure* m_Measure = nullptr;
    int m_X = 0, m_Y = 0, m_W = 0, m_H = 0;
};
```

子类：

| 类 | 渲染类型 | 依赖 |
|----|---------|------|
| `MeterString` | 文本 | DirectWrite |
| `MeterImage` | 位图 | WIC / Direct2D |
| `MeterBar` | 进度条 | Direct2D |
| `MeterLine` | 折线图 | Direct2D |

### 3.4 Skin 容器

```cpp
// Library/Skin.h（简化版）
class Skin
{
public:
    bool Load(const std::wstring& iniPath);
    void Update();            // 周期性驱动所有 Measure/Meter
    void Render(ID2D1RenderTarget* rt);
    void DoBang(const std::wstring& bang);

    const std::vector<std::unique_ptr<Measure>>& GetMeasures() const;
    const std::vector<std::unique_ptr<Meter>>& GetMeters() const;

private:
    std::vector<std::unique_ptr<Measure>> m_Measures;
    std::vector<std::unique_ptr<Meter>>   m_Meters;
    ConfigParser   m_Parser;
    HWND           m_Window = nullptr;   // 待替换为 Duilib 窗口
};
```

### 3.5 ConfigParser / CommandHandler

- `ConfigParser`：INI 解析引擎，对应 Rainmeter `Library/ConfigParser`。直接复用。
- `CommandHandler`：Bang 命令解析与执行，对应 `Library/CommandHandler`。直接复用。

## 4. Dock 模块（全新实现）

详见 [DOCK_DESIGN.md](DOCK_DESIGN.md)。核心类：

- `DockBar`：Dock 容器，承载 `DockItem` 列表，处理鼠标交互与放大效果。
- `DockItem`：单个图标项（路径、参数、运行状态、关联窗口句柄）。
- `DockMagnify`：悬停放大算法（基于到指针距离的衰减函数）。
- `DockWindow`：Duilib 窗口基类派生，承载 `DockBar` 渲染。

## 5. 模块依赖关系

```
App/main.cpp
  └─ CRainmeter ──┬─ Skin ──┬─ Measure (基类)
  │               │          │     ├─ MeasureCPU / Memory / Net / Time / Plugin
  │               │          └─ Meter (基类)
  │               │                ├─ MeterString / Image / Bar / Line
  │               ├─ ConfigParser
  │               └─ CommandHandler
  └─ DockWindow ── DockBar ──┬─ DockItem
                             └─ DockMagnify
UI/DuilibWindowBase  ←─ Skin.m_Window / DockWindow 共同基类
```

## 6. 与 Rainmeter 原有模块的集成方式

| Rainmeter 模块 | 集成方式 | 修改点 |
|----------------|---------|--------|
| `Rainmeter.cpp/h` | 直接复用 | 替换窗口管理为 Duilib |
| `Measure*.cpp/h` | 直接复用 | 无 |
| `Meter*.cpp/h` | 直接复用 | 无 |
| `ConfigParser.cpp/h` | 直接复用 | 无 |
| `CommandHandler.cpp/h` | 直接复用 | 无 |
| `Skin.cpp/h` | 简化复用 | 窗口创建改为 Duilib |
| Dock（无） | 全新实现 | — |
| 主题系统 | 扩展 | 支持 Dock + 挂件统一主题 |

## 7. 关键技术决策

- **C++17 / MSVC v143**：与 Rainmeter 主线一致。
- **Direct2D + DirectWrite**：替代 GDI+ 渲染路径（可选保留 GDI+ 作后备）。
- **Duilib**：透明/异形窗口支持优于原生 Win32，适合 Dock 与挂件统一。
- **GPL v2**：派生 Rainmeter 必须；新写模块同样以 GPL v2 发布以避免许可冲突。
