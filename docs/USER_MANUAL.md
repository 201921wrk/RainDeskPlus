# RainDeskPlus 用户手册

> 适用版本：**v0.1.0** ｜ 平台：Windows 10 / 11 x64 ｜ 许可证：GPL v2
>
> **截图说明**：当前版本尚未提供界面截图，本文所有「界面效果」均以文字描述 + INI 片段说明，
> 图片占位（`docs/images/*.png`）留待后续补入。

---

## 目录

1. [安装与运行](#1-安装与运行)
2. [皮肤语法](#2-皮肤语法)
3. [Measure 参考](#3-measure-参考)
4. [Meter 参考](#4-meter-参考)
5. [主题系统](#5-主题系统)
6. [Dock 栏配置](#6-dock-栏配置)
7. [Bang 命令参考](#7-bang-命令参考)
8. [故障排查](#8-故障排查)
9. [许可证](#9-许可证)

---

## 1. 安装与运行

### 1.1 从发布包运行

下载 `RainDeskPlus-0.1.0-win64.zip` 并解压到任意可写目录，得到如下结构：

```
RainDeskPlus-0.1.0-win64/
├── RainDeskPlus.exe     主程序
├── LICENSE              GPL v2 许可证全文
├── README.md            项目说明
└── Skins/               皮肤根目录
    └── example/         随包示例皮肤
        ├── skin.ini     挂件皮肤定义
        ├── Dock.ini     Dock 栏定义
        ├── items.ini    Dock 图标项
        ├── Themes/      主题文件
        └── Icons/       Dock 图标资源
```

双击 `RainDeskPlus.exe` 即可启动。程序启动后依次完成：

1. 在 **exe 所在目录**逐级向上（含自身，最多 5 级）查找 `Skins\` 目录，作为皮肤根；
2. 执行内置自检，把逐项结果写入 **exe 同目录**的 `smoke_result.txt`；
3. 读取 `Skins\example\Dock.ini` 拉起 Dock 栏窗口，并进入消息循环常驻。

> **v0.1.0 的形态说明**：当前主程序同时承担「自检 + Dock 宿主」两个职责。
> 挂件皮肤（`skin.ini`）的完整加载链路已实现并由自检区段 10.1 端到端验证，但尚未提供
> 「交互式选择皮肤 / 挂件」的入口。关闭 Dock 窗口即可退出程序。

### 1.2 运行时限（`RAINDOCK_SMOKE_MS`）

Dock 常驻循环默认**一直挂起**直到 Dock 窗口被关闭。若需要限时退出（便于脚本化回归），
先设置环境变量 `RAINDOCK_SMOKE_MS`（单位毫秒）：

```powershell
$env:RAINDOCK_SMOKE_MS = "3000"   # 3 秒后自动退出
.\RainDeskPlus.exe
```

未设置或设置为非正数时，语义为「常驻」。

### 1.3 退出码与自检结果

主程序为 **GUI 子系统**（不产生控制台输出），诊断信息以文件形式落盘：

| 文件 | 位置 | 内容 |
|------|------|------|
| `smoke_result.txt` | exe 同目录 | 逐项 `PASS` / `FAIL` 明细，末行 `SMOKE SUMMARY PASS=<n> FAIL=<n>` |

进程退出码：`FAIL == 0` 时为 `0`，否则为 `1`。可用于 CI / 批处理断言。

### 1.4 从源码构建

前置条件与踩坑见 [BUILD.md](BUILD.md)。最简流程（需 VS 2026 / CMake / Windows SDK）：

```powershell
# 默认线（不加 Duilib）：构建到 build/vs2026/RelWithDebInfo
.\scripts\build.ps1 -Preset vs2026-release

# 带 Duilib 的完整线（Dock 透明窗口 / 图标渲染）：build/vs2026-dock-ui/RelWithDebInfo
cmake --preset vs2026-x64-dock-ui
cmake --build --preset vs2026-dock-ui-release
```

产物 `RainDeskPlus.exe` 位于对应的 `RelWithDebInfo\` 子目录下。

---

## 2. 皮肤语法

皮肤是标准 INI 文本（UTF-8 / ANSI 均可，程序按 ANSI 936 代码页读取），按段（`[Section]`）组织。
一个最小可用皮肤如下：

```ini
[Rainmeter]
Update=1000

[Variables]
FontColor=255,255,255,255

[MeasureClock]
Measure=Time
Format=%H:%M:%S

[MeterClock]
Meter=String
MeasureName=MeasureClock
Text=%1
FontFace=Segoe UI
FontSize=30
FontColor=#FontColor#
X=40
Y=40
```

### 2.1 段类型一览

| 段前缀 | 作用 | 说明 |
|--------|------|------|
| `[Rainmeter]` | 皮肤级设置 | 仅 `Update` 生效（见下） |
| `[Variables]` | 自定义变量 | 通过 `#名称#` 在任意位置展开 |
| `[Theme]` | 主题声明 | `Name=<主题名>`，见 [§5](#5-主题系统) |
| `[Measure*]` | 数据源 | 段内须有 `Measure=<类型>` |
| `[Meter*]` | 显示元素 | 段内须有 `Meter=<类型>`，可用 `MeasureName=` 绑定数据源 |

`<类型>` **大小写敏感**，必须与下表完全一致。

### 2.2 `[Rainmeter]` 段

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Update` | 整数（毫秒） | `1000` | 皮肤刷新周期；写 `0` 或负值时回落为 `1000` |

> 示例皮肤中的 `AccurateText` / `AntiAlias` / `Background` 键当前版本**未读取**，保留仅为与
> 上游 Rainmeter 皮肤保持兼容，可安全删除。

### 2.3 `[Variables]` 段

```ini
[Variables]
FontFace=Segoe UI
PanelW=220
```

引用方式：`FontFace=#FontFace#`。变量在解析阶段展开，可用于任何字符串/数值位置。

**预置变量**：主题系统提供一批常用变量（配色、字号），例如 `#Color#`、`#TrackColor#`、
`#FontSizeBody#`、`#AccentCPU#` 等，完整清单见 [§5.2](#52-可用变量清单)。

### 2.4 段公共键（Measure / Meter 通用）

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `UpdateDivider` | 整数 | `1` | 每 N 次皮肤刷新才更新一次本段；负值或 `0` 表示每次更新 |
| `Group` | 字符串 | 空 | 段分组名 |
| `DynamicVariables` | 布尔 | `0` | 每次更新重新展开变量 |
| `OnUpdateAction` | Bang 串 | 空 | 本段更新后执行的 Bang（重入受保护） |

布尔键接受 `0` / `1`（亦接受 `true` / `false` 等写法，程序内部按数值解析）。

---

## 3. Measure 参考

### 3.1 Measure 公共键

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Measure` | 字符串 | — | **必填**，测量类型 |
| `MinValue` | 浮点 | 未设置 | 归一化下限；未设置表示「无界」 |
| `MaxValue` | 浮点 | 未设置 | 归一化上限；未设置表示「无界」 |
| `InvertMeasure` | 布尔 | `0` | 反转数值方向 |
| `Disabled` | 布尔 | `0` | 禁用本 Measure（不采样） |
| `Substitute` | 串对 | 空 | 字面量替换，格式 `"原文":"替换","原文2":"替换2"` |
| `UpdateDivider` / `Group` / `DynamicVariables` / `OnUpdateAction` | — | — | 见 [§2.4](#24-段公共键measure--meter-通用) |

### 3.2 条件动作键（所有 Measure 可用）

**数值条件**：

| 键 | 说明 |
|----|------|
| `IfAboveValue` / `IfAboveAction` | 当前值 > 阈值时执行 |
| `IfBelowValue` / `IfBelowAction` | 当前值 < 阈值时执行 |
| `IfEqualValue` / `IfEqualAction` | 当前值 == 阈值（取整比较）时执行 |

**表达式条件**（`IfCondition` 使用数学表达式语法）：

| 键 | 说明 |
|----|------|
| `IfCondition` / `IfTrueAction` / `IfFalseAction` | 条件成立 / 不成立分别执行 |
| `IfConditionMode` | 布尔；`1` 时条件与上次结果相同也重复执行 |

**正则匹配**（PCRE）：

| 键 | 说明 |
|----|------|
| `IfMatch` / `IfMatchAction` / `IfNotMatchAction` | 对字符串值做正则匹配 |
| `IfMatchMode` | 布尔；`1` 时每次都执行匹配动作 |

**多组写法**：任意条件键都支持追加编号 `2`、`3`…… 例如
`IfCondition2` / `IfTrueAction2` / `IfFalseAction2`，`IfMatch2` / `IfMatchAction2` 等。
编号必须连续，遇到空条件即停止解析。

```ini
[MeasureMemory]
Measure=Memory
IfAboveValue=80
IfAboveAction=!SetVariable AccentColor 255,80,80,255
```

### 3.3 `Measure=Time` — 时钟

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Format` | 字符串 | `%H:%M:%S` | `strftime` 风格格式串 |
| `FormatLocale` | 字符串 | 空 | 格式化使用的区域设置 |
| `TimeStamp` | 字符串 | `-1` | 固定时间点（Unix 秒）；`-1` 表示当前时间 |
| `TimeZone` | 整数 | `0` | 时区偏移（分钟），叠加在系统时区之上 |
| `DaylightSavingTime` | 布尔 | `1` | 是否应用夏令时 |

```ini
[MeasureClock]
Measure=Time
Format=%H:%M:%S
```

### 3.4 `Measure=Calendar` — 日历网格

产出「表头 + 固定 6 行 × 7 列」的整月网格字符串（每格 3 字符宽 + 列间 1 空格 = 27 字符等宽）。
行数**固定 6 行**，避免挂件高度随月份 4~6 周跳动。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Year` | 整数 | `0` | 固定年份；`0` 表示当前年 |
| `Month` | 整数 | `0` | 固定月份；`0` 表示当前月 |
| `TimeStamp` | 字符串 | `-1` | 固定 Unix 秒 |
| `TimeZone` | 整数 | `0` | 时区偏移（分钟），口径与 `Measure=Time` 一致 |
| `DaylightSavingTime` | 布尔 | `1` | 是否应用夏令时 |
| `WeekStart` | 整数 | `0` | `0` = 周日为周首日，`1` = 周一为周首日 |
| `HighlightToday` | 布尔 | `1` | 是否标记今日 |
| `TodayMark` | 字符串 | `*` | 今日标记符号 |

> 使用等宽字体（如 `Consolas`）才能保证逐列对齐。
> 固定了 `Year` / `Month` 且与参考月不一致时**不标记今日**，避免把今天标到别的月份。

### 3.5 `Measure=CPU` — 处理器占用

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Processor` | 整数 | `0` | `0` = 整机平均；单核采样为 M1 降级口径 |

数值范围 `0` ~ `100`（百分比），可直接喂给 `Meter=Bar`。

### 3.6 `Measure=Memory` — 内存

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Mode` | 字符串 | `UsedPercent` | `UsedPercent` / `UsedBytes` / `FreeBytes` / `TotalBytes` |
| `Total` | 布尔 | `0` | `1` 时统计「物理内存 + 页面文件」，否则只统计物理内存 |

**`Mode` 取值大小写敏感**，写错时保持默认。

### 3.7 `Measure=Disk` — 磁盘容量

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Drive` | 字符串 | 系统盘 | 盘符，如 `C:`、`D:` |
| `Total` | 布尔 | `0` | `0` = 剩余容量，`1` = 总容量 |

归一化分母恒为「该盘总容量」，可与 `Meter=Bar` 直接配合。

### 3.8 `Measure=Net` — 网络流量

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Type` | 字符串 | `InOctets` | `InOctets`（下行）/ `OutOctets`（上行），大小写敏感 |
| `Interface` | 字符串 | `Best` | `Best` = 自动选择活动网卡；`Total` = 全部网卡合计；或直接写网卡名 |
| `Cumulative` | 布尔 | `0` | `1` 时产出累计字节数，`0` 时产出每秒速率 |

### 3.9 `Measure=Weather` — 天气

读取 OpenWeatherMap 报文 JSON 缓存，产出「城市 温度 描述」。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `ApiKey` | 字符串 | 空 | OWM API Key；留空时回退环境变量 `OPENWEATHER_API_KEY` |
| `City` | 字符串 | 内置默认城市 | 目标城市 |
| `Units` | 字符串 | `metric` | `metric`（摄氏）/ `imperial`（华氏），大小写不敏感 |
| `CacheFile` | 字符串 | 内置默认 | 报文缓存文件路径；相对路径按皮肤目录解析 |
| `ExpireMinutes` | 整数 | `0` | 报文过期阈值（分钟）；`0` = 禁用过期判定 |

报文过期时输出末尾追加 `(cache stale)`。缓存缺失 / JSON 损坏 / OWM 错误体三类情况
统一回退为上一次成功值。

### 3.10 `Measure=Media` — 媒体播放信息

从 JSON 快照读取播放状态。因一个 Measure 只产出一个字符串，展示「标题 + 艺术家 + 状态 + 进度」
需用**多个段实例**（每段设不同 `Field`）。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `StateFile` | 字符串 | 内置默认 | 快照文件路径；相对路径按皮肤目录解析 |
| `Field` | 字符串 | `Title` | `Title` / `Artist` / `Album` / `State` / `Progress`（大小写不敏感） |

- `State` 输出首字母大写归一化的状态（如 `Playing` / `Paused` / `Stopped`）；
- `Progress` 输出可读进度 `1:18 / 4:05`，同时给出数值 `已播放秒数`、上限 `总时长`；
- 未知 `Field` 回落为 `Title`；
- 快照缺失 / JSON 损坏 / 错误体三类回退为占位符 `-`，失败后保住上一次成功值。

播放控制通过 `!CommandMeasure` 发送（见 [§7](#7-bang-命令参考)），
支持命令：`Play` / `Pause` / `PlayPause` / `Toggle` / `Next` / `NextTrack` / `Prev` / `Previous` /
`PrevTrack` / `Stop`（大小写不敏感，未知命令静默忽略）。

### 3.11 `Measure=Audio` — 音频频谱

从 PCM 快照读取采样并做 FFT，产出 8 段频谱强度。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `AudioFile` | 字符串 | 内置默认 | 快照文件路径（`{ "sampleRate": 44100, "samples": [...] }`） |
| `Field` | 字符串 | `Band1` | `Band1`~`Band8` / `Level` / `Peak`（大小写不敏感） |

- `Band1`~`Band8`：按几何级数平分 60Hz~12kHz，每段约一个倍频程；
- `Level`：时域 RMS×√2（满量程正弦归一为 1）；
- `Peak`：抽样绝对值峰值；
- 数值统一为 `0`~`1`，字符串值为百分比文本；
- 未显式设置 `MaxValue` 时自动补 `1`，保证 `Meter=Bar` 可归一化；
- 快照缺失 / JSON 损坏 / `samples` 为空三类回退为 `-`（静音帧为 `0%`，**不是**失败）。

---

## 4. Meter 参考

当前版本可用 Meter 类型：`String`、`Bar`、`Line`。

### 4.1 Meter 公共键

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Meter` | 字符串 | — | **必填**，Meter 类型 |
| `MeasureName` | 字符串 | 空 | 绑定的 Measure 段名（大小写不敏感） |
| `X` / `Y` | 整数 | `0` | 相对于皮肤左上角的位置（像素） |
| `Hidden` | 布尔 | `0` | 是否隐藏（仍参与布局计算） |

### 4.2 `Meter=String` — 文本

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `Text` | 字符串 | 空 | 显示文本；`%1` 会被替换为绑定 Measure 的字符串值 |
| `FontFace` | 字符串 | `Segoe UI` | 字体名 |
| `FontSize` | 浮点 | `12` | 字号（磅） |
| `FontColor` | 颜色 | `255,255,255,255` | 文本颜色 |
| `StringAlign` | 字符串 | `Left` | `Left` / `Center` / `Right`（大小写不敏感） |

- `Text` 为空时直接显示绑定 Measure 的字符串值；
- 未设置 `Text` 且无 `MeasureName` 时显示空串；
- 尺寸由文本度量自动决定（AutoSize）。

```ini
[MeterCPUText]
Meter=String
MeasureName=MeasureCPU
Text=CPU  %1%
FontFace=Segoe UI
FontSize=14
FontColor=#FontColor#
X=20
Y=40
```

### 4.3 `Meter=Bar` — 条状图

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `W` / `H` | 整数 | `0` | 宽 / 高（像素）；**必须显式给出**，为 `0` 时不绘制 |
| `BarOrientation` | 字符串 | `Horizontal` | `Horizontal` / `Vertical` |
| `BarColor` | 颜色 | `51,153,255,255` | 进度条颜色 |
| `SolidColor` | 颜色 | `255,255,255,102` | 底槽颜色 |

数值按 Measure 的 `MinValue` / `MaxValue` 归一化；未设置量程时恒取 `0`。

### 4.4 `Meter=Line` — 折线图

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `W` / `H` | 整数 | `0` | 宽 / 高（像素）；**必须显式给出** |
| `LineColor` | 颜色 | `51,153,255,255` | 折线颜色 |
| `LineWidth` | 浮点 | `1.0` | 线宽（像素） |
| `AutoScale` | 布尔 | `1` | `1` = 按窗口内极值自动量程 |
| `LineCount` | 整数 | `60` | 环形采样队列长度；小于 `2` 时按 `2` 处理 |

### 4.5 颜色写法

- `R,G,B,A`（十进制，如 `255,255,255,255`）—— Meter 的 `FontColor` / `BarColor` / `LineColor` 等；
- `#RRGGBB` / `#AARRGGBB`（十六进制）—— Dock 面板色 `PanelColor` 使用该口径。

### 4.6 鼠标动作

所有 Meter 都支持下列键，值为一个或多个 Bang（`!` 开头）命令：

| 键 | 触发时机 |
|----|----------|
| `LeftMouseUpAction` / `LeftMouseDownAction` / `LeftMouseDoubleClickAction` | 左键抬起 / 按下 / 双击 |
| `MiddleMouseUpAction` / `MiddleMouseDownAction` / `MiddleMouseDoubleClickAction` | 中键抬起 / 按下 / 双击 |
| `RightMouseUpAction` / `RightMouseDownAction` / `RightMouseDoubleClickAction` | 右键抬起 / 按下 / 双击 |
| `X1MouseUpAction` / `X1MouseDownAction` / `X1MouseDoubleClickAction` | 侧键 1 |
| `X2MouseUpAction` / `X2MouseDownAction` / `X2MouseDoubleClickAction` | 侧键 2 |
| `MouseScrollUpAction` / `MouseScrollDownAction` / `MouseScrollLeftAction` / `MouseScrollRightAction` | 滚轮上 / 下 / 左 / 右 |
| `MouseOverAction` / `MouseLeaveAction` | 指针进入 / 离开 |
| `MouseActionCursor` | 布尔，是否切换指针形状 |
| `MouseActionCursorName` | 指针形状：`HAND` / `TEXT` / `CROSS` / `NO` / `SIZE_ALL` / `UPARROW` 等，或 `.cur` 文件名 |

命中判定为**倒序**（后绘制的 Meter 在上层，优先命中）。

### 4.7 完整示例

参见随包皮肤 [`Skins/example/skin.ini`](../Skins/example/skin.ini)：九合一挂件，
含 21 个 Measure（CPU / 内存 / 磁盘 / 网络 / 天气 / 时钟 ×3 / 日历 / 媒体 ×4 / 音频 ×8）
与 28 个 Meter（String / Bar / Line）。

---

## 5. 主题系统

### 5.1 声明与合并语义

在皮肤（或 `Dock.ini`）中声明主题名：

```ini
[Theme]
Name=Dark
```

程序会在**主文件同目录**查找 `Themes\<主题名>.ini` 并合并：

> **主题文件是「低优先级默认值」** —— 主文件显式写出的同名键优先，主题文件只补缺口。

主题文件缺失时降级告警、**不阻断**皮肤加载。因此皮肤可以在主文件里覆盖任意主题键：

```ini
[Theme]
Name=Dark

[Variables]
FontFace=Consolas    ; 覆盖主题里的 Segoe UI，仅此皮肤生效
```

### 5.2 可用变量清单

随包主题 `Skins/example/Themes/Dark.ini`（`Light.ini` / `HighContrast.ini` 同构）提供：

| 类别 | 变量 |
|------|------|
| 通用取色 | `Color`、`TrackColor` |
| 字号 | `FontSize`（14）、`FontSizeSmall`（12，控制按钮）、`FontSizeBody`（13，日历与媒体副标题）、`FontSizeTitle`（15）、`ClockSize`（30，时钟大字） |
| 挂件强调色 | `AccentCPU`、`AccentMemory`、`AccentDisk`、`AccentNet`、`AccentMedia` |
| 音频频谱 | `AudioBand1` ~ `AudioBand8`（低频 → 高频渐变） |

主题文件的 `[Colors]` 段额外供 Dock 使用：

| 键 | 口径 | 说明 |
|----|------|------|
| `PanelColor` | `#AARRGGBB` | Dock 面板底色，默认 `#40000000` |
| `Transparency` | `0`~`255` | 面板透明度（越大越不透明） |

### 5.3 切换主题

改为目标主题名后，触发一次刷新即可生效：

```ini
LeftMouseUpAction=!SetOption Rainmeter ThemeAction "!Refresh"
```

或直接由外部发送 `!Refresh`。`!Refresh` 会从磁盘重读 INI，主题合并随之重算。

> `Dock.ini` 是**生成文件**：程序退出时会回写 `[Theme] Name=` 与 `[Dock]` 6 个字段，
> 不写 `[Colors]`（透明度归主题管辖，写死会遮住主题）。
> 手写 `[Colors]` 仍可本地覆盖，但会在下次回写时保持原样不被打扰。

---

## 6. Dock 栏配置

### 6.1 `Dock.ini`

```ini
; RainDeskPlus Dock configuration (generated)
[Theme]
Name=Dark

[Dock]
IconSize=48           ; 图标基准边长（像素），合法区间 16..128，越界自动钳制并告警
Position=Bottom       ; 停靠位置：Top / Bottom / Left / Right（大小写不敏感，非法值回落 Bottom）
AutoHide=0            ; 是否自动隐藏
Spacing=8             ; 图标间距（像素），负值钳制为 0
MaxScale=1.80         ; 悬停放大的最大倍数；小于 1.0 时忽略（保持内置默认）
MaxRadius=150.00      ; 放大影响半径（像素）；小于等于 0 时忽略
```

`[Colors]` 段（可选，本地覆盖）支持 `PanelColor=#AARRGGBB` 与 `Transparency=<0-255>`；
未提供时依次回退到主题文件的 `[Colors]`、再回退到内置默认值。

### 6.2 `items.ini`

```ini
; RainDeskPlus Dock items (generated)
[Item1]
Id=notepad             ; 唯一标识
Name=记事本            ; 显示名（鼠标悬停提示）
Icon=Icons\notepad.png ; 图标路径，相对 Dock.ini 所在目录
Target=C:\Windows\System32\notepad.exe
WorkingDir=            ; 工作目录，留空使用程序默认
Arguments=             ; 启动参数
```

`Id` 缺失时回落为段名，`Name` 缺失时回落为 `Id`。

### 6.3 添加图标（拖拽）

把文件**拖到 Dock 栏上**即可添加，处理规则如下：

1. 仅接受**普通文件**；同一目标路径已存在时直接跳过（重复拖入不会产生第二项，也不覆盖已改过的 `Name`）；
2. `Name` 取文件名（不含扩展名，保留原始大小写），`WorkingDir` 取该文件所在目录；
3. `Id` 由文件名规范化得到；若与其他项冲突，依次追加 `_2`、`_3`… 直到唯一；
4. 图标提取并写为 `Icons\<Id>.png`（相对 `Dock.ini` 所在目录）；**提取失败不丢弃该项**，只是没有图标；
5. 新条目追加到 `items.ini`，并在程序退出时随 `Dock.ini` 一并回写。

---

## 7. Bang 命令参考

Bang 是以 `!` 开头的命令，可写在 `OnUpdateAction`、各类 `*Mouse*Action`、`IfCondition` 动作中。

**通用规则**：

- Bang 名**大小写不敏感**（`!redraw` 等价 `!Redraw`）；
- 不以 `!` 开头的动作当前版本不执行（ShellExecute 分支尚未实现）；
- 未知 Bang / 目标不存在时**静默忽略**，不报错。

| Bang | 语法 | 说明 |
|------|------|------|
| `!Update` | `!Update` | 立即更新当前皮肤（所有 Measure / Meter） |
| `!Redraw` | `!Redraw` | 立即强制重绘一帧（绕过脏检查） |
| `!SetOption` | `!SetOption Section Key Value` | 修改**内存中**的段选项；`Section` 可写 `[Section]` 或裸名；`Value` 可为空或含空格 |
| `!SetVariable` | `!SetVariable Name Value` | 修改内存变量；`Value` 可含空格 |
| `!WriteKeyValue` | `!WriteKeyValue Section Key Value [File]` | **只落盘**，不改变运行中的内存值；`File` 缺省为当前皮肤 ini，相对路径按皮肤目录解析；值与路径支持引号包裹含空格 |
| `!Hide` | `!Hide` | 隐藏皮肤窗口 |
| `!Show` | `!Show` | 显示皮肤窗口 |
| `!Toggle` | `!Toggle` | 切换显示 / 隐藏 |
| `!Refresh` | `!Refresh` | 重新从磁盘加载皮肤（等价 `Skin::Reload()`） |
| `!ActivateConfig` | `!ActivateConfig ConfigName [Variant]` | 激活指定配置（皮肤目录名） |
| `!DeactivateConfig` | `!DeactivateConfig ConfigName` | 停用指定配置 |
| `!CommandMeasure` | `!CommandMeasure "MeasureName" "Command"` | 向 Measure 下发命令；引号内可含空格，也支持裸写；Measure 名大小写不敏感 |
| `!Quit` | `!Quit` | 退出程序 |

### 示例

```ini
; 点击按钮强制刷新皮肤（主题切换后生效）
[MeterRefresh]
Meter=String
Text=⟳
LeftMouseUpAction=!Refresh

; 内存超过 80% 时把强调色改为红色
[MeasureMemory]
Measure=Memory
IfAboveValue=80
IfAboveAction=!SetVariable AccentColor 255,80,80,255

; 媒体控制按钮
[MeterMediaPlay]
Meter=String
Text=▶
LeftMouseUpAction=!CommandMeasure "MeasureMediaState" "PlayPause"

; 把配置写回磁盘（需再 !Refresh 才会生效）
[MeterSave]
Meter=String
Text=Save
LeftMouseUpAction=!WriteKeyValue Variables City Beijing
```

> `!SetOption` / `!SetVariable` 只改内存，**重启或 `!Refresh` 后丢失**；
> 需要持久化请用 `!WriteKeyValue`，再配合 `!Refresh` 让改动生效。

---

## 8. 故障排查

| 现象 | 排查方向 |
|------|----------|
| 启动后立刻退出 | 关闭 Dock 窗口即为正常退出路径；检查是否误设了 `RAINDOCK_SMOKE_MS` |
| Dock 栏不显示 | 确认 exe 上溯 5 级内存在 `Skins\` 目录，且 `Skins\example\Dock.ini` 可读；查看 `smoke_result.txt` 的 `dock-start` 项 |
| 自检出现 `FAIL` | 打开 `smoke_result.txt`，每一行含检查点名称与实测读数，可据此定位 |
| 皮肤不加载 | 确认段内有 `Measure=` / `Meter=` 键，且 `<类型>` 与本文档大小写一致 |
| 颜色不生效 | 区分两种口径：Meter 用 `R,G,B,A`，Dock 的 `PanelColor` 用 `#AARRGGBB` |
| 主题切换无效 | 确认 `[Theme] Name=` 拼写、`Themes\<名>.ini` 文件存在；切完需触发 `!Refresh` |
| 修改 INI 后无变化 | `!SetOption` / `!SetVariable` 只改内存；落盘须 `!WriteKeyValue`，生效须 `!Refresh` |
| 日历列不对齐 | 使用等宽字体（如 `Consolas`） |
| 字体 / 图标显示异常 | 参见 [BUILD.md](BUILD.md) 的运行时依赖说明 |

日志与构建问题定位见 [BUILD.md](BUILD.md)；Dock 内部设计见 [DOCK_DESIGN.md](DOCK_DESIGN.md)。

---

## 9. 许可证

Copyright (C) 2014-2025 Rainmeter Project
Copyright (C) 2026 RainDeskPlus Project

本程序为自由软件，依据 **GNU 通用公共许可证第 2 版（GPL v2）** 条款发布，详见 [LICENSE](../LICENSE)。
Rainmeter 源码派生部分保留其原作者版权声明；Dock 模块为全新实现，同样以 GPL v2 发布。
