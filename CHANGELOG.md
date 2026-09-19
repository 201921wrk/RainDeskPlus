# 更新日志

本文件记录 RainDeskPlus 的重要变更。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

开发里程碑逐条对应 [开发路线图](docs/ROADMAP.md)。

---

## [0.1.0] - 2026-09-18

首个可运行版本。Phase 0-5（D1-D40）全部交付：核心引擎从 Rainmeter 提取并升真、Dock 栏可拖拽加项与悬停放大、皮肤/主题系统统一取色、配置可落盘持久化。

### 新增 — 核心引擎（Phase 1，D6-10）

- **`Library/ConfigParser`**：真实 INI 解析（变量展开、颜色/数值口径、段序与键序视图、文件头注释保留）。
- **`Library/Measure`** 基类：`Initialize` → `ReadOptions` → `Update` 跳帧链路，支持 `InvertMeasure` / `Substitute` / `MinValue` / `MaxValue` / `UpdateDivider`。
- **Measure 实现**：`MeasureTime`（真实格式化 + 时区/DST 口径）、`MeasureCPU`（整机 `GetSystemTimes` 差分）、`MeasureMemory`（物理 + 页面文件口径）、`MeasureNet`（`GetIfTable2` 64 位计数器，速率与累计）。
- **`RainDeskPlusRenderTest`** 离屏渲染 Smoke（Direct2D + WIC PNG 落盘核验）。
- 代码审查（D10）覆盖 `Library/` 46 + `Common/` 25 个文件，修复 3 Critical / 9 Major / 14 Minor（含悬垂指针、重复驱动、`TimeZone` 重复叠加等）。

### 新增 — Dock 栏（Phase 1 末 · Phase 2，D11-17）

- **`Dock/DockBar`**：纯逻辑容器（DD-1），项管理、布局计算、配置读写。
- **`Dock/DockItem` / `Dock/IconExtractor`**：快捷项模型与图标提取（提取失败降级不丢项）。
- **拖拽添加（D15）**：接收 `WM_DROPFILES`，按目标路径去重，`Id` 冲突自动追加 `_2`/`_3`。
- **悬停放大（D16-17）**：`DockMagnify` 线性衰减曲线，由 `WM_TIMER`(~60ms) 驱动。
- **`Dock/DockWindow`**：Duilib `WindowImplBase` 透明/异形窗口，Duilib 依赖仅收敛于该 TU（DD-2）。
- 架构设计定稿 [DOCK_DESIGN.md](docs/DOCK_DESIGN.md)，含 12 条关键设计决策（DD-1~DD-12）。

### 新增 — 系统监控挂件（Phase 2，D18-25）

- **磁盘（D18-19）**：`MeasureDisk` + 两个真渲染 Meter `MeterBar`（Min-Max 归一化）与 `MeterLine`（环形采样 + `AutoScale` 自动量程）。
- **天气（D20-21）**：`MeasureWeather` 解析 OpenWeatherMap 报文，`ExpireMinutes` 过期判定，`ApiKey` 支持环境变量兜底（仓库内不落真实 Key）。
- **时钟 / 日历（D22-23）**：`MeasureCalendar` 产出固定 6 行 × 7 列整月网格（27 字符等宽对齐），`WeekStart` / `HighlightToday` / `TodayMark` 可配。
- **媒体控制（D24）**：`MeasureMedia` 快照字段（Title/Artist/Album/State/Progress）+ `!CommandMeasure` 经 `WM_APPCOMMAND` 广播播放控制。
- **音频可视化（D25）**：`MeasureAudio` 自写 radix-2 Cooley-Tukey FFT（定长 1024 点窗、几何级数 8 频段、段内取最强 bin），`Field` 取 Band1..Band8 / Level / Peak。
- 示例皮肤 `Skins/example/skin.ini` 扩展为九合一挂件（21 Measure / 28 Meter），并配套离线快照 `weather_cache.json` / `media_state.json` / `audio_state.json` 保证无网络、无设备可全量回归。

### 新增 — 皮肤 / 主题系统（Phase 3，D26-30）

- **主题合并**：`ConfigParser::ApplyThemeDefaults` 读 `[Theme] Name=` → 同目录 `Themes\<名>.ini` 按**低优先级默认值**并入（主文件显式键优先），缺失仅告警不阻断。
- **三套内置主题**：`Skins/example/Themes/{Dark,Light,HighContrast}.ini`，含 `[Variables]` 挂件取色字号与 `[Colors]` Dock 面板色 / 透明度；`Dark` 即原硬编码配色，主题化后零视觉回归。
- **挂件 + Dock 共用同一份主题文件**；皮肤内 22 处硬编码取色/字号提升为 `#变量#`。
- **Dock 主题字段**：`DockBar` 新增 `PanelColor`（`#AARRGGBB`，默认 `#40000000`）与 `ThemeName`，`Transparency` 走三级回退。
- **切换方式**：改 `[Theme] Name=` 后由 `!Refresh` 触发 `Skin::Reload()` 从磁盘重读生效。

### 新增 — 配置与持久化（Phase 4，D31-35）

- **`ConfigParser` 统一落盘出口**：新增 `SaveFile` / `LoadFileRaw` / `SetHeaderComment` / `GetHeaderComment` / `RemoveValue` / `RemoveSection`；引入段序、键序、文件头注释三套顺序视图，生成文件与既有手写格式**逐字节一致**（UTF-8 无 BOM / CRLF）。
- **`LoadFileRaw` 不合并主题**，避免「读 → 改 → 写」把主题键展开进主文件、污染 `[Theme] Name=` 声明。
- **`DockBar` 配置读写改走 `ConfigParser`**，DD-7 既定例外（裸 `std::ofstream`）作废；`SaveConfig` 首行写 `[Theme] Name=`，且不再写 `Transparency`（归主题 `[Colors]` 管辖）。
- **`!WriteKeyValue Section Key Value [File]`** Bang：补齐「内存改 → 落盘」链路，引号感知分词支持值与路径含空格。

### 新增 — 集成测试与性能优化（Phase 5，D36-40）

- **端到端 Smoke 区段**：新增 4 个夹具与 6 个检查点 —— `e2e-real-skin`（真实皮肤全量加载，实测 21 Measure / 28 Meter）、`e2e-theme-switch-reload`（主题切换后 `Reload()`，`SMOKE-DARK → SMOKE-LIGHT`）、`e2e-reload-ini-path`（重载后 `GetIniPath()` 不漂移）。
- **性能 #1 — IfMatch 正则编译缓存**：`Library/Pcre.h` 增进程级编译计数，`IfActions` 按下标缓存已编译 `pcre16` 句柄，消除每次 `Update` 重编译同一表达式的开销（`perf-ifmatch-compile-once`：连续 25 次 `Update` 编译次数恒为 1）。
- **性能 #2 — Skin 脏检查跳帧**：`Skin::RenderFrame` 前用 FNV-1a 64 位指纹比对，画面未变则整帧跳过；`!Redraw` 经 `m_Dirty` 强制重绘一帧（`perf-dirty-skip-render`：`renders=0 skipped=frames`；`perf-bang-redraw`：`renders` 恰 +1）。
- 性能效果一律用**确定性计数**断言，不用墙钟时间，故可直接纳入常态化回归。

### 修复

- **Dock 关闭竞态崩溃（D36-40）**：`DockWindow::Stop()` 原走 Duilib `CWindowWnd::Close()`（异步 `PostMessage(WM_CLOSE)`），调用方返回后立即 `CPaintManagerUI::Term()` 释放共享资源，队列残留的 `WM_CLOSE` 随后被派发 → 访问已释放资源（`0xC0000005`）。改为 `::SendMessageW(m_hWnd, WM_CLOSE, 0, 0)` 同步关闭（`DestroyWindow` 顺带丢弃该窗口残留消息）。
- **`MeasureAudio::ParseBandIndex` 前缀大小写口径不一致**：导致 `Field=BandN` 恒回落 `Band1`，前缀改为全小写后修复。

### 验证基线

| 构建线 | 配置 | Smoke | 测试程序 |
|--------|------|-------|----------|
| OFF（Duilib 关） | `vs2026-x64` / `RelWithDebInfo` | 115 PASS / 0 FAIL | 5/5 全过 |
| ON（Duilib 开） | `vs2026-x64-dock-ui` / `RelWithDebInfo` | 117 PASS / 0 FAIL | 7/7 全过 |

两线均 0 error / 0 warning；日志归档于 `logs/build_20260918_233725.log`。

### 文档

- 新增 [用户手册](docs/USER_MANUAL.md)（安装运行 / 皮肤语法 / Measure·Meter 参考 / 主题 / Dock 配置 / Bang 命令 / 故障排查）。
- 重写 [README](README.md) 对齐 D1-D40 现状。
- 新增本更新日志。

### 已知限制

- `Meter=Image` 仅为骨架，未注册进 `Skin` 工厂。
- 天气 / 媒体 / 音频三者为**离线快照**范式（读 JSON），真实联网拉取与 WASAPI 采集按同一接口后续接上。
- 主程序当前形态为「自检 + Dock 宿主」，尚无交互式皮肤选择入口。
- `RAINDOCK_BUILD_TESTS` / `RAINDOCK_BUILD_PLUGINS` 仍为 OFF，插件 DLL 未实现。
- `TimeStamp=DSTStart / DST_NEXT_START` 未实现。

---

## 版本号说明

- **0.1.0**：首个版本，核心能力闭环（挂件 + Dock + 主题 + 持久化）可用，故主版本号从 0 起步、次版本号 1 表示首批功能集。
- 发布标签：`v0.1.0`；发布包：`RainDeskPlus-0.1.0-win64.zip`（x64 / RelWithDebInfo）。
