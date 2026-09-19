# RainDeskPlus

> 桌面美化集成平台 —— 基于 Rainmeter (GPL v2) 派生的**系统监控挂件 + Dock 栏 + 皮肤/主题系统**。

**当前版本：v0.1.0**。Phase 0-5（D1-D40）已全部交付：核心引擎从上游提取并升真、Dock 栏可拖拽加项与悬停放大、皮肤/主题系统统一取色、配置可落盘持久化。两条构建线全量回归常态绿（OFF Smoke 115/0、ON Smoke 117/0）。

## 项目定位

| 维度 | 说明 |
|------|------|
| 上游 | [Rainmeter](https://github.com/rainmeter/rainmeter) — GPL v2，C++ 核心 + C# 插件 |
| 新增 | Dock 栏（参考 Cedro Modern Dock / RocketDock 设计，全新实现，不使用专有 Nexus 源码） |
| UI 框架 | Duilib（`qdtroy/DuiLib_Ultimate`，已 vendor 至 `third_party/duilib`），由 CMake 选项 `RAINDOCK_USE_DUILIB` 开关 |
| 渲染 | Direct2D + DirectWrite（挂件）；Dock 图标绘制走 Duilib `CRenderEngine` |
| 语言标准 | C++20（MSVC `/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc`） |
| 工具链 | MSVC v145（VS 2026，主）+ v143（VS 2022，回退）；两者二进制兼容 |
| 构建 | CMake（最低 3.20）+ CMakePresets；x64 / 系统代码页 936 |
| 测试 | 自研 Smoke 可执行程序（不依赖 GoogleTest），构建即可跑 |
| 许可证 | GPL v2（保留上游版权声明，详见 [LICENSE](LICENSE)） |

## 已实现能力

### 系统监控挂件（9 类 Measure + 4 类 Meter）

| Measure | 要点 |
|---------|------|
| `Time` | `Format` / `FormatLocale` / `TimeStamp` / `TimeZone` / `DaylightSavingTime` |
| `Calendar` | 固定 6 行 × 7 列整月网格（27 字符等宽对齐），`Year`/`Month`/`WeekStart`/`HighlightToday`/`TodayMark` |
| `CPU` | 整机 `GetSystemTimes` 差分百分比，`Processor` 可选单核 |
| `Memory` | 物理 + 页面文件口径，`Mode`（UsedPercent/UsedBytes/FreeBytes/TotalBytes）/ `Total` |
| `Disk` | `Drive` 指定盘符，`Total` 切换总容量与剩余容量 |
| `Net` | `GetIfTable2` 64 位计数器，`Type`（InOctets/OutOctets）/ `Cumulative` / `Interface`（Best·Total·网卡名） |
| `Weather` | OpenWeatherMap 离线报文解析（温度 / 城市 / 描述），`ExpireMinutes` 过期判定，`ApiKey` 支持环境变量兜底 |
| `Media` | 播放状态快照：`Field` 取 Title/Artist/Album/State/Progress；`!CommandMeasure` 经 `WM_APPCOMMAND` 广播播放控制 |
| `Audio` | 自写 radix-2 FFT，`Field` 取 Band1..Band8 / Level / Peak，产出 0..1 供 Bar 归一化 |

| Meter | 要点 |
|-------|------|
| `String` | `FontFace` / `FontSize` / `FontColor` / `StringAlign` / `Text`（`%1` 绑定 Measure） |
| `Bar` | `BarOrientation` / `BarColor` / `SolidColor`（底槽），Min-Max 归一化 |
| `Line` | 环形采样 + `AutoScale` 自动量程折线，`LineColor` / `LineWidth` / `LineCount` |
| `Image` | 骨架（未注册进 `Skin` 工厂） |

段公共键：`UpdateDivider` / `DynamicVariables` / `OnUpdateAction` / `Group`（Section），`X` / `Y` / `Hidden`（Meter），`MinValue` / `MaxValue` / `InvertMeasure` / `Disabled` / `Substitute`（Measure）。条件动作支持数值（`IfAbove*`/`IfBelow*`/`IfEqual*`）、表达式（`IfCondition*`）、正则（`IfMatch*`，PCRE16 编译结果按下标缓存）三组，均带编号扩展。

### Dock 栏

- **项管理**：`items.ini` 声明的快捷项 + 运行态枚举；配置读写统一走 `ConfigParser`（段序 / 键序 / 文件头注释逐字节保持）。
- **拖拽添加**：接收 `WM_DROPFILES`，仅接受普通文件，按 `targetPath` 去重，`Id` 冲突自动追加 `_2`/`_3`，图标提取失败降级不丢项。
- **悬停放大**：`DockMagnify` 线性衰减曲线，由 `DockBar::TickAnimation()` + `WM_TIMER`(~60ms) 驱动。
- **透明窗口**：Duilib `WindowImplBase` 适配层，依赖仅收敛在 `DockWindow`（DD-2），`DockBar` 为纯逻辑容器可脱 UI 单测（DD-1）。
- **布局参数**：`IconSize`（16..128，越界钳制并告警）/ `Position`（Top/Left/Right/Bottom，非法回落 Bottom）/ `Spacing` / `AutoHide` / `MaxScale` / `MaxRadius`。

### 皮肤与主题系统

- **皮肤语法**：`[Rainmeter]` / `[Variables]` / `[Theme]` / `[Measure*]` / `[Meter*]`，`#变量#` 展开；`Skin` 工厂按 `Measure=` / `Meter=` 构造并绑定 `MeasureName`。
- **主题合并**：读 `[Theme] Name=` → 同目录 `Themes\<名>.ini` 按**低优先级默认值**并入（主文件显式键优先），缺失仅告警不阻断 → 挂件与 Dock 共用同一份主题文件。
- **三套内置主题**：`Dark`（默认，与原硬编码配色零视觉回归）/ `Light` / `HighContrast`，含 `[Variables]` 挂件取色字号 + `[Colors]` Dock 面板色与透明度。
- **切换方式**：改 `[Theme] Name=` 后 `!Refresh` 触发 `Skin::Reload()` 从磁盘重读生效。

### Bang 命令（13 条，大小写不敏感）

`!Update` / `!Redraw` / `!SetOption` / `!SetVariable` / `!WriteKeyValue` / `!Hide` / `!Show` / `!Toggle` / `!Refresh` / `!ActivateConfig` / `!DeactivateConfig` / `!CommandMeasure` / `!Quit`

非 `!` 开头不解析，未知命令静默忽略。详见 [用户手册](docs/USER_MANUAL.md) 第 7 章。

## 目录结构

```
RainDeskPlus/
├── App/            主程序（自检 Smoke + Dock 宿主）
├── Library/        核心引擎（Rainmeter 派生：Measure / Meter / Skin / ConfigParser ...）
├── Common/         基础设施（StringUtil / MathParser / Timer / FileUtil / JsonParser ...）
├── Dock/           Dock 栏（DockBar / DockItem / DockMagnify / DockWindow / IconExtractor）
├── UI/             Duilib 窗口适配层
├── Plugins/        插件 DLL（占位）
├── Skins/example/  示例皮肤（skin.ini / Dock.ini / items.ini / Themes×3 / 离线快照 json）
├── Themes/         主题目录（占位，实际主题随皮肤放在 Skins/<皮肤>/Themes/）
├── ThirdParty/     ankerl（哈希表）+ pcre（PCRE16 正则）
├── Tests/          Smoke 测试程序源码
├── docs/           架构 / 模块提取 / Dock 设计 / 构建 / 路线图 / Agent 提示词 / 用户手册
├── scripts/        构建 / 配置 / 拉取 Rainmeter 脚本
├── CMakeLists.txt  主构建脚本
├── CMakePresets.json
└── RainDeskPlus.sln          VS 原生 staging stub（可直接双击打开）
```

> Duilib 未入库（`third_party/duilib` 由 `.gitignore` 排除），需按 [BUILD.md](docs/BUILD.md) §4 自行 vendor。

## 快速开始

### 运行发布包

解包 `RainDeskPlus-0.1.0-win64.zip` 后双击 `RainDeskPlus.exe`。程序会以自身 exe 目录为基准向上最多 5 级查找 `Skins\` 目录，加载 `Skins\example\Dock.ini` 拉起 Dock 常驻；同目录写出的 `smoke_result.txt` 记录本次自检结果（末行为 `SMOKE SUMMARY PASS=n FAIL=n`）。

自检失败时进程退出码非 0；设置 `RAINDOCK_SMOKE_MS` 可让 Dock 常驻循环在指定毫秒后自动退出，便于脚本化验证：

```powershell
$env:RAINDOCK_SMOKE_MS = "3000"
.\RainDeskPlus.exe
```

### 从源码构建

前置：VS 2026（或 2022）+ Windows SDK + CMake ≥ 3.20。产物统一写入项目下的 `build/`，不落 C 盘。

```powershell
# 默认线（Duilib OFF）：挂件 + Dock 数据模型
cmake --preset vs2026-x64
cmake --build --preset vs2026-release

# ON 线（Duilib ON）：额外构建 Dock 透明窗口与 Duilib 适配测试
cmake --preset vs2026-x64-dock-ui
cmake --build --preset vs2026-dock-ui-release

# 或使用脚本（-Preset 可选 ninja-debug/ninja-release/vs2026-debug/vs2026-release/vs2022-debug/vs2022-release）
.\scripts\build.ps1 -Preset vs2026-release
```

产物位于 `build/vs2026/RelWithDebInfo/`（ON 线为 `build/vs2026-dock-ui/RelWithDebInfo/`）：`RainDeskPlus.exe` + `RainDeskPlusCore/Dock/UI.lib`。

## 构建与验证

每次构建后按约定归档日志到 `logs/build_YYYYMMDD_HHMMSS.log`。两线常态化回归基线：

| 构建线 | 配置 | Smoke | 测试程序 |
|--------|------|-------|----------|
| OFF | `vs2026-x64` / `RelWithDebInfo` | **115 PASS / 0 FAIL** | 5/5 全过 |
| ON | `vs2026-x64-dock-ui` / `RelWithDebInfo` | **117 PASS / 0 FAIL** | 7/7 全过 |

Smoke 检查点覆盖：Measure/Meter 全链路、主题合并与切换、配置落盘字节精确比对、真实皮肤端到端加载、IfMatch 正则编译缓存（`pcre16_compile=1`）、Skin 脏检查跳帧（`renders=0 skipped=frames`）与 `!Redraw` 强制重绘。性能效果一律用**确定性计数**断言，不用墙钟时间。

## 文档导航

- [用户手册](docs/USER_MANUAL.md) — 安装运行 / 皮肤语法 / Measure·Meter 参考 / 主题 / Dock 配置 / Bang 命令 / 故障排查
- [更新日志](CHANGELOG.md)
- [架构设计](docs/ARCHITECTURE.md)
- [Rainmeter 模块提取方案](docs/MODULE_EXTRACTION.md)
- [Dock 栏设计](docs/DOCK_DESIGN.md)
- [构建指南](docs/BUILD.md)
- [开发路线图](docs/ROADMAP.md)
- [AI Agent 提示词模板](docs/AGENT_PROMPTS.md)

## 许可证

Copyright (C) 2014-2025 Rainmeter Project  
Copyright (C) 2026 RainDeskPlus Project

本程序为自由软件，依据 GNU 通用公共许可证第 2 版（GPL v2）条款发布。详见 [LICENSE](LICENSE)。

> Rainmeter 源码派生部分保留其原作者版权声明；Dock 模块为全新实现，同样以 GPL v2 发布。PCRE 与 ankerl 第三方库各自保留其原始许可证（见 `ThirdParty/` 下对应文件）。
