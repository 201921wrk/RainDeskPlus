/*
 * RainDeskPlus - Desktop beautification platform
 * UI 适配层（GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 空占位编译单元。
 *
 * 本文件**故意不包含任何头文件**（尤其不包含 DuilibWindowBase.h）：
 * - RAINDOCK_USE_DUILIB=ON 时 DuilibWindowBase.h 会拉入 "UIlib.h"，
 *   而 RainDeskPlusUI 未链接 Duilib、也未加 /UNOMINMAX /UWIN32_LEAN_AND_MEAN，
 *   一旦在此包含必然编译失败（见 docs/DOCK_DESIGN.md §5.3 尾注、DD-4）。
 * - 保留空 TU 是为了让 CMake 能推断 RainDeskPlusUI 的链接语言（CXX）。
 */
