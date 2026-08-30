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
// Batch-2 upstream Common modules (Platform.cpp) use the VersionHelpers.h
// inline wrappers: IsWindows10OrGreater(), IsWindowsServer(), …
#include <VersionHelpers.h>
#include <Commctrl.h>
#include <Shlobj.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>

#include <string>
// Batch-2 upstream Common/Library modules use std::min / std::max on integral
// pair values unqualified (Section.cpp L41 `min(counter, divider)` and
// PdhUtil.cpp L79 `min(rawCount, count)`).  NOMINMAX is set globally via
// CMakeLists.txt, so the Windows SDK macros won't shadow; a local `using`
// keeps the upstream code verbatim.
#include <algorithm>
using std::min;
using std::max;
// PdhUtil.cpp L92 references `ankerl::unordered_dense::map` verbatim.  Same
// warning firewall as Library/StdAfx.h to keep /W4 clean.
#pragma warning(push, 0)
#include "ankerl/unordered_dense.h"
#pragma warning(pop)

// Batch-2 upstream Common modules carry two /W4 warnings that are inherent to
// the verbatim source and cannot be fixed without editing it:
//   C4245 — CharacterEntityReference.cpp L392 `size_t pos = -1;` (int→size_t
//           sign mismatch; deliberate npos idiom).
//   C4702 — Platform.cpp L62-68 unreachable code on x64 builds (the WOW64
//           fallback is only reachable on x86; guarded by `#if _WIN64`).
#pragma warning(disable:4245 4702)
