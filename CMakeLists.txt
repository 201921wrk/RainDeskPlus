# RainDeskPlus - Desktop beautification platform
# Based on Rainmeter (https://github.com/rainmeter/rainmeter) - GPL v2
# Copyright (C) 2014-2025 Rainmeter Project
# Copyright (C) 2026 RainDeskPlus Project
cmake_minimum_required(VERSION 3.20)

project(RainDeskPlus
    VERSION 0.1.0
    DESCRIPTION "Desktop beautification platform: widgets + dock + skins"
    LANGUAGES CXX)

# ---------- 全局选项 ----------
option(RAINDOCK_USE_DUILIB    "Enable Duilib UI integration (requires vendored Duilib)" OFF)
option(RAINDOCK_BUILD_DOCK    "Build Dock bar module"                                   ON)
option(RAINDOCK_BUILD_TESTS   "Build unit tests (requires GoogleTest)"                  OFF)
option(RAINDOCK_BUILD_PLUGINS "Build plugin DLLs"                                       OFF)

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
    ${CMAKE_SOURCE_DIR}/Dock
    ${CMAKE_SOURCE_DIR}/UI
    ${CMAKE_SOURCE_DIR}/App
)

# ---------- 核心引擎静态库（Rainmeter 派生） ----------
# 骨架阶段：用 GLOB 收集；工具链就位并提取 Rainmeter 源码后，建议改为显式列表。
file(GLOB RAINDOCK_CORE_SOURCES  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Library/*.cpp)
file(GLOB RAINDOCK_CORE_HEADERS  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Library/*.h)

# Common 基础设施（Rainmeter 上游 Common/ 直拷批：StringUtil/MathParser/Timer/
# FileUtil/PathUtil/ParseUtil/StringParser/RawString，提取路线见 MODULE_EXTRACTION.md §4）
file(GLOB RAINDOCK_COMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Common/*.cpp)
file(GLOB RAINDOCK_COMMON_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Common/*.h)

add_library(RainDeskPlusCore STATIC
    ${RAINDOCK_CORE_SOURCES} ${RAINDOCK_CORE_HEADERS}
    ${RAINDOCK_COMMON_SOURCES} ${RAINDOCK_COMMON_HEADERS})
target_include_directories(RainDeskPlusCore PUBLIC
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Common)

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

# ---------- Dock 模块 ----------
if(RAINDOCK_BUILD_DOCK)
    file(GLOB RAINDOCK_DOCK_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Dock/*.cpp)
    file(GLOB RAINDOCK_DOCK_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Dock/*.h)
    add_library(RainDeskPlusDock STATIC ${RAINDOCK_DOCK_SOURCES} ${RAINDOCK_DOCK_HEADERS})
    target_include_directories(RainDeskPlusDock PUBLIC ${CMAKE_SOURCE_DIR}/Dock)
    target_link_libraries(RainDeskPlusDock PUBLIC RainDeskPlusCore)

    # Duilib 接入（需先 vendor Duilib 源码到 third_party/duilib）
    if(RAINDOCK_USE_DUILIB)
        # 期望路径：third_party/duilib
        if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/duilib)
            message(WARNING
                "RAINDOCK_USE_DUILIB=ON but third_party/duilib not found. "
                "Vendor Duilib first (see docs/BUILD.md).")
        else()
            add_subdirectory(third_party/duilib)
            target_link_libraries(RainDeskPlusDock PUBLIC Duilib)
            target_compile_definitions(RainDeskPlusDock PUBLIC RAINDOCK_USE_DUILIB=1)
        endif()
    endif()
endif()

# ---------- UI 适配层 ----------
file(GLOB RAINDOCK_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/UI/*.cpp)
file(GLOB RAINDOCK_UI_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/UI/*.h)
if(RAINDOCK_UI_SOURCES OR RAINDOCK_UI_HEADERS)
    add_library(RainDeskPlusUI STATIC ${RAINDOCK_UI_SOURCES} ${RAINDOCK_UI_HEADERS})
    target_include_directories(RainDeskPlusUI PUBLIC ${CMAKE_SOURCE_DIR}/UI)
    target_link_libraries(RainDeskPlusUI PUBLIC RainDeskPlusCore)
    if(RAINDOCK_BUILD_DOCK)
        target_link_libraries(RainDeskPlusUI PUBLIC RainDeskPlusDock)
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

message(STATUS "RainDeskPlus ${PROJECT_VERSION} configured")
message(STATUS "  RAINDOCK_USE_DUILIB  = ${RAINDOCK_USE_DUILIB}")
message(STATUS "  RAINDOCK_BUILD_DOCK  = ${RAINDOCK_BUILD_DOCK}")
message(STATUS "  RAINDOCK_BUILD_TESTS = ${RAINDOCK_BUILD_TESTS}")
