/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 最小本地版本：运行时类型标识（对应上游 rainmeter-upstream/Library/Util.h
 * 的 GetUniqueID / TypeID<T> 部分）。其余上游 Util 功能（GetString / GetIcon /
 * GetFormattedString 等）暂不引入。
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // UINT

// 返回进程内唯一 ID（每调用一次递增）。
UINT GetUniqueID();

template <typename T>
UINT TypeID() { static UINT id = GetUniqueID(); return id; }
