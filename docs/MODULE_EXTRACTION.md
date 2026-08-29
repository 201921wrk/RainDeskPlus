# Rainmeter 模块提取方案

> Phase 1 「核心引擎提取」操作手册。说明从 Rainmeter 上游源码裁剪、提取、整合到本仓库 `Library/` 的步骤与裁剪策略。

## 1. 上游源码获取

```powershell
# 1) 安装 git（本机当前缺失），配置身份
# 2) 克隆上游（注意本机 hosts 把 github.com 指向 127.0.0.1 MITM 代理，TLS 可能失败）
git clone https://github.com/rainmeter/rainmeter.git rainmeter-upstream
# 或用脚本：scripts/fetch_rainmeter.ps1
```

**TLS 注意**：本机 hosts 把 github.com 等 ~25 个域名映射到 `127.0.0.1`，由 GitHub 加速器（FastGithub/Watt 类）MITM 代理。若 `git ls-remote` 因自签 CA 失败：
- 把该代理根 CA 加入系统受信任根证书库；或
- 临时注释 hosts 中相关条目直连；或
- 使用 `git -c http.sslVerify=false`（仅临时排错，**不要**长期关闭校验）。

提取后的源码进入 `Library/`，上游克隆留在 `rainmeter-upstream/`（已被 `.gitignore` 忽略，不入仓）。

## 2. 提取范围（直接复用）

以下模块对应 Rainmeter `Library/` 下的同名文件，**直接拷贝**到本仓库 `Library/`：

| 本仓库文件 | 上游文件 | 功能 | 复用度 | 依赖 |
|-----------|---------|------|--------|------|
| `Rainmeter.cpp/h` | `Library/Rainmeter.cpp/h` | 单例 / 生命周期 | 简化复用 | 替换窗口管理 |
| `Measure.cpp/h` | `Library/Measure.cpp/h` | Measure 基类 | 直接 | 无 |
| `MeasureCPU.cpp/h` | `Library/MeasureCPU.cpp/h` | CPU | 直接 | Pdh |
| `MeasureMemory.cpp/h` | `Library/MeasureMemory.cpp/h` | 内存 | 直接 | `GlobalMemoryStatusEx` |
| `MeasureNet.cpp/h` | `Library/MeasureNet.cpp/h` | 网络 | 直接 | IP Helper |
| `MeasureTime.cpp/h` | `Library/MeasureTime.cpp/h` | 时间 | 直接 | 无 |
| `MeasurePlugin.cpp/h` | `Library/MeasurePlugin.cpp/h` | 插件加载 | 直接 | DLL 机制 |
| `Meter.cpp/h` | `Library/Meter.cpp/h` | Meter 基类 | 直接 | 无 |
| `MeterString.cpp/h` | `Library/MeterString.cpp/h` | 文本 | 直接 | DirectWrite |
| `MeterImage.cpp/h` | `Library/MeterImage.cpp/h` | 图像 | 直接 | WIC/D2D |
| `MeterBar.cpp/h` | `Library/MeterBar.cpp/h` | 进度条 | 直接 | D2D |
| `MeterLine.cpp/h` | `Library/MeterLine.cpp/h` | 折线 | 直接 | D2D |
| `Skin.cpp/h` | `Library/Skin.cpp/h` | 皮肤容器 | 简化 | 窗口→Duilib |
| `ConfigParser.cpp/h` | `Library/ConfigParser.cpp/h` | INI 解析 | 直接 | 无 |
| `CommandHandler.cpp/h` | `Library/CommandHandler.cpp/h` | Bang | 直接 | 无 |

## 3. 需要修改/扩展的部分

| 模块 | 修改内容 | 原因 |
|------|---------|------|
| `CRainmeter` 窗口管理 | 替换为 Duilib 窗口体系 | 挂件 + Dock 统一渲染 |
| `Skin` 窗口创建 | 改为 Duilib 窗口创建 | 原生 Win32 窗口需替换 |
| 主题系统 | 扩展原有主题机制 | 支持 Dock + 挂件统一主题 |
| `Measure`/`Meter` 渲染入口 | 接入 Direct2D `ID2D1RenderTarget` | 替代 GDI+ |

## 4. 裁剪清单（不上游依赖的部分，可暂不提取）

- `Library/MeasurePlugin*` 之外的 `Plugins/` 官方插件 DLL：按需提取，先跳过。
- `Common/` 公共工具：提取时一并带入（如 `StringUtil`、`FileUtil`、`Util`），随依赖补齐。
- `SkinInstaller/`：MVP 不需要，跳过。
- `Language/`：本地化资源，按需提取。
- `Restart/`、`sspt/`：跳过。
- `Application/` 入口：用本仓库 `App/main.cpp` 替代。

## 5. 集成顺序（建议）

1. 先拷贝无外部依赖的：`ConfigParser`、`MeasureTime`、`Measure` 基类、`Meter` 基类。编译通过。
2. 再加 `MeasureCPU` / `MeasureMemory`（依赖性能 API / 内存 API），补系统库链接。
3. 加 `MeasureNet`（IP Helper）、`MeasurePlugin`（DLL 加载）。
4. 加 `Meter*`（Direct2D/WIC），打通渲染上下文。
5. 接 `Skin` + `CRainmeter`，替换窗口创建为 Duilib。
6. 接 `CommandHandler`，验证 Bang 命令。

每一步在 CMake 下编译通过再进下一步，避免一次性大规模集成导致编译错误堆积。

## 6. 版权与许可证合规

- 每个从 Rainmeter 派生的源文件**必须保留**上游 GPL v2 版权头与原作者署名。
- 新写文件（Dock/）添加本仓库 GPL v2 头，注明 `Copyright (C) 2026 RainDeskPlus Project`。
- 仓库根 [LICENSE](../LICENSE) 为 GPL v2 全文。
- 仓库根 README 注明派生自 Rainmeter 及上游仓库链接。

## 7. 验证里程碑

- [x] **M1（2026-08-28，vs2026-x64 Debug，clean rebuild：0 error / 0 warning，4 目标全产出）**
  - 提取策略修正：**非一次性拷贝上游 14 个文件**（上游每个 Measure 拉 IfActions/LocaleUtil/Common/* 一串依赖，导致 100+ 错误爆炸），改为「就地升级骨架」：保留对外 API 不变，逐模块写入 Rainmeter 等价逻辑+公共字段，每步编译通过后再进下一步。
  - `ConfigParser` 真可用：INI 按段解析 + `m_SectionOrder` 保留段序（对齐 Rainmeter 原生加载顺序） + `#Var#`/`$$` 变量展开（最多 3 pass 嵌套）+ `ReadUInt/ReadUInt64/ReadColor/ReadRect/ReadFloats/GetVariable/SetVariable` 完整 API；通过 `<d2d1_1.h>+<windows.h>` 解决 D2D1_COLOR_F/RECT SDK 类型与本头冲突（C2371）。
  - `Measure` 基类对齐 Rainmeter 生命周期：`Measure(Skin*,name)` → `Initialize → ReadOptions(parser,section)` → `Update(bool rereadOptions)` 按 `UpdateDivider` 跳帧 → `UpdateValue()=0` 纯虚子类采样；公共字段 `m_MinValue/m_MaxValue/m_Invert/m_Disabled/m_Initialized` 提至 `protected`（子类频繁读写，C2248 修复）；`GetValue` 做 Invert + clamp；`GetString` 经 `CheckSubstitute` 字面量替换；Substitute 按"引号-逗号"解析。
  - `MeasureTime` 真格式化：`Format` 字符串经 `wcsftime` / `_wcsftime_l`（`_create_locale` 给 FormatLocale）、`DaylightSavingTime=0` 走 `gmtime_s` + `_get_timezone` 固定偏差（CRT 无公开 `_set_daylight`，避免改全局 `_daylight` 弃用）、`TimeStamp` 数字秒、`TimeZone` 分钟偏移，输出经 `CheckSubstitute`。
  - `MeasureCPU` 真采样（整机）：构造 `m_MaxValue=100/m_MinValue=0`；`Processor=0`（默认）用 `GetSystemTimes()` 差分 Idle/(Kernel+User+Idle) Δ → 百分比 `[0,100]`；`Processor≥1` M1 降级到 Total（避免引入 ntdll `NtQuerySystemInformation` + 40+ 行单核解析）。
  - `MeasureMemory` 真采样（Phys+PageFile 口径，对齐上游 Rainmeter MeasureMemory 语义而非纯物理）：构造用 `GlobalMemoryStatusEx` 预设 `m_MaxValue`；`ReadOptions` 用 oldMax 守卫（用户手写 `MaxValue=` 优先级最高 + 基类不会错覆盖）；每次 `UpdateValue` 重算 `m_MaxValue=ullTotalPhys+ullTotalPageFile`（pagefile 动态）；`Total=1` 强制返回 TotalBytes；支持 `Mode=UsedPercent(dwMemoryLoad)/UsedBytes/FreeBytes/TotalBytes`。
  - `MeasureNet` / `MeasurePlugin` 占位对齐新 Measure 签名（真采样留 M2）。
- [x] **M2（2026-08-28，vs2026-x64 Debug，clean rebuild：0 error / 0 warning，4 目标全产出；Smoke 11/0 全过）**
  - `MeasureNet` 真采样：升级 `GetIfTable2 + MIB_IF_ROW2`（**64 位计数器**，上游 GetIfTable/MIB_IFROW 的 32 位计数器在现代网卡驱动上普遍错误上报且回绕）；`Interface=Best`（默认，非回环 In+Out Octets 最大、优先 OPERATIONAL）/`Total`（非回环求和）/`<N>`（按 InterfaceIndex）；`Cumulative=0` 输出速率（Δbytes/Δ秒）、`=1` 输出累计值；`Type=` 支持 In/OutOctets、In/OutError、In/OutUcast、In/OutMulticast（MIB_IF_ROW2 专用 `In/OutMulticastOctets` 字节计数）。
  - **头文件陷阱**：`netioapi.h` 的 MIB 结构由 `_WS2IPDEF_` 宏解锁 → 必须显式 `#include <ws2ipdef.h>`（在 winsock2.h 之后、iphlpapi.h 之前），否则 C2061 `MIB_IF_ROW2` 未声明。
  - `MeasurePlugin` DLL 加载骨架：`LoadLibraryW` + `GetProcAddress` 绑定 Rainmeter 插件五导出（`Initialize/Reload/Update/GetString/Finalize`），真插件联调留后续。
  - **真 bug 修复 ×2（Smoke 验证发现）**：
    1. `MeasureMemory` UsedPercent 模式错误地把 `dwMemoryLoad` 百分比再按 MaxValue 缩放 → 直接返回百分比（`m_MaxValue` 是字节总量，供 Bar 相对显示，与百分比无关）。
    2. `Measure.h` `m_MaxValue` 默认 `1.0` 导致 Net 等无界 Measure 被 GetValue clamp 成 1.0 → 默认改 `0.0` =「未设置范围」语义，仅 `m_MaxValue > m_MinValue` 时才 clamp/Invert（CPU 等显式建范围的子类不受影响）。
  - **Smoke 测试入口**（`App/main.cpp`）：程序内生成 `smoke.ini`（Variables/Time/CPU/Memory/Net 段）→ ConfigParser LoadFile → 验证段序/变量展开/SetVariable/ReadInt → 各 Measure Initialize→Update×2 → 打印 GetValue/GetString → 结果落盘 `build/vs2026/Debug/smoke_result.txt`；另含 GetIfTable2 接口级计数器 dump 诊断（验证 Best 选择数据源）。验证：**PASS=11 FAIL=0**，Net 累计 445,158,716 与系统计数器一致，`CRainmeter::Initialize` ok。
  - Meter\* D2D 真渲染准备顺延 M3（本里程碑聚焦采样链路闭环）。
- [x] **M3（2026-08-29，vs2026-x64 Debug，clean rebuild 0 error / 0 warning；Smoke 17/0 全过）**
  - 新增 `Canvas.h/cpp`（对应上游 Library/Canvas 的 D2D 重构版）：进程级共享 `ID2D1Factory`（SINGLE_THREADED）/ `IDWriteFactory`（SHARED）/ `IWICImagingFactory` 单例 + `FinalizeAll`；`CreateOffscreenTarget` 产出 WIC 软件渲染目标（显式 96 DPI，1 DIP=1px，Smoke 断言不受系统 DPI 缩放影响）；`SaveBitmapToPng`（WIC stream→PNG encoder）供人工核验。
  - `MeterString` 真渲染：`Initialize` 读 `Text/FontFace/FontSize/FontColor/StringAlign`；`Text=%1` 引用绑定 Measure 字符串（对齐上游 Text 键语义）；无 Text= 时直接取 Measure->GetString()。布局用 `IDWriteTextLayout`（先大画布建 layout → GetMetrics → SetMaxWidth/Height 收口），AutoSize 语义下文本 metrics 决定 m_W/m_H；`StringAlign` 三值映射 DWRITE_TEXT_ALIGNMENT。`Draw` = brush 惰性创建（与 rt 绑定，rt 变更/析构经 COM 引用计数保安全）+ `DrawTextLayout`。
  - `Meter` 基类补公共定位：X/Y/Hidden。
  - **Smoke 新增 6 项**：com-init（WIC 需 CoInitializeEx STA）/ canvas-offscreen 400x80 / meterstring-metrics（278x21，`RainDeskPlus M3 | 2026-08-29 00:20:57`）/ enddraw / **d2d-render（alpha>8 命中 1222 px）** / save-png（`smoke_text.png` 人工核验通过）。入口注意：GUI 子系统下 PowerShell 直接 `& exe` 不等待，需 `Start-Process -Wait`。
  - **顺手修复项目迁移遗留**：仓库由 `f:/desktop_beautiful/beautiful_app` 迁至 `f:/codeTrae_project/beautiful_app` 后，`build/vs2026/CMakeCache.txt` 仍指旧路径导致 configure 报 "cache directory is different"——删除整个 build/vs2026 重新 configure 解决（clean rebuild 顺带完成）。
- [x] **M4（2026-08-29，vs2026-x64 Debug，clean rebuild 0 error / 0 warning，6 目标全产出；Smoke 22/0 全过）**
  - `Skin` 真实现（窗口宿主）：`Load` = `[Rainmeter] Update=` 周期 + **两遍构建**（先全部 Measure 再 Meter，MeterString 绑定时能拿到字符串初值）+ `MeasureName` 绑定；工厂：Time/CPU/Memory/Net + String（Image/Bar/Line/Roundline 留 M5+）。`Show` = `WS_POPUP|WS_EX_TOOLWINDOW|WS_EX_TOPMOST` 挂件风格窗口 + `CreateHwndRenderTarget`（SOFTWARE 类型、显式 96 DPI 与 M3 离屏度量一致）+ `SetTimer(Update=)`。WndProc 经 `GWLP_USERDATA` 绑 Skin*；WM_TIMER → `RenderFrame()`（Update → Clear 深色背景 → Meter.Draw → EndDraw 成功才计帧）；AutoSize 窗口尺寸 = Meter 包围盒 + 边距。
  - `MeterString::Update` 动态文本：新增 `ResolveText()` 每帧按原始 `Text=%1` 重组（时钟走字必需），`SetText` 变化时才 relayout。
  - **Smoke 新增 5 项**：skin-ini-write / skin-load（measures=1 meters=1）/ skin-window-create / skin-visible（IsWindowVisible）/ **skin-frames=5**（2.2s 消息循环 ≥3 帧期望）。皮肤窗口真实显示 2.2s 后随进程退出。
  - **工程经验（重要）**：沙盒/脚本里跑 GUI 子系统 exe 必须 `Start-Process -RedirectStandardOutput`，否则 exe 继承 PowerShell 管道句柄导致命令"挂起"不返回——曾被误判为启动崩溃（SmokeLog 增量落盘为排查手段：Record 即时写盘，崩溃不丢日志）。
  - 协作记录：并行会话同期新增 `Tests/` 3 个独立 Smoke exe（RenderTest/SkinTest/BangTest）+ CMakeLists target + `Measure::GetString` 数值回退格式化（`m_StringValue` 空时按 `%f.1` 输出，保证纯数值 Measure 的 %1 有意义）——与本里程碑改动文件级无冲突，clean rebuild 一并编译通过。
- [x] **M4.5 Common 直拷批（2026-08-29，vs2026-x64 Debug，0 error / 0 warning；Smoke 24/0 全过）**
  - 上游完整源码就位：`rainmeter-upstream/`（origin=github.com/rainmeter/rainmeter，HEAD `abd0b5a`，Library 106 cpp + 104 h + Common/ThirdParty 齐全，.gitignore 已排除）。提取策略由「就地升级骨架」升级为「**上游真代码直拷 + 编译驱动适配**」。
  - **第一批落地（Common 基础设施，与上游路径同构）**：`Common/{StringUtil,MathParser,Timer,FileUtil,PathUtil,ParseUtil,StringParser,RawString}` 直拷（namespace 风格与 raindock 模块无冲突）；`Library/Util` 因重依赖上游资源系统（Language/DialogDebug）未拷，待 Section 体系引入时按需裁剪重建。
  - 适配点（均已标注 `upstream patch` 注释）：① 轻量版 `Common/StdAfx.h`（上游版含 d2d1_1/wrl/ankerl unordered_dense，本项目不需要）；② **项目 C++ 标准 17→20**（上游用 `std::numbers`/`string_view::starts_with`）；③ StdAfx 补 Shlobj.h（SHGetFolderPath）；④ 三处 /W4 警告修复（MathParser C4244、StringUtil C4245、ParseUtil C4456 遮蔽）；⑤ 新增链接 `imagehlp`（FileUtil PE 校验）。
  - 生效验证：Smoke 新增 `common-mathparser`（`(2+3)*4+22/7`=23.142857 精确命中）+ `common-stringutil`（EqualsIgnoreCase/WidenUTF8）。
  - **后续批次路线**（编译驱动，每批 Smoke 回归）：
    1. ~~Common 基础设施~~（本批）
    2. **语义核心**：`Section`（Measure/Meter 共同基类归位）+ `IfActions` + `Group` + `Mouse`——对齐上游继承树后，所有 Measure/Meter 可直接对照拷贝
    3. **ConfigParser 完整化**：@Include、公式 `(…)`（依赖本批 MathParser）、嵌套变量、动态变量
    4. **Meter 家族**：MeterImage/Bar/Line/RoundLine/Shape——ReadOptions/尺寸逻辑照抄上游，Draw 用本项目 D2D Canvas 重写（上游 GDI+ 渲染栈不拷）
    5. **Measure 家族轻量批**：DiskSpace/Registry/Uptime/Calc/Loop/Quote/UsageMonitor/PhysicalMemory/VirtualMemory
    6. **重量项**：WebParser(PCRE)/Script(Lua)/Plugin 生态
- [ ] M5：`CRainmeter` + `CommandHandler`，Bang 命令切换皮肤成功。

## 8. 上游核心模块盘点清单（2026-08-29 对照 `rainmeter-upstream/` HEAD `abd0b5a`）

> 本清单为 §7 批次路线（M4.5 条目）的逐模块展开版；批次编号 L2/L3/L4/P2 沿用 §7 的 1-6 批映射。
> 状态图例：✅ 真实现（Smoke 验证）｜🟡 骨架/部分实现｜📦 已直拷（Common 第一批）｜❌ 缺失待提取｜🚫 本项目不采用
> 上游规模：`Library/` 106 cpp + 104 h，`Common/` 22 cpp（含 5 个 `*_Test.cpp`）+ 24 h，`ThirdParty/` 9 个第三方库。

### 8.1 Common 工具层（上游 `Common/`，共 24 组）

| 模块 | 状态 | 说明 / 依赖 |
|------|------|------------|
| StringUtil | 📦 | 字符串工具全集（Widen/Narrow/大小写/URL/转义），被全库引用 |
| MathParser | 📦 | 公式求值引擎（基于 ccalc），ConfigParser `(…)`、MeasureCalc 的核心 |
| Timer | 📦 | 高精度计时（QPC），header-only |
| FileUtil | 📦 | 文件读写/临时文件/PE 校验，依赖 imagehlp |
| PathUtil | 📦 | 路径规范化/展开，SHGetFolderPath |
| ParseUtil | 📦 | INI 键值/颜色/公式/布尔解析（ParseColor 用 D2D1_COLOR_F） |
| StringParser / RawString | 📦 | 字符串视图解析器 / SSO 字符串类 |
| CharacterEntityReference | ❌ | HTML 实体解码——WebParser 前置，P2 批 |
| CriticalSection | ❌ | RAII 锁——Skin 线程化（后台 Measure 更新）前置，L2 批 |
| DirectoryWatcher | ❌ | 文件变更监视——皮肤热重载前置，L2 批 |
| DpiUtil | ❌ | DPI 感知（PerMonitorV2）——多屏挂件缩放，M4 窗口增强前置 |
| NetworkUtil | ❌ | 网络 MAC/连接枚举——MeasureNet 增强对照 |
| Platform / Version / ScopedFunction / Map | ❌ | 系统版本判断 / 小工具，随用随拷 |
| PdhUtil | ❌ | PDH 查询封装——PerfMon/UsageMonitor/AdvancedCPU 前置 |
| Dialog / MenuTemplate / ControlTemplate | 🚫 | 上游 Win32 对话框体系，本项目 Dock 走 Duilib，不采用 |
| CrashDump / UnitTest / `*_Test.cpp` | ❌ | 工程设施，本项目用 Smoke 体系替代，暂不拷 |

### 8.2 Library 语义核心层（提取关键路径）

| 模块 | 状态 | 说明 |
|------|------|------|
| Section | ❌ | **Measure/Meter 共同基类**（名称/Skin 引用/Initialize 骨架/Update 分发）。对齐它之后上游所有 Measure/Meter 拷贝摩擦最小——**批次 2 的第一优先** |
| Group | ❌ | 段分组（Group= 键 + Bang 的组操作），Section 直接持有 |
| IfActions | ❌ | IfCondition/IfAbove/IfBelow/IfEqual 动作体系——皮肤逻辑的灵魂 |
| Mouse | ❌ | 鼠标动作（LeftMouseDown/Over/…），Section 持有，交互必需 |
| ConfigParser | 🟡 | 已有：段序/变量展开/ReadColor/ReadRect。缺：@Include、`(…)`公式（MathParser 已就位）、嵌套/动态变量、KeyWWWW 选择器 |
| CommandHandler | 🟡 | 已有 DoBang 占位。缺：上游 Bang 全集分发（!SetOption/!UpdateMeter/!Redraw/…） |
| CRainmeter | 🟡 | 已有单例骨架。缺：Config/SkinRegistry/托盘/多皮肤管理 |
| Skin | 🟡 | M4 已真实现窗口宿主+渲染循环。缺：变量缓存、Mouse 动作派发、游走 Dragging、Occlusion |
| Measure / Meter 基类 | 🟡 | 生命周期真实现；缺 Section 化、变换/裁剪/Tooltip 等公共项 |
| Logger | ❌ | 日志（文件+Debugger），Debug=1 调试必需 |
| System | ❌ | 全局系统事件（显示器/电源/多屏 MonitorChange），多屏挂件前置 |
| MonitorUtil / SkinRegistry / SkinPosition / ContextMenu | ❌ | 多屏定位/皮肤注册/右键菜单——挂件管理期（Phase 2）提取 |
| WindowOcclusionTracker | ❌ | 上游 D2D 渲染优化（遮挡暂停渲染），M4 渲染循环节能增强 |
| ImageCache / ImageOptions / GeneralImage | ❌ | 图像加载缓存体系——MeterImage/MeterBar/Histogram 的共同底座（WIC/D2D 位图） |
| AsyncTask | ❌ | 异步任务——WebParser/ImageCache 前置 |
| Pcre | ❌ | 正则（第三方 pcre）——Substitute 正则模式/WebParser 前置 |
| LuaScript / LuaHelper / LuaBindingSection | ❌ | Lua 脚本体系（第三方 luajit/luautf8），P2 批 |
| LocaleUtil | ❌ | 区域格式（数字/日期本地化输出） |
| Net | ❌ | MeasureNet 共享辅助（接口统计缓存），MeasureNet 增强时对照 |
| Util | ❌ | 首批被资源系统（Language/DialogDebug）阻断，Section 批裁剪重建 |
| Language / TrayIcon / UpdateCheck / GameMode / Export | 🚫/❌ | Language 资源化、托盘、更新检查、游戏模式、插件导出——Phase 2+ 按需 |
| DialogAbout/Manage/Debug/Install/NewSkin/Package, SkinInstaller, SkinDropTarget, SkinSelectionOverlay | 🚫 | 上游 Win32 UI 家族，本项目不采用（Dock/Duilib 体系） |

### 8.3 Measure 家族（上游 44 项）

| 批次 | 模块 | 依赖 |
|------|------|------|
| ✅ 已真实现 | Time / CPU / Memory / Net(+In/Out/Total) / Plugin(骨架加载) | — |
| **L4a 轻量高价值** | DiskSpace / Uptime / Calc（用 MathParser）/ Loop / Quote / String / Registry / PhysicalMemory / VirtualMemory / UsageMonitor（PdhUtil） | 无/Pdh |
| **L4b 中等** | AdvancedCPU+Process / Power / Ping / SysInfo / WifiStatus / WindowMessage / ActionTimer / MediaKey / Audio+AudioLevel（kiss_fft）/ FolderInfo / FileView / RecycleManager / RunCommand | 各系统 API |
| **L4c 重量生态** | WebParser（pcre+zlib+wininet）/ Script（luajit）/ PerfMon / ResMon / NowPlaying / iTunes / CoreTemp / SpeedFan / DragDrop / InputText / Mouse(measure) | 第三方 |

### 8.4 Meter 家族（上游 13 项）

| 批次 | 模块 | 渲染策略 |
|------|------|---------|
| ✅ | String（D2D/DirectWrite 真渲染） | 本项目 Canvas |
| 🟡 骨架待对照 | Bar / Image / Line | ReadOptions/尺寸照抄上游；**Draw 用 D2D 重写**（上游 GDI+ 栈不拷，见 §3） |
| ❌ L3 批 | StringBase（String 公共逻辑基类，StringEffect/ClipString 之家）/ RoundLine / Shape / Histogram / Button / Bitmap / Rotator / Svg | 同上；Shape/Svg 依赖上游较新图形代码，列 P2 |

### 8.5 第三方依赖对照（上游 `ThirdParty/`）

| 库 | 用途 | 引入时机 |
|----|------|---------|
| pcre | 正则（Substitute regex / WebParser） | L2 Substitute 正则化 / L4c |
| luajit + luautf8 | MeasureScript | P2 |
| kiss_fft | MeasureAudio 频谱 | L4b |
| zlib | WebParser gzip / 包管理 | L4c |
| ankerl unordered_dense | 上游容器优化 | 不引入（本项目 std 容器） |
| fmt / rapidjson / inipp | 上游现代化改造 | 不引入（避免双栈） |

