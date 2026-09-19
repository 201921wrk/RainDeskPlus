# RainDeskPlus - Desktop beautification platform
# Based on Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
# Copyright (C) 2014-2025 Rainmeter Project
# Copyright (C) 2026 RainDeskPlus Project
cmake_minimum_required(VERSION 3.20)

project(RainDeskPlus
    VERSION 0.1.0
    DESCRIPTION "Desktop beautification platform: widgets + dock + skins"
    LANGUAGES C CXX)

# ---------- 全局选项 ----------
option(RAINDOCK_USE_DUILIB    "Enable Duilib UI integration (requires vendored Duilib)" OFF)
option(RAINDOCK_BUILD_DOCK    "Build Dock bar module"                                   ON)
option(RAINDOCK_BUILD_TESTS   "Build unit tests (requires GoogleTest)"                  OFF)
option(RAINDOCK_BUILD_PLUGINS "Build plugin DLLs"                                       OFF)

# ---------- 第三方：Duilib（可选，静态库） ----------
# 源码已 vendor 至 third_party/duilib（qdtroy/DuiLib_Ultimate）。
#
# 该 target 必须在下方 add_compile_options / add_compile_definitions **之前**创建：
# CMake 的目录级编译选项在 target 创建时被继承，放在这里可让 Duilib 不继承
# `/W4 /permissive- /utf-8 /DNOMINMAX` 等面向本工程现代代码的开关。原因是上游
# Duilib 为 VS2010 时代代码：
#   1) 源文件多为 GBK 无 BOM 编码，需要系统代码页（936）而非 UTF-8；
#   2) StdAfx.h 里 `#define MAX max` 依赖 windows.h 提供的 min/max 宏，
#      在 /DNOMINMAX 下会解析失败；
#   3) 使用非标准扩展，需要默认的宽松一致性模式。
#
# 上游自带的 DuiLib/CMakeLists.txt 不可用：它构建 SHARED + UILIB_EXPORTS，
# 且用 aux_source_directory 把含 DllMain 的 UILib.cpp 一并收进静态库。
if(RAINDOCK_USE_DUILIB)
    set(DUILIB_SRC_ROOT ${CMAKE_SOURCE_DIR}/third_party/duilib/DuiLib)
    if(NOT EXISTS ${DUILIB_SRC_ROOT}/UIlib.h)
        message(WARNING
            "RAINDOCK_USE_DUILIB=ON but ${DUILIB_SRC_ROOT}/UIlib.h not found. "
            "Vendor Duilib first (see docs/BUILD.md).")
    else()
        file(GLOB DUILIB_SOURCES CONFIGURE_DEPENDS
            ${DUILIB_SRC_ROOT}/Control/*.cpp
            ${DUILIB_SRC_ROOT}/Core/*.cpp
            ${DUILIB_SRC_ROOT}/Layout/*.cpp
            ${DUILIB_SRC_ROOT}/Utils/*.cpp
            ${DUILIB_SRC_ROOT}/StdAfx.cpp)   # UILib.cpp（DllMain）不入静态库，故不收集

        add_library(Duilib STATIC ${DUILIB_SOURCES})
        # 头文件按 "Utils/xxx.h" / "Core/xxx.h" 方式包含，故根目录即包含路径
        target_include_directories(Duilib PUBLIC ${DUILIB_SRC_ROOT})
        # UILIB_STATIC 必须同时作用于 Duilib 自身与消费者：UIlib.h 据此把
        # UILIB_API 展开为空（否则消费者侧会当作 dllimport）
        target_compile_definitions(Duilib PUBLIC UILIB_STATIC UNICODE _UNICODE)
        # 上游为 VS2010 时代代码：register 关键字在 C++17 起被移除，降级到 C++14
        set_target_properties(Duilib PROPERTIES
            CXX_STANDARD 14
            CXX_STANDARD_REQUIRED ON)
        # 目录级 add_compile_definitions 是在**生成阶段**对目录内所有 target 生效的，
        # 与 target 创建顺序无关（这点与 add_compile_options 不同），所以本工程为现代
        # 代码准备的 NOMINMAX / WIN32_LEAN_AND_MEAN 仍会落到 Duilib 上，必须在此撤销：
        #   NOMINMAX             → windows.h 不再定义 min/max 宏，StdAfx.h 的
        #                          `#define MAX max` 于是解析到 std::max，LONG 与 int
        #                          混合实参触发 C2672 模板推导失败
        #   WIN32_LEAN_AND_MEAN  → windows.h 不再引入 winsock.h，UIIPAddress.cpp 的
        #                          WSADATA / in_addr / WSAStartup 全部未声明
        # 上游 DuiLib.vcxproj 只定义 WIN32;_WINDOWS;UILIB_STATIC，故撤销后与其一致。
        target_compile_options(Duilib PRIVATE /UNOMINMAX /UWIN32_LEAN_AND_MEAN)
        # UIlib.h / StdAfx.cpp 以 #pragma comment 声明了 comctl32/GdiPlus/Imm32/winmm，
        # 静态库场景下再显式列出其余系统依赖，确保最终链接不缺符号：
        #   shell32 — Utils/TrayIcon.cpp 的 Shell_NotifyIcon
        #   ws2_32  — Control/UIIPAddressEx.cpp 的 WSA* 调用
        target_link_libraries(Duilib PUBLIC
            comctl32 gdiplus imm32 winmm
            ole32 oleaut32 uuid shlwapi dwmapi user32 gdi32
            shell32 ws2_32)

        # ---------- D5 验证：Duilib Hello World ----------
        # 与 Duilib 一样在 add_compile_options 之前创建，避免继承
        # /W4 /permissive- /DNOMINMAX（上游头文件依赖宽松模式与 min/max 宏）。
        # 仅额外打开 /utf-8，因为本文件自身是 UTF-8 编码。
        add_executable(RainDeskPlusDuilibTest Tests/test_duilib_hello.cpp)
        set_target_properties(RainDeskPlusDuilibTest PROPERTIES
            CXX_STANDARD 14
            CXX_STANDARD_REQUIRED ON)
        target_compile_options(RainDeskPlusDuilibTest PRIVATE /utf-8)
        target_link_libraries(RainDeskPlusDuilibTest PRIVATE Duilib)
    endif()
endif()

# ---------- D13-14 前置：开关宏全局一致（DD-12） ----------
# 放在 Duilib 段之后，使 Duilib 自身也不继承该宏以外的现代代码开关；
# 各 TU 以 #ifdef RAINDOCK_USE_DUILIB 选择「占位 / 真实窗口基类」实现
# （UI/DuilibWindowBase.h、Dock/DockWindow.h），两条构建线必须保持同值。
if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
    add_compile_definitions(RAINDOCK_USE_DUILIB=1)
endif()

# ---------- 编译标准与特性 ----------
# C++20：上游 Rainmeter 用 std::numbers / string_view::starts_with（Common/MathParser、ParseUtil）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    add_compile_definitions(_UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------- 头文件包含 ----------
include_directories(
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Common
    ${CMAKE_SOURCE_DIR}/ThirdParty
    ${CMAKE_SOURCE_DIR}/Dock
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}/App
    ${CMAKE_SOURCE_DIR}         # 允许 `#include "../ThirdParty/..."` 相对路径从 Library/ 解析
)

# ---------- 核心引擎静态库（Rainmeter 派生） ----------
# 骨架阶段：用 GLOB 收集；工具链就位并提取 Rainmeter 源码后，建议改为显式列表。
file(GLOB RAINDOCK_CORE_SOURCES  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Library/*.cpp)
file(GLOB RAINDOCK_CORE_HEADERS  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Library/*.h)

# Common 基础设施（Rainmeter 上游 Common/ 直拷批：StringUtil/MathParser/Timer/
# FileUtil/PathUtil/ParseUtil/StringParser/RawString，提取路线见 MODULE_EXTRACTION.md §4）
file(GLOB RAINDOCK_COMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Common/*.cpp)
file(GLOB RAINDOCK_COMMON_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Common/*.h)

# ---------- Batch-2: PCRE16 正则引擎（IfActions 的 IfMatch 依赖） ----------
# 上游 Library/Pcre.h 封装 pcre16_compile/pcre16_exec；直接把所有 pcre*.c
# 加进 RainDeskPlusCore 静态库（避免额外 SHARED 库）。排除 *_printint.c 调试
# 辅助文件，它引用 main() 会造成链接歧义。
file(GLOB RAINDOCK_PCRE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/ThirdParty/pcre/pcre*.c
    ${CMAKE_SOURCE_DIR}/ThirdParty/pcre/pcre16_*.c)
if(RAINDOCK_PCRE_SOURCES)
    list(FILTER RAINDOCK_PCRE_SOURCES EXCLUDE REGEX "/pcre(_|16_)printint\\.c$")
    # MSVC：PCRE 在 C 模式下会报 /W4 级别告警（unused param / conditional
    # expression is constant 等）。逐文件降级为 /W0 避免污染 Batch-2 的 0-warning
    # 目标；同时在 C 编译时定义 PCRE 宏要求的 SUPPORT_UTF16。
    if(MSVC)
        set_source_files_properties(${RAINDOCK_PCRE_SOURCES} PROPERTIES
            COMPILE_FLAGS "/W0 /DSUPPORT_UTF16 /DPCRE_STATIC=1 /DHAVE_CONFIG_H=1")
    else()
        set_source_files_properties(${RAINDOCK_PCRE_SOURCES} PROPERTIES
            COMPILE_DEFINITIONS "SUPPORT_UTF16;PCRE_STATIC=1;HAVE_CONFIG_H=1")
    endif()
endif()

add_library(RainDeskPlusCore STATIC
    ${RAINDOCK_CORE_SOURCES} ${RAINDOCK_CORE_HEADERS}
    ${RAINDOCK_COMMON_SOURCES} ${RAINDOCK_COMMON_HEADERS}
    ${RAINDOCK_PCRE_SOURCES})
target_include_directories(RainDeskPlusCore PUBLIC
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Common
    ${CMAKE_SOURCE_DIR}/ThirdParty
    ${CMAKE_SOURCE_DIR})
target_compile_definitions(RainDeskPlusCore PUBLIC
    PCRE_STATIC=1              # Pcre.h 不走 dllimport/dllexport
    HAVE_CONFIG_H=1            # pcre_internal.h 引用 ThirdParty/pcre/config.h
    SUPPORT_UTF16)             # Pcre 封装用 UTF-16 API

# Rainmeter 派生模块在 Windows 上依赖系统 API：
if(WIN32)
    target_link_libraries(RainDeskPlusCore PUBLIC
        advapi32      # 性能计数器、注册表
        pdh           # CPU 采样 (Pdh*)
        iphlpapi      # 网络流量 (GetIfTable 等)
        psapi         # 进程信息
        wininet       # 部分网络 Measure
        shlwapi       # 路径
        d2d1 dwrite dwmapi  # Direct2D / DirectWrite / DWM（注：SDK 只有 dwrite.lib，无 dwrite_3.lib）
        windowscodecs # 图像解码
        gdiplus gdi32 user32 ole32 oleaut32 uxtheme
        imagehlp      # Common/FileUtil（PE 校验）
    )
endif()

# ---------- UI 适配层（基础设施，须先于 Dock 创建：Dock 依赖它，DD-10） ----------
# UI/DuilibWindowBase.h 是纯头文件适配层（唯一 .cpp 为空占位），不链 Duilib：
# RAINDOCK_USE_DUILIB 的开关分支由各 TU 自行 #ifdef（DD-2）。
file(GLOB RAINDOCK_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/UI/*.cpp)
file(GLOB RAINDOCK_UI_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/UI/*.h)
if(RAINDOCK_UI_SOURCES OR RAINDOCK_UI_HEADERS)
    add_library(RainDeskPlusUI STATIC ${RAINDOCK_UI_SOURCES} ${RAINDOCK_UI_HEADERS})
    target_include_directories(RainDeskPlusUI PUBLIC ${CMAKE_SOURCE_DIR}/UI)
    target_link_libraries(RainDeskPlusUI PUBLIC RainDeskPlusCore)
endif()

# ---------- Dock 模块 ----------
if(RAINDOCK_BUILD_DOCK)
    file(GLOB RAINDOCK_DOCK_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Dock/*.cpp)
    file(GLOB RAINDOCK_DOCK_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Dock/*.h)
    add_library(RainDeskPlusDock STATIC ${RAINDOCK_DOCK_SOURCES} ${RAINDOCK_DOCK_HEADERS})
    target_include_directories(RainDeskPlusDock PUBLIC ${CMAKE_SOURCE_DIR}/Dock)
    target_link_libraries(RainDeskPlusDock PUBLIC RainDeskPlusCore)

    # 适配层是底层，Dock 依赖它（DD-10 反转依赖方向：原为 UI → Dock）。
    if(TARGET RainDeskPlusUI)
        target_link_libraries(RainDeskPlusDock PUBLIC RainDeskPlusUI)
    endif()

    if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
        # PRIVATE：Duilib 只被 DockWindow.cpp 使用，不向消费者外泄 UIlib.h 的
        # NOMINMAX 兼容负担；静态库的 PRIVATE 依赖仍会传播到最终链接行。
        target_link_libraries(RainDeskPlusDock PRIVATE Duilib)
        # DockWindow.cpp 是唯一包含 Duilib 头的 TU，必须撤销目录级
        # NOMINMAX / WIN32_LEAN_AND_MEAN（上游 StdAfx.h 依赖 min/max 宏与
        # winsock.h），并降告警级别（上游 VS2010 时代代码在 /W4 下噪声极大）——DD-4。
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/Dock/DockWindow.cpp
            PROPERTIES COMPILE_OPTIONS "/UNOMINMAX;/UWIN32_LEAN_AND_MEAN;/W0;/utf-8")
    endif()
endif()

# ---------- 主程序 ----------
add_executable(RainDeskPlus App/main.cpp)
# GUI 子系统（入口为 wWinMain），避免链接器找 main
set_target_properties(RainDeskPlus PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(RainDeskPlus PRIVATE RainDeskPlusCore)
if(RAINDOCK_BUILD_DOCK)
    target_link_libraries(RainDeskPlus PRIVATE RainDeskPlusDock)
    target_compile_definitions(RainDeskPlus PRIVATE RAINDOCK_BUILD_DOCK=1)

    # D16：App/main.cpp 在 RAINDOCK_BUILD_DOCK 下包含 Dock/DockWindow.h，ON 线会
    # 间接触及 DuiLib/UIlib.h（经 UI/DuilibWindowBase.h）。RainDeskPlusDock 以
    # PRIVATE 链接 Duilib（DD-2），包含目录不会外泄，故此处按其它 ON 线消费者
    # 的范式补链 Duilib，取回其 PUBLIC 包含目录与 UILIB_STATIC 定义；并对该 TU
    # 施加与 DockWindow.cpp 同源的宽松一致性选项（DD-4）。
    if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
        target_link_libraries(RainDeskPlus PRIVATE Duilib)
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/App/main.cpp
            PROPERTIES COMPILE_OPTIONS "/UNOMINMAX;/UWIN32_LEAN_AND_MEAN;/W0;/utf-8")
    endif()
endif()

# ---------- 测试 ----------
if(RAINDOCK_BUILD_TESTS)
    enable_testing()
    # 工具链就位后：include(FetchContent) 引入 GoogleTest
    # add_subdirectory(Tests)
endif()

# M3.3 离屏渲染 Smoke 测试（不依赖 GoogleTest，独立可执行）
add_executable(RainDeskPlusRenderTest Tests/test_meter_render.cpp)
target_link_libraries(RainDeskPlusRenderTest PRIVATE RainDeskPlusCore)

# M4 Skin 加载 Smoke 测试
add_executable(RainDeskPlusSkinTest Tests/test_skin_load.cpp)
target_link_libraries(RainDeskPlusSkinTest PRIVATE RainDeskPlusCore)

# M5 Bang 命令 Smoke 测试
add_executable(RainDeskPlusBangTest Tests/test_bang_commands.cpp)
target_link_libraries(RainDeskPlusBangTest PRIVATE RainDeskPlusCore)

# D13-14 D1/D2/D4：DockBar 纯逻辑 Smoke（无 Duilib 依赖，两条构建线都构建）
if(RAINDOCK_BUILD_DOCK)
    add_executable(RainDeskPlusDockCoreTest Tests/test_dock_core.cpp)
    target_link_libraries(RainDeskPlusDockCoreTest PRIVATE RainDeskPlusDock)
endif()

# D15：Dock 拖放加项 Smoke（两条构建线都构建）
# OFF 线只验证数据模型（路径 → 派生 → 去重 → 落库）；ON 线额外验证界面增量与尺寸契约。
if(RAINDOCK_BUILD_DOCK)
    add_executable(RainDeskPlusDockDropTest Tests/test_dock_drop.cpp)
    target_link_libraries(RainDeskPlusDockDropTest PRIVATE RainDeskPlusDock)

    # ON 线：DockWindow.h 经 RAINDOCK_USE_DUILIB 分支引入 Duilib 头，
    # 需与 RainDeskPlusDockWindowTest 同源的宽松一致性选项（DD-4）。
    if(RAINDOCK_USE_DUILIB AND TARGET Duilib)
        set_target_properties(RainDeskPlusDockDropTest PROPERTIES
            CXX_STANDARD 14
            CXX_STANDARD_REQUIRED ON)
        target_compile_options(RainDeskPlusDockDropTest PRIVATE
            /UNOMINMAX /UWIN32_LEAN_AND_MEAN /W0 /utf-8)
        target_link_libraries(RainDeskPlusDockDropTest PRIVATE Duilib)
    endif()
endif()

# D13-14 D3：DockWindow 透明/异形窗口 Smoke（需真实 Duilib，仅 ON 线构建）
if(RAINDOCK_BUILD_DOCK AND RAINDOCK_USE_DUILIB AND TARGET Duilib)
    add_executable(RainDeskPlusDockWindowTest Tests/test_dock_window.cpp)
    # 与 RainDeskPlusDuilibTest 同源：上游头文件需要宽松一致性模式与 min/max 宏
    set_target_properties(RainDeskPlusDockWindowTest PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON)
    target_compile_options(RainDeskPlusDockWindowTest PRIVATE
        /UNOMINMAX /UWIN32_LEAN_AND_MEAN /W0 /utf-8)
    target_link_libraries(RainDeskPlusDockWindowTest PRIVATE RainDeskPlusDock Duilib)
endif()

message(STATUS "RainDeskPlus ${PROJECT_VERSION} configured")
message(STATUS "  RAINDOCK_USE_DUILIB  = ${RAINDOCK_USE_DUILIB}")
message(STATUS "  RAINDOCK_BUILD_DOCK  = ${RAINDOCK_BUILD_DOCK}")
message(STATUS "  RAINDOCK_BUILD_TESTS = ${RAINDOCK_BUILD_TESTS}")
