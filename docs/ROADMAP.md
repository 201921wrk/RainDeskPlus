# 开发路线图

> 3 个月 MVP，单人开发 + AI 辅助。本文为计划骨架，进度按实际推进更新。

## Phase 0 — 环境搭建（Week 1）

- [x] D1：项目脚手架（目录 / CMake / VS stub / 文档 / 类骨架）
- [x] D1：安装 **Visual Studio 2026 Community**（v145 工具集，回退 v143 保留；路径 `E:\visual_C`）
- [x] D1：配置 git 身份（全局 `Tcool / thankword@outlook.com`；.gitconfig 4 域名级 openssl+SteamTools 根 CA 绕过 Watt MITM 证书链不完整）
- [x] D2：克隆 Rainmeter 源码到 `rainmeter-upstream/`（`--depth 1`，HEAD abd0b5ae；.gitignore 排除）
- [~] D2：打开 `Rainmeter.sln`（上游）编译 Debug（M1 未执行；上游依赖 GDI+/WTL/vcpkg 未就绪，M2 前置或按需验证）
- [x] D3：架构笔记就位（= [ARCHITECTURE.md](ARCHITECTURE.md)）
- [x] D4：本项目目录结构就绪 + 骨架首次 Debug 构建通过（2026-08-26，0 error 0 warning）
- [x] D5：Duilib 开发环境就绪 + Hello World 跑通（2026-09-13）—— vendor `qdtroy/DuiLib_Ultimate` 于 `third_party/duilib`，根 `CMakeLists.txt` 自建 STATIC `Duilib` target；预设 `vs2026-x64-dock-ui`（`RAINDOCK_USE_DUILIB=ON`）构建 0 error，`RainDeskPlusDuilibTest` Smoke **6/6 全过**（窗口创建 / 内联 XML 解析 / 控件查找 / 保持可见 / 正常销毁）；同时回归确认默认预设（Duilib OFF）3 个既有 Smoke 无退化。踩坑与隔离方案见 [BUILD.md](BUILD.md) §4

## Phase 1 — 核心引擎提取（Week 2）

- [x] D6-7：从 Rainmeter **提取核心骨架并对齐**到 `Library/`（见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M1 细节）
  - `ConfigParser.cpp/h`（真实 INI 解析 + 变量展开 + 颜色/数值/段序）
  - `Measure.cpp/h`（基类：Initialize→ReadOptions→Update 跳帧→UpdateValue=0 / Invert/Substitute/Min/Max/Divider）
  - `MeasureTime`（真实格式化 + 时区/DST 禁用兼容）
  - `MeasureCPU`（整机 `GetSystemTimes` 差分 %CPU；单核 M1 降级）
  - `MeasureMemory`（Phys+PageFile 口径 / Mode / Total 键）
  - `MeasureNet` / `MeasurePlugin`（占位对齐新签名；真采样留 M2）
  - `Meter*/Skin/CommandHandler/Rainmeter` 骨架保留，M2→M5 陆续升真
- [x] **M1 里程碑（2026-08-28）**：vs2026-x64 Debug **clean rebuild 0 error / 0 warning / 4 目标全产出**（RainDeskPlusCore.lib + RainDeskPlusDock.lib + RainDeskPlusUI.lib + RainDeskPlus.exe）
- [x] **M2 里程碑（2026-08-28）**：`MeasureNet` 真采样（GetIfTable2 64 位计数器 / Best·Total·Index / 速率·累计）+ `MeasurePlugin` DLL 加载骨架 + **Smoke 测试 11/0 全过**（Time/CPU/Memory/Net 全链路，Net 累计值与系统计数器一致）；修 2 个真 bug（Memory UsedPercent 缩放语义 / Measure.h 默认 clamp 误伤无界 Measure）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7。
- [x] **M3 里程碑（2026-08-29）**：`MeterString` 经 Direct2D/DirectWrite 真渲染出文本（`Canvas` 共享工厂 + 离屏 WIC 渲染目标；Text=%1 绑定 Measure；AutoSize metrics；Smoke **17/0 全过**，渲染像素 1222 命中 + PNG 落盘核验）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M3。
- [x] **M4 里程碑（2026-08-29）**：`Skin` 加载最简皮肤并**真实显示**（WS_POPUP 挂件窗口 + D2D HwndRenderTarget + WM_TIMER 驱动；INI→Measure/Meter 工厂构建绑定；Smoke **22/0 全过**，窗口可见 + 5 帧渲染）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M4。
- [x] **M5 里程碑（2026-09-06）**：`CRainmeter` + `CommandHandler` Bang 命令切换皮肤成功——`ActivateSkin` 加载后显示挂件窗口（同名 config 先停用旧皮肤防泄漏）、`Initialize` 自动向上探测 `Skins/` 根目录并刷新 `SkinRegistry`、`ExecuteActionCommand(::Section*)` 恢复皮肤上下文；`RainDeskPlusBangTest` **ALL TESTS PASSED**（3 套 Smoke 全过）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M5。
- [x] **D10 代码审查（2026-09-13）**：【代码审查 Agent】审查 Phase 1 全量提取产物（`Library/` 46 + `Common/` 25 = 71 个 `.h/.cpp/.inl`）。共发现 **26 条问题：3 Critical / 9 Major / 14 Minor**，已按 Iterative Fix Loop 全部修复并回归验证：
  - Critical：#1 `Mouse.cpp` `ReadString().c_str()` 悬垂指针、#2 `Skin::Load()` 清空容器后 `m_MouseOverMeter` 悬垂、#3 `OnUpdateAction` 中 `!Redraw`/`!Update` 无保护重入递归
  - Major：#4 Measure 双重驱动（`UpdateDivider` 折半）、#5 `RegQueryValueEx` 缓冲长度单位错误、#6 `ftell` 失败未检查、#7 `MeasureTime` TimeZone 重复叠加、#8 `MeasureMemory` 量纲不一致、#11 `RawString` realloc 自引用失效 + 其余
  - Minor：#12-#26（含 `Canvas` 工厂 `std::call_once`、`Util::GetUniqueID` 原子化、`MeterString` 右值 `SetText` 与 `TextFormat` 跨帧复用、Bang 名大小写不敏感、`PathUtil` RAII 缓冲、GPL 版权头补齐等）
  - 回归：`vs2026-x64` Debug 与 `vs2026-x64-dock-ui` Debug 两条构建线 **0 error / 0 warning**；`RainDeskPlusRenderTest` / `RainDeskPlusSkinTest` / `RainDeskPlusBangTest` / `RainDeskPlusDuilibTest` **全部 ALL TESTS PASSED**。日志见 `logs/build_20260913_*.log`、`logs/smoke_*_*.log`。

## Phase 1 末 / Phase 2 — Dock 栏（Week 3）

- [x] D11-12：【架构师 Agent】设计 `DockBar` 结构（= [DOCK_DESIGN.md](DOCK_DESIGN.md)）**（2026-09-14 定稿）**
  - 交付：`DOCK_DESIGN.md` 641 行终稿，含架构师 Agent 5 项输出（类设计 / 核心接口 C++ 框架 / 模块依赖 / Rainmeter 集成方式 / 关键实现思路），覆盖 `DockItem`·`DockMagnify`·`DockBar`·`DockWindow`·`UI::DuilibWindowBase` 五类。
  - **12 条关键设计决策（DD-1~DD-12）**：DD-1 `DockBar` 为纯逻辑容器不继承 Duilib 基类（可脱 UI 单测）；DD-2 Duilib 依赖仅收敛在 `DockWindow` 且由 `RAINDOCK_USE_DUILIB` 包裹；DD-3 适配层真实分支定为 `#include "UIlib.h"` + `DuiLib::WindowImplBase`（依据 `UIlib.h:69` 间接包含 `Utils/WinImplBase.h`，该头不自包含）；DD-4 含 Duilib 头的 TU 须 `/UNOMINMAX /UWIN32_LEAN_AND_MEAN /W0`（目录级宏与 `StdAfx.h` 冲突会 C2672）；DD-5 动画由 `DockBar::TickAnimation()` + `WM_TIMER`(~60ms) 驱动；DD-6 布局收敛进 `ComputeLayout()` 并暴露只读查询；DD-7 配置复用 `Library/ConfigParser`；DD-8 图标绘制走 Duilib `CRenderEngine::DrawImage`，Dock 不引入 Direct2D；DD-9 保留线性衰减曲线，替换点集中 `DockMagnify.cpp`；DD-10 **反转依赖方向** `Dock →(PUBLIC) UI`，删除 `UI → Dock` 消除循环依赖；DD-11 Dock 生命周期由 `App/main.cpp` 持有，不进 `CRainmeter`；DD-12 `RAINDOCK_USE_DUILIB=1` 必须全局一致（目录级定义）防 ODR 冲突。
  - **实现待办（§10 前置清单 8 项）已按依赖顺序排列**：改 `DuilibWindowBase.h` → 删 `UI→Dock` 边 → 加全局宏 → Dock 条件链 Duilib + 源文件选项 → 补 `DockBar` API → 改 `DockWindow` 签名（现状 `Notify(void*)`/`OnMouseMove(void*,void*)` 与真实 `WindowImplBase` 不兼容）→ 实现运行态枚举 → 新增 2 个测试。
  - 验收里程碑 D1~D4（项管理+启动 / 放大几何断言 / 透明窗口 / 运行态枚举）与 D10 回归行已写入 §9；5 条开放问题（`WS_EX_NOACTIVATE` 兼容性、运行中程序口径、`!Dock*` Bang 归属、高 DPI 多显示器、`Dock.ini` 归属）均标 `[需要确认]`。
- [x] D13-14（2026-09-15）：【编码助手 Agent】实现 `DockBar` 核心功能
- [x] D15（2026-09-15）：图标拖拽添加
- [x] D16-17（2026-09-15）：鼠标悬停放大效果（`DockMagnify`）

## Phase 2 — 功能集成（Week 4）

- [x] D18-19（2026-09-16）：系统监控挂件（CPU / 内存 / 磁盘 / 网络）
  - 交付：新增 `MeasureDisk`（`Drive=` 指定盘符 / `Total=` 切换总容量与剩余容量，未配置退回系统盘）与两个真渲染 Meter——`MeterBar`（`BarOrientation` / `BarColor` / `SolidColor`，Min-Max 归一化）与 `MeterLine`（`PushSample` 环形采样、`AutoScale` 自动量程折线，供网络速率）；三者均注册进 `Skin` 工厂（`Disk` / `Bar` / `Line`）。
  - 皮肤：`Skins/example/skin.ini` 重写为四合一挂件（CPU / 内存 / 磁盘 / 网络，4 Measure + 8 Meter = 4 String + 3 Bar + 1 Line）。
  - 验证：`App/main.cpp` Smoke 扩展磁盘与 Bar/Line 离屏渲染检查点（OFF 线 35 项 / ON 线 37 项全 PASS）；`Tests/test_skin_load.cpp` 与 `Tests/test_bang_commands.cpp` 断言同步为 4 Measure / 8 Meter；两线全量构建 + 全量回归全绿（日志归档于 `logs/final_smoke_*`）。
- [x] D20-21（2026-09-16）：天气挂件（OpenWeatherMap API）
  - 交付：新增 `MeasureWeather`（读取 `CacheFile=` 指定的 OWM 报文 JSON，解析 `main.temp` / `name` / `weather[0].description` / `dt`，产出温度值与「城市 温度 描述」字符串；`ExpireMinutes=` 配合报文自带 `dt` 判定过期并追加 `(cache stale)`，`0` 表示禁用；读盘或解析失败回退上次值；`ApiKey=` INI 配置优先、环境变量 `OPENWEATHER_API_KEY` 兜底，仓库内不落真实 Key），注册进 `Skin` 工厂（`Weather`）。本轮先交付离线可测骨架，真实联网拉取按同一接口后续接上。
  - 皮肤：`Skins/example/skin.ini` 扩展为五合一挂件（追加天气文本 Meter）；新增 `Skins/example/weather_cache.json` 离线种子报文，保证无网络环境可测。
  - 验证：`App/main.cpp` Smoke 新增天气检查点（`weather-*` 共 14 项：报文解析 / 摄氏度与华氏度换算 / 过期与禁用过期 / 坏 JSON、OWM 错误体、文件缺失三类回退 / Key 的 INI、环境变量、均无三级解析 / 无 Key 离线取值）；`Tests/test_skin_load.cpp` 与 `Tests/test_bang_commands.cpp` 断言同步为 5 Measure / 9 Meter；两线全量构建 + 全量回归全绿（OFF Smoke 49 项 / ON Smoke 51 项，日志归档于 `logs/final_smoke_*`）。
- [x] D22-23（2026-09-16）：时钟 / 日历挂件
  - 交付：新增 `MeasureCalendar`（产出「表头 + 固定 6 行 × 7 列」整月网格字符串，每格 3 字符宽 + 列间 1 空格 → 27 字符等宽、等宽字体下逐列对齐；行数固定 6 行，避免挂件高度随月份 4~6 周跳动。`Year=` / `Month=` 固定年月、`TimeStamp=` 固定 Unix 秒、`TimeZone=` / `DaylightSavingTime=` 与 `MeasureTime` 同一时区口径，保证同一皮肤上时钟与日历不出现跨天错位；`WeekStart=` 切换周首日，`HighlightToday=` / `TodayMark=` 控制今日标记，固定年月与参考月不一致时不标记（避免把当前日期标到别的月份）；`m_Value` = 当月天数，供数值型 Meter 归一化），注册进 `Skin` 工厂（`Calendar`）。时钟侧零新增 C++：3 个 `MeasureTime`（`%H:%M:%S` / `%Y-%m-%d` / `%A`）分别承担时、日期、星期。`TimeStamp=DSTStart / DST_NEXT_START`（`MeasureTime` 的 TODO(M2)）本轮不做，留待后续单独处理。
  - 皮肤：`Skins/example/skin.ini` 扩展为七合一挂件（追加时钟 / 日期 / 星期 3 个 String Meter 与日历整月网格 1 个 String Meter，共 9 Measure / 13 Meter）。
  - 验证：`App/main.cpp` Smoke 新增日历检查点（`calendar-*` 共 8 项：闰年 2 月 29 天 / 平年 2 月 28 天 / 网格 7 行 27 字符 / `WeekStart=0` 与 `=1` 的表头轮转与首格差异 / 今日标记唯一 / 固定月≠参考月无标记）；`Tests/test_skin_load.cpp` 与 `Tests/test_bang_commands.cpp` 断言同步为 9 Measure / 13 Meter；两线全量构建 + 全量回归全绿（OFF Smoke 57 项 / ON Smoke 59 项，日志归档于 `logs/final_smoke_*`）。
- [x] D24（2026-09-17）：媒体播放控制
  - 交付：新增 `MeasureMedia`（离线快照骨架：读取 `StateFile=` 指定的 JSON 快照，`Field=` 选择产出字段 —— `Title` / `Artist` / `Album` / `State` / `Progress`，因 `Measure::GetString()` 只出一条字符串而挂件需同时展示标题、艺术家、状态与进度条，故用「一个类 + 多段实例」承载；`State` 首字母大写归一化，`Progress` 产出 `1:18 / 4:05` 并给出 `m_Value` = 已播放秒数、`m_MaxValue` = 总时长，`MaxValue` 缺省补 1 保证 `MeterBar` 可归一化；快照缺失 / 坏 JSON / 错误体三类回退为占位符 `-`，解析失败时保住上一次成功值（对齐 `MeasureWeather` 的离线范式）。控制侧打通 `!CommandMeasure` 全链路：`Measure` 基类补 `virtual Command()` 默认 no-op → `MeasureMedia::Command` 经 `SendMessageTimeoutW(HWND_BROADCAST, WM_APPCOMMAND, ...)` 广播 `APPCOMMAND_MEDIA_PLAY_PAUSE` / `_NEXTTRACK` / `_PREVIOUSTRACK` / `_STOP`（播放与暂停合一；未知命令静默忽略），零新增构建依赖；`CommandHandler` 新增 `!CommandMeasure "MeasureName" "Command"` Bang，支持引号内空格与裸写两种写法，段名大小写不敏感，目标不存在时静默忽略），注册进 `Skin` 工厂（`Media`）。
  - 皮肤：`Skins/example/skin.ini` 扩展为八合一挂件（追加媒体挂件：标题 / 艺术家 / 状态 3 个 String Meter + 进度 1 个 Bar + PREV / PLAY / NEXT 3 个控制按钮 String Meter，共 13 Measure / 20 Meter）；新增 `Skins/example/media_state.json` 离线种子快照，保证无播放器、无网络环境可测。
  - 验证：`App/main.cpp` Smoke 新增媒体检查点（`media-*` 共 16 项：四字段产出 / 状态归一化 / 未知字段回落 Title / 进度量与可读串 / 文件缺失、坏 JSON、错误体三类回退 / 缺 State 字段默认 `Stopped` / 解析失败保住上次值 / APPCOMMAND 映射与未知命令返回 false / 未知命令 no-op 读数不变；为不干扰开发机正在播放的音乐，Smoke 不真广播）；`Tests/test_skin_load.cpp` 与 `Tests/test_bang_commands.cpp` 断言同步为 13 Measure / 20 Meter，并新增 `!CommandMeasure` 分发冒烟；两线全量构建 + 全量回归全绿（OFF Smoke 73 项 / ON Smoke 75 项，日志归档于 `logs/build_20260917_200659.log`）。
- [x] D25（2026-09-17）：音频可视化
  - 交付：新增 `MeasureAudio`（离线快照骨架：读取 `AudioFile=` 指定的 PCM 快照 `{ "sampleRate": 44100, "samples": [...] }`，`Field=` 选择产出字段 —— `Band1`..`Band8` / `Level` / `Peak`，未知取值回落 `Band1`，段名大小写不敏感；频谱用自写 radix-2 Cooley-Tukey FFT 替代上游的 `kiss_fft`（定长 1024 点窗、实输入虚部补零、只取前半谱，零新增构建依赖）；8 段按几何级数平分 60Hz..12kHz（每段约一个倍频程），段内取**最强 bin** 而非均值——高段覆盖的 bin 数远多于低段，均值会按 bin 数稀释单频能量；归一化基准取 `2.0/used`（实际参与点数），短帧因此不被低估。`Level` = 时域 RMS×√2（满量程正弦归一为 1），`Peak` = `max|sample|`；`m_Value` 统一为 0..1，`m_StringValue` 为百分比文本；`ReadOptions` 末尾在未显式给量程时补 `MaxValue=1`（否则 `MeterBar` 因 `span<=0` 恒取 0）。快照缺失 / 坏 JSON / `samples` 为空三类回退为占位符 `-`（不留空串，否则基类 `GetString()` 会回退成裸数值 `0.0`），成功过一次后再失败则保住上一次成功值（对齐 `MeasureWeather` / `MeasureMedia` 的离线范式），注册进 `Skin` 工厂（`Audio`）。真实 WASAPI 采集按同一接口后续接上。
  - 皮肤：`Skins/example/skin.ini` 扩展为九合一挂件（追加音频频谱：8 个 `Measure=Audio` + 8 根竖向 `Meter=Bar` 并排阵列，`X = 20 + (N-1) * 25`、条宽 23 / 间隔 2、`Y=608` / `H=60`、色序蓝→青→绿→黄→橙→粉渐变，共 21 Measure / 28 Meter）；新增 `Skins/example/audio_state.json` 离线种子快照（512 点 / 8 正弦叠加 130~8200Hz，统一缩放使峰值 ≈0.95，无设备、无播放即可全量回归）。
  - 验证：`App/main.cpp` Smoke 新增音频检查点（`audio-*` 共 19 项，分两层：① `Analyze()` 直调验证纯算法——频段常量 / 满量程正弦 peak≈level≈1 / 1000Hz 落第 5 段且 >0.8 / 100→0 段、300→2 段、3000→5 段、9000→7 段的映射 / 静音帧全 0 / 256 点短帧不低估 / 空指针与零采样率不产生 NaN；② `Measure` 全链路验证解析、字段与回退——`AudioFile` 相对/绝对路径、`Field` 大小写不敏感、未知字段回落 `Band1`、显式 `MaxValue=` 不被默认值覆盖、静音帧为 `0%` 而非失败的 `-`、坏 JSON / 空 `samples` / 文件缺失三类回退、失败保住上一次成功值）；`Tests/test_skin_load.cpp` 与 `Tests/test_bang_commands.cpp` 断言同步为 21 Measure / 28 Meter，并新增 8 条 `MeasureAudio(Band1..Band8)` 与 `MeterAudioBand1` 为 `MeterBar` 的类型断言；两线全量构建 + 全量回归全绿（OFF Smoke 92 项 / ON Smoke 94 项，日志归档于 `logs/build_20260917_215459.log`）。
  - 修复：`ParseBandIndex` 前缀大小写口径不一致（输入经 `towlower` 归一后仍与首字母大写的字面量 `Band` 比对）导致 `Field=BandN` 恒返回 -1、静默回落 `Band1`；首轮 OFF Smoke 的 `audio-band5` / `audio-maxvalue-explicit` 因此 FAIL（1000Hz 落在第 5 段却读到第 1 段的 1%）。前缀改为全小写后 19 项全绿。

## Phase 3 — 皮肤系统（Week 5-6）

- [x] D26-30（2026-09-18）：完善皮肤 / 主题系统（挂件 + Dock 统一主题）
  - 交付：`ConfigParser` 新增主题合并 `ApplyThemeDefaults(path)`，在 `LoadFile` 末尾读 `[Theme] Name=`，主题文件按「主文件同目录 `Themes\<名>.ini`」解析；存在则 `LoadFromString` 后以**低优先级默认值** `emplace` 并入（主文件显式写过的键不被覆盖），缺失则 `LogWarningF` 降级告警、不阻断加载 → 挂件与 Dock 共用同一份主题文件。切换为 INI 声明式：改 `[Theme] Name=` 后由 `!Refresh` 触发 `Skin::Reload()` → `Load(m_IniPath)` 从磁盘重读生效。`DockBar` 新增 `m_PanelColor`（Duilib `bkcolor` 口径 `#AARRGGBB`，默认 `#40000000` 与旧硬编码逐字节一致）与 `m_ThemeName`，配 `Set/GetPanelColor`（空值忽略，防止误清空主题色）、`Set/GetThemeName`；`LoadConfig` 读 `[Theme] Name` 与 `[Colors] PanelColor`，`Transparency` 走三级回退（`[Colors]` → 旧 `[Dock]` → 成员默认）；`SaveConfig` 首行写 `[Theme] Name=`（否则重载后主题声明丢失、`!Refresh` 切不回来）+ `[Dock]` 6 字段，且**不再写 `Transparency`**（透明度归主题 `[Colors]` 管辖，写死会遮住主题）。
  - 皮肤：`Skins/example/skin.ini` 把 22 处硬编码取色 / 字号提升为 `#变量#` 并声明 `[Theme] Name=Dark`，`[Variables]` 只留皮肤自有项（`FontFace` / `PanelW` / `MonoFace`）；新增 `Skins/example/Themes/{Dark,Light,HighContrast}.ini` 三套共享主题（`[Variables]` 挂件取色 / 字号 + `[Colors]` Dock 面板色 / 透明度），其中 `Dark` 即原硬编码配色 → 主题化后零视觉回归；`Skins/example/Dock.ini` 声明 `[Theme] Name=Dark`，Dock 的面板色 / 透明度由主题文件 `[Colors]` 提供（Dock.ini 手写 `[Colors]` 可本地覆盖，但 `SaveConfig` 视其为生成文件，只落 `[Theme]` 与 `[Dock]` 6 字段，不落该段）。
  - 验证：`App/main.cpp` Smoke 新增主题检查点（`theme-*` 共 10 项：`skin.ini` 的 Dark 基线取色贯通 / Dock 应用与保存 / 临时主题夹具写入 / 主题合并与变量展开 / 主文件优先 / `[Colors]` 填充 / `SmokeA→SmokeB` 切换 / 主题缺失降级不阻断）；`Tests/test_skin_load.cpp` 新增第 4 节主题合并断言（Dark 基线取色、皮肤自有变量不被主题覆盖、Dock `[Colors]` 同源并入），`Tests/test_dock_core.cpp` 新增 `PanelColor` 默认 `#40000000` / 空值忽略 / `ThemeName` 默认空与存取，`Tests/test_dock_drop.cpp` 写回格式断言由「7 字段」修正为 `[Theme] Name=` + `[Dock]` 6 字段且不写 `Transparency`、并补 `[Theme] Name=` 存读往返一致；两线全量构建 + 全量回归全绿（OFF Smoke 102 项 / ON Smoke 104 项 = D25 基线 92/94 加主题 10 项；OFF 5/5、ON 5/5 测试程序全部 ALL TESTS PASSED，日志归档于 `logs/build_20260918_205223.log`）。

## Phase 4 — 配置与持久化（Week 7）

- [x] D31-35（2026-09-18）：统一配置管理、INI 持久化、Dock items.ini
  - 交付：`ConfigParser` 由「只读 + 内存写」补齐为**统一落盘出口**，新增 `SaveFile(path)` / `LoadFileRaw(path)` / `SetHeaderComment` / `GetHeaderComment` / `RemoveValue(section,key)`（键删空后自动移除空段）/ `RemoveSection(section)`；为保住生成文件与既有手写格式**逐字节一致**，引入三套顺序视图 `m_SectionOrder`（段出现序）/ `m_KeyOrder`（段内键插入序）/ `m_HeaderComments`（文件头注释）——`LoadFromString` 回填、`SetValue/SetVariable` 登记、`SaveFile` 按插入序输出（UTF-8 无 BOM / CRLF）。`LoadFileRaw` 刻意**不**并入主题（跳过 `ApplyThemeDefaults`），否则「读→改→写」会把主题键展开进主文件、污染 `[Theme] Name=` 声明。`DockBar` 的 `SaveConfig` / `WriteItemsIni` 改走 `ConfigParser`，**DD-7 既定例外（裸 `std::ofstream`）作废**，`DockBar.cpp` 移除 `<fstream>`，生成文件字节格式不变（首行注释经 `SetHeaderComment` 保留）。`CommandHandler` 新增 `!WriteKeyValue Section Key Value [File]`（缺省写当前皮肤 ini，相对路径按皮肤目录解析；引号感知分词支持值与路径含空格、`[Section]` 方括号写法），补齐「内存改 → 落盘」链路。
  - 验证：`App/main.cpp` Smoke 新增 `cfg-*` 7 项（`SaveFile` 写盘 / **字节精确比对**（段序 `Zeta→Alpha`、键序 `B→A` 均与字典序相反）/ `LoadFileRaw` 往返与清脏 / 读→改→写保序保注释 / `RemoveValue` 删键后空段消失且落盘正确 / `RemoveSection` 首删 true 重删 false / `LoadFileRaw` 不合并主题（读到默认 `#none`，而 `LoadFile` 读到主题 `#abcdef`））；`Tests/test_bang_commands.cpp` 新增第 4.5 节 `!WriteKeyValue` 断言（引号值含空格 / 首行注释保留 / `[Section]` 等价 / 覆盖既有键 / 缺参静默忽略），夹具落构建产物目录、不污染仓库内皮肤文件；两线全量构建 + 全量回归全绿（OFF Smoke 109 项 / ON Smoke 111 项 = D26-30 基线 102/104 加 7 项；OFF 5/5、ON 7/7 测试程序全部 `exit=0` 且 0 项 FAIL，日志归档于 `logs/build_20260918_212006.log`）。

## Phase 5 — 测试与发布（Week 8）

- [x] D36-40（2026-09-18）：集成测试 + 性能优化
  - 交付：「集成测试」按「扩展 Smoke 端到端区段」落地 —— `App/main.cpp` 新增区段 10)「集成测试闭环 + 性能优化确定性断言」，含 4 个夹具常量（`kIfMatchIni` / `kStableIni` / `kThemeBodyDark` / `kThemeBodyLight`）与 6 个检查点：`e2e-real-skin`（真实皮肤 `Skins/example/skin.ini` 全量 `Load`，贯通主题合并 + Measure/Meter 工厂 + `MeasureName` 绑定，实测 21 Measure / 28 Meter）、`e2e-theme-switch-reload`（同一皮肤体改 `[Theme] Name` 后 `Reload()`，等价 `!Refresh`，首个 Meter 文本 `SMOKE-DARK → SMOKE-LIGHT`）、`e2e-reload-ini-path`（重载后 `GetIniPath()` 不漂移）。性能优化只取两项高收益：① **#1 IfMatch 正则编译缓存** —— `Library/Pcre.h` 增进程级 `s_CompileCount` 计数器与 `GetCompileCount()` / `ResetCompileCount()`，`Library/IfActions.h/.cpp` 增 `MatchRegexCache`（按条件下标缓存已编译的 `pcre16` 句柄），消除「每次 `Update` 都重编译同一表达式」的重复开销，配 `perf-ifmatch-compile-once`：连续 25 次 `Update` 后编译次数恒为 1；② **#2 Skin 脏检查** —— `Library/Skin.cpp/.h` 在 `RenderFrame` 前用 FNV-1a 64 把「会影响画面」的状态压成 64 位指纹，与 `m_LastFingerprint` 相同且 `m_Dirty` 为假时整帧跳过（不 `Clear` + 不 `Render`），`!Redraw` 经 `m_Dirty` 强制重绘一帧；新增 `GetFrameCount()` / `GetRenderCount()` / `GetSkippedRenderCount()` 三个确定性计数，配 `perf-dirty-skip-render`（静态画面下 `renders==0` 且 `skipped==frames`）与 `perf-bang-redraw`（`!Redraw` 后 `renders` 恰 +1）。
  - 修复：ON 线 Smoke 稳定复现「区段 10.1/10.2 PASS 后 10.3 崩溃（`0xC0000005`）」。二分探针定位（`probe-stable-shown` 之后 `probe-loop-done` 未达 → 锁在 1.5s 消息循环；逐消息探针 `iter=0 msg=0x16 frames=1`，`std::to_string` 输出十进制，16 即 `WM_CLOSE`）确认根因：`DockWindow::Stop()` 走的 DuiLib `CWindowWnd::Close()` 实为 `PostMessage(WM_CLOSE)`（异步，`third_party/duilib/DuiLib/Core/UIBase.cpp:344-358`），而调用方 `Stop()` 返回后立即 `CPaintManagerUI::Term()` 释放 `ImageHash` / `CustomFonts` / `StyleHash` 等共享资源；队列里残留的 `WM_CLOSE` 随后被 10.3 的消息循环派发 → `__WndProc → WM_NCDESTROY → WindowImplBase::OnFinalMessage → m_pm.ReapObjects()` 访问已释放资源。修复：`Dock/DockWindow.cpp` 的 `Stop()` 改用 `::SendMessageW(m_hWnd, WM_CLOSE, 0, 0)` 同步关闭 —— `Stop()` 返回时窗口必已销毁、`OnFinalMessage` 已在 DuiLib 仍有效的状态下执行完毕，且 `DestroyWindow` 会顺带丢弃队列中该窗口的残留消息（含 6 行中文注释说明「为什么必须同步」）；`docs/DOCK_DESIGN.md` §3.3 生命周期图与 `Stop()` 注释同步更新。
  - 验证：性能效果一律用**确定性计数**而非墙钟时间断言，故可直接纳入两线常态化回归。两线全量构建 + 全量回归全绿：OFF Smoke **115/0**、ON Smoke **117/0**（= D31-35 基线 109/111 各加本阶段 6 项），关键读数逐字 `perf-ifmatch-compile-once | updates=25 pcre16_compile=1`、`perf-dirty-skip-render | frames=13 renders=0 skipped=13`、`perf-bang-redraw | renders 1→2`；OFF 线 5/5、ON 线 7/7 测试程序全部 `exit=0` 且 `ALL TESTS PASSED`；日志归档于 `logs/build_20260918_233725.log`（含探针版复核 `PASS=136` 与移除探针后的最终 `PASS=117`）。
- [x] D41-42（2026-09-19）：文档（【文档工程师 Agent】）+ README + 发布
  - 交付：新增 `docs/USER_MANUAL.md`（9 章：安装运行 / 皮肤语法 / Measure 参考 / Meter 参考 / 主题系统 / Dock 栏配置 / Bang 命令参考 / 故障排查 / 许可证；全部键名、默认值与合法区间逐条对照源码核实，含 6 处初稿事实校正 —— `MeterString` 字号默认 12、`MeterBar`/`MeterLine` 颜色默认具体值、`Dock.ini` 各键越界行为、拖拽加项 5 条精确规则、`Dark.ini` 变量清单剔除不存在的 3 个 `Accent*`）；重写 `README.md`（删除「脚手架 + 骨架 / 不保证可编译 / C++17 / VS 2022 / 构建不可用」等过期表述，补齐 D1-D40 已交付能力、两线构建命令、Smoke 基线 115/0 与 117/0、目录结构与文档导航）；新增 `CHANGELOG.md`（Keep a Changelog 格式，按 Phase 0-5 归纳 D1-D40，含 v0.1.0 发布说明、修复项、已知限制）；新增 `scripts/package.ps1`（版本号从 `CMakeLists.txt` 的 `project(... VERSION)` 解析，收集 exe + `Skins/` + `LICENSE`/`README.md`/`CHANGELOG.md`/`docs/USER_MANUAL.md` 打为 `dist/RainDeskPlus-<版本>-win64.zip`，暂存目录与产物均在项目内、不落 C 盘）；`.gitignore` 补 `dist/`。
  - 验证：`.\scripts\package.ps1` 打包成功 → `dist\RainDeskPlus-0.1.0-win64.zip`（858,157 字节，17 个条目）；**发布包端到端验证**：解包到 `dist\_verify` 后以 `RAINDOCK_SMOKE_MS=1500` 实跑 `RainDeskPlus.exe`，进程退出码 0、`smoke_result.txt` 末行 `SMOKE SUMMARY PASS=117 FAIL=0`，与源码构建线 ON 线基线一致；文档事实核对全部基于实读源码（Bang 清单取自 `CommandHandler.cpp` 的 13 条 `RegisterBang`、鼠标动作键取自 `Mouse.cpp` 的 21 项静态表、默认值取自各 `Meter*.h` / `Measure*.cpp` 成员初始化）。日志归档于 `logs/build_20260919_084358.log`。
  - 发布：commit `9c94bd2`（`feat(release): D41-42 文档三件套 + 发布打包（v0.1.0）`，88 files changed / +10175 −445，提交前已完成暂存区敏感信息扫描，无异常）→ 附注 tag `v0.1.0` → 推送 `origin main`（`6717094..9c94bd2`）与 tag → GitHub Release `v0.1.0`（https://github.com/201921wrk/RainDeskPlus/releases/tag/v0.1.0 ）附 `RainDeskPlus-0.1.0-win64.zip`（858,157 字节）。

## 每日 AI 协作流程

```
晨：向【架构师 Agent】描述当日功能 → 拿设计方案
   ↓ 向【编码助手 Agent】索要代码
   ↓ 集成、编译、测试
   ↓ 有错 → 把错误发给【编码助手 Agent】修复
暮：【代码审查 Agent】审查当日代码
```

## Agent 角色与提示词

见 [AGENT_PROMPTS.md](AGENT_PROMPTS.md)。

| 阶段 | 主 Agent | 任务 |
|------|---------|------|
| Phase 0 | 架构师 + 编码助手 | VS 配置 / 编译脚本 / 目录结构 |
| Phase 1 | 架构师 | 模块依赖、裁剪方案 |
| Phase 1 | 编码助手 | 提取后的核心引擎代码 |
| Phase 1 | 架构师 + 编码助手 | DockBar 设计与实现 |
| Phase 2 | 编码助手 | Measure/Meter 集成 |
| Phase 3 | 编码助手 | 皮肤加载 / 切换 / 主题 |
| Phase 5 | 测试 + 文档工程师 | 测试用例 / 用户文档 / README |
