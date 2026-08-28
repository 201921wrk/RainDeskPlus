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
- [ ] M3：`MeterString` 经 Direct2D 渲染出文本。
- [ ] M4：`Skin` 加载一个最简皮肤并显示。
- [ ] M5：`CRainmeter` + `CommandHandler`，Bang 命令切换皮肤成功。
