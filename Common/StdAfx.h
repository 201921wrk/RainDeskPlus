// RainDeskPlus - Desktop beautification platform
// Derived from Rainmeter - GPL v2
// Copyright (C) 2014-2025 Rainmeter Project
// Copyright (C) 2026 RainDeskPlus Project
//
// 轻量版 Common 预编译头替代：上游 Common/StdAfx.h 还包含 d2d1_1/wrl/ankerl
// 等 UI 与第三方头，本项目按需在各翻译单元显式包含。

#pragma once

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include <Windows.h>
#include <Commctrl.h>
#include <Shlobj.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>

#include <string>
