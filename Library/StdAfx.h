// RainDeskPlus - Batch-2 upstream TUs compatibility layer.
// This file is included FIRST by every upstream .cpp (Section, IfActions,
// Group, Mouse, Logger, …) copied from rainmeter-upstream into beautiful_app.
//
// Strategy: instead of rewriting 200+ call sites in upstream sources to use
// `raindock::` qualified names, we pull the full local library + common into
// the global namespace via `using namespace raindock;` after including the
// local headers. This matches the upstream convention (bare ConfigParser /
// Skin / Measure / Meter / StringParser / MathParser / CRainmeter / …).
//
// M4/M5 core files are NEVER overwritten; this StdAfx.h is brand-new and not
// on the 44-file SKIP list.

#pragma once

// Upstream Batch-2 modules use the "unsafe" wide-char CRT families
// (_wcsupr, wcsncpy_s' siblings, etc.) which trigger MSVC C4996 in /W4.
// Scope-restricted to the StdAfx.h umbrella (only upstream .cpp copies
// include this header; M4/M5 local files never pull from here).
#define _CRT_SECURE_NO_WARNINGS

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

// Namespace-scope shadow warnings (C4458: class member hidden by local;
// C4457: function param hidden by local) come from verbatim upstream
// copies of IfActions.h/IfActions.cpp.  NOTE: bare `disable` (no push/pop)
// so it stays in force for the WHOLE translation unit — IfActions.h is
// #include'd by each upstream .cpp AFTER this StdAfx.h, so a push/pop pair
// scoped to this header would expire before IfActions.h is even parsed.
#pragma warning(disable:4458 4457)

// ---------- Win32 + STL min-set needed by Batch-2 (aligned to upstream StdAfx
// but omitting rarely-used heavy headers to speed compile) ----------
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <comdef.h>
#include <Iphlpapi.h>
#include <Mmsystem.h>
#include <Shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <Wininet.h>
#include <wrl/client.h>

#include <array>
#include <map>
#include <set>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cerrno>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <process.h>

// ---------- ThirdParty (path resolved via CMake include dirs) ----------
// ankerl/unordered_dense.h emits hundreds of template-instantiation noise
// warnings under MSVC /W4 (C4127 conditional is constant, C4100 unreferenced
// param, etc.); build behind a warning firewall.
#pragma warning(push, 0)
#include "ankerl/unordered_dense.h"
#pragma warning(pop)
// fmt/base.h + fmt/xchar.h are intentionally NOT included here to keep the
// Batch-2 compile footprint small. Batch-2 modules (Section/IfActions/Group/
// Mouse/Logger) never call fmt::format directly — all logging goes through
// the C-va_list Logger::LogVF family, which our minimal Logger.cpp implements
// with vswprintf_s. If a later batch truly needs fmt, copy upstream-fills/
// ThirdParty/fmt/ and uncomment these lines.
// #include "fmt/base.h"
// #include "fmt/xchar.h"

// ---------- Additional macros shared with upstream ----------
#define IsCtrlKeyDown()     (GetKeyState(VK_CONTROL) < 0)
#define IsShiftKeyDown()    (GetKeyState(VK_SHIFT) < 0)
#define IsAltKeyDown()      (GetKeyState(VK_MENU) < 0)

// ---------- RainDeskPlus local Common + Library headers ----------
// (All live in `namespace raindock {}`. After inclusion we pull the names into
// global scope via `using namespace raindock;`, matching upstream TUs.)
#include "../Common/StdAfx.h"
#include "../Common/StringParser.h"
#include "../Common/MathParser.h"

#include "ConfigParser.h"
#include "Rainmeter.h"
#include "Skin.h"
#include "Measure.h"
#include "Meter.h"
// Logger.{h,cpp} expose the LogErrorF / LogWarningF / GetLogger() family that
// every upstream TU calls (e.g. IfActions.cpp L205 LogErrorF).
#include "Logger.h"

// ---------- Namespace compatibility shim ----------
using namespace raindock;

// ---------- Free-standing singletons expected by upstream call sites ----------
// Upstream writes `GetRainmeter().ExecuteActionCommand(...)` and
// `GetLogger().LogVF(...)`; Logger.h already provides inline GetLogger()
// returning Logger::GetInstance().
//
// NOTE: intentionally NO bare `class CRainmeter;` global forward here — the
// class lives solely in `namespace raindock`, and `using namespace raindock;`
// above already brings `raindock::CRainmeter` into unqualified lookup. Adding
// a separate global forward would create a C2872 ambiguous-symbol collision.
inline CRainmeter& GetRainmeter() { return raindock::CRainmeter::GetInstance(); }
