# 构建指南

> **当前状态（2026-08-26）：工具链已就绪，骨架构建通过（0 错误 0 警告）。**

## 1. 前置条件（本机现状，已验证）

| 工具 | 本机状态 |
|------|---------|
| Visual Studio 2026 Community | ✅ 已装于 `E:\visual_C`（vswhere productLine=18） |
| MSVC v145 工具集 | ✅ `E:\visual_C\VC\Tools\MSVC\14.51.36231`（编译器 19.51） |
| Windows SDK | ✅ 10.0.26100.0 |
| CMake | ✅ 4.3.1（VS 自带）。**注意：不在终端 PATH**，用全路径调用：`E:\visual_C\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Git | ✅ VS 内置：`E:\visual_C\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`（同样不在 PATH；或后续单独安装 git for Windows） |

> 建议：把上述 cmake/git 目录加入用户 PATH，简化日常命令。

### 构建命令（本机实际可用）

```powershell
$cmake = "E:\visual_C\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset vs2026-x64
& $cmake --build build/vs2026 --config Debug
```

产物：`build/vs2026/Debug/RainDeskPlus.exe` 及 RainDeskPlusCore / Dock / UI 静态库。

## 2. 首次构建记录与已修复问题

| 问题 | 修复 |
|------|------|
| CMakePresets.json 报非法字段 `comment` | 删除该字段（presets 不支持任意额外键） |
| `MultiByteToWideChar` 等未声明 | ConfigParser.cpp / MeasureTime.cpp 补 `<windows.h>` |
| MeterString.cpp C2027 使用未定义类型 Measure | 补 `#include "Measure.h"` |
| windows.h 宏 `LoadString→LoadStringW` 与方法冲突 | 方法改名 `LoadFromString` |
| LNK1104 找不到 `dwrite_3.lib` | SDK 无此库，改链 `dwrite.lib` |
| LNK2019 未解析 main（入口是 wWinMain） | 主程序目标加 `WIN32_EXECUTABLE TRUE` |
| C4005 宏重定义警告 ×11 文件 | 统一加 `#ifndef WIN32_LEAN_AND_MEAN` 守卫 |

## 3. 用 VS 直接打开（staging stub）

仓库根的 `RainDeskPlus.sln` 是一个最小手写 stub（已设为 VS 2026 / v145），可直接双击打开。**注意**：

- 它只编译核心静态库（Library + Dock + UI 骨架），**不含** Duilib/Direct2D 链接配置。
- 真正完整、被维护的构建图在 CMake。建议用 CMake 生成 .sln 后再在 VS 中打开（即第 2 节方式 B 生成的 `build/vs2026/RainDeskPlus.sln`）。

## 4. 引入 Duilib

Duilib 源码已 vendor 到 `third_party/duilib`，选用 [qdtroy/DuiLib_Ultimate](https://github.com/qdtroy/DuiLib_Ultimate)（命名空间 `DuiLib`）。

```powershell
# 启用 Duilib 预设构建（RAINDOCK_USE_DUILIB=ON）
cmake --preset vs2026-x64-dock-ui
cmake --build --preset vs2026-dock-ui-debug
# 跑 Hello World 验证
.\build\vs2026-dock-ui\Debug\RainDeskPlusDuilibTest.exe
```

### 4.1 为什么自建 target 而不用上游 `DuiLib/CMakeLists.txt`

上游那份构建的是 **SHARED + `UILIB_EXPORTS`**，且用 `aux_source_directory` 把含 `DllMain` 的 `UILib.cpp` 一并收进库；另有 `CMAKE_GENERATOR_TOOLSET v100`、`CMAKE_SYSTEM_VERSION 5.01`、`add_subdirectory(Demos)` 等本机不成立的设定。因此在根 `CMakeLists.txt` 顶部（`if(RAINDOCK_USE_DUILIB)` 块）自建 **STATIC** `Duilib` target：

- 源清单 = `Control/*.cpp` + `Core/*.cpp` + `Layout/*.cpp` + `Utils/*.cpp` + `StdAfx.cpp`（根目录 `UILib.cpp` 不在 GLOB 内，天然排除）。
- `UIActiveX.cpp` / `UIFlash.cpp` / `UIWebBrowser.cpp` **不能裁**：`Core/ControlFactory.cpp` 有对应 `INNER_REGISTER_DUICONTROL` 注册宏，裁掉即缺符号。
- `Control/UIGifAnimEx.cpp` 整体在 `#ifdef USE_XIMAGE_EFFECT` 内，不定义该宏时为空编译单元，可安全保留。
- include 目录为 `${SOURCE_DIR}/third_party/duilib/DuiLib`（公开），头文件按 `"Utils/xxx.h"` 形式包含。

### 4.2 必须处理的 5 个坑（均已实证）

| # | 现象 | 原因 | 处理 |
|---|------|------|------|
| 1 | `error C2672: "std::max": 未找到匹配的重载函数`（`UIList/UIRender/UIManager/UICombo/UIVerticalLayout`…） | 本工程目录级 `add_compile_definitions` 定义了 `NOMINMAX`，且它在**生成阶段**对目录内所有 target 生效（与 target 创建顺序无关），于是 `windows.h` 不再定义 `min/max` 宏，`StdAfx.h` 的 `#define MAX max` 解析到 `std::max`，`LONG` 与 `int` 混合实参推导失败 | 在 `Duilib` target 上加 `target_compile_options(Duilib PRIVATE /UNOMINMAX)` |
| 2 | `error C2065: "WSADATA"/"in_addr" 未声明的标识符`（`Control/UIIPAddress.cpp`） | 同上的泄漏 `WIN32_LEAN_AND_MEAN` 使 `windows.h` 不再引入 `winsock.h` | 同上，追加 `/UWIN32_LEAN_AND_MEAN` |
| 3 | 源码乱码 / 解析错误 | 上游多为 **GBK 无 BOM** 源文件 | **不要**给该 target 加 `/utf-8`；本机系统 ACP=936，cl.exe 默认即按 GBK 解码 |
| 4 | `error C5033: "register" 不再是存储类说明符` | `Core/UIMarkup.cpp`、`Utils/unzip.cpp` 使用 `register`，C++17 起移除 | `set_target_properties(Duilib PROPERTIES CXX_STANDARD 14)` |
| 5 | 消费者侧 `__declspec(dllimport)` 链接失败 | `UIlib.h` 据 `UILIB_STATIC` 决定 `UILIB_API` 展开 | `target_compile_definitions(Duilib PUBLIC UILIB_STATIC ...)` |

另：`Utils/TrayIcon.cpp` 用 `Shell_NotifyIcon` 但自身无 `#pragma comment(lib,"shell32.lib")`，`Control/UIIPAddressEx.cpp` 需要 `ws2_32`，故在 `target_link_libraries` 显式补 `shell32 ws2_32`。DPI 相关 API（`GetDpiForMonitor` / `SetProcessDpiAwareness`）由 `Utils/DPI.cpp` 动态 `GetProcAddress`，**无 shcore 链接期依赖**。

### 4.3 与 Dock 模块的关系

D5 只验证 Duilib 环境本身，`RainDeskPlusDock` 仍**不**依赖 Duilib。把 `DockWindow` 真正挂到 `WindowImplBase` 属于 D13-14：需同时（1）让 `RainDeskPlusDock` 链接 `Duilib` 并定义 `RAINDOCK_USE_DUILIB=1`；（2）把 `UI/DuilibWindowBase.h` 真实分支改为 `#include "UIlib.h"` + `DuiLib::` 限定；（3）让 `DockWindow` 实现 `GetSkinFile()` / `GetWindowClassName()` 并改 `Notify` / `OnMouseMove` 签名。

## 5. 网络与 TLS 故障排除

本机 hosts 把 github.com / raw.githubusercontent.com / codeload.github.com 等映射到 `127.0.0.1`，经本地 GitHub 加速器 MITM。

```powershell
# 测试连通（装好 git 后）
git ls-remote https://github.com/rainmeter/rainmeter.git HEAD
```

若因自签 CA 失败，按优先级处理：

1. **首选**：把加速器根 CA 导入「受信任的根证书颁发机构」（加速器通常提供 `证书安装` 入口）。
2. 临时：注释 hosts 中相关行，直连。
3. **仅排错**：`git -c http.sslVerify=false clone ...`（不要长期关闭校验，存在中间人风险）。

## 6. 常见问题

- `error C2039: 'xxx': is not a member of ...` → Rainmeter 模块依赖 `Common/` 公共头未提取，按 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §5 顺序补齐。
- `LINK 2019: unresolved external` → 检查 `CMakeLists.txt` 的 `target_link_libraries` 是否含 `advapi32/pdh/iphlpapi/d2d1/...`。
- `windows.h` 找不到 → Windows SDK 未装或 `WindowsTargetPlatformVersion` 未设。
- CMake 报 `Generator "Visual Studio 18 2026" not found` → CMake 版本 < 4.2，升级 CMake（VS 2026 Installer 自带的 CMake 已满足）。
- CMake 报 v143 toolset 找不到（回退 2022 预设时）→ 本机只装了 VS 2026；用 `vs2026-x64` 预设，或在 VS Installer 单独勾选「MSVC v143 - VS 2022 C++ x64/x86 生成工具」。
