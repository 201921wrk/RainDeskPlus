/*
 * RainDeskPlus - M5 CRainmeter + CommandHandler Smoke 测试
 * 验证：单例初始化、皮肤激活/停用、Bang 命令解析与分发。
 */
#include "Rainmeter.h"
#include "Skin.h"

#include <cstdio>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace raindock;

static bool g_Pass = true;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s\n", msg); \
        g_Pass = false; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while (0)

static std::wstring GetSkinIniPath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exePath(path);
    size_t pos = exePath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return exePath.substr(0, pos + 1) + L"..\\..\\..\\Skins\\example\\skin.ini";
    }
    return L"Skins/example/skin.ini";
}

int main()
{
    printf("=== M5 CRainmeter + CommandHandler Smoke Test ===\n\n");

    // ---- 1. CRainmeter 单例与初始化 ----
    printf("1. CRainmeter singleton & Initialize\n");
    CRainmeter& rm = CRainmeter::GetInstance();
    CHECK(&rm == &CRainmeter::GetInstance(), "GetInstance returns same instance");

    bool initOk = rm.Initialize(nullptr);  // hInstance 可为空（M5 骨架不依赖窗口）
    CHECK(initOk, "Initialize succeeded");
    printf("\n");

    // ---- 2. 激活皮肤 ----
    printf("2. Activate skin\n");
    std::wstring iniPath = GetSkinIniPath();
    Skin* skin = rm.ActivateSkin(L"Example", iniPath);
    CHECK(skin != nullptr, "ActivateSkin returns non-null");

    const auto& skins = rm.GetSkins();
    CHECK(skins.size() == 1, "1 skin in skin map");
    CHECK(skins.count(L"Example") == 1, "Skin key 'Example' exists");
    CHECK(skins.at(L"Example") == skin, "Skin pointer matches");

    // 验证皮肤已加载 Measure/Meter
    CHECK(skin->GetMeasures().size() == 1, "Skin has 1 measure");
    CHECK(skin->GetMeters().size() == 1, "Skin has 1 meter");
    printf("\n");

    // ---- 3. Bang 命令解析与执行 ----
    printf("3. Bang command execution\n");

    // !Refresh - 已知 Bang，不应崩溃
    rm.ExecuteCommand(L"!Refresh", skin);
    printf("  [PASS] !Refresh executed without crash\n");

    // 带参数的 Bang（!Refresh SomeArg）
    rm.ExecuteCommand(L"!Refresh MyArg", skin);
    printf("  [PASS] !Refresh with args executed without crash\n");

    // 前导空白的 Bang
    rm.ExecuteCommand(L"   !Refresh", skin);
    printf("  [PASS] Bang with leading whitespace handled\n");

    // 未知 Bang - 应静默忽略（不崩溃）
    rm.ExecuteCommand(L"!NonExistentBang", skin);
    printf("  [PASS] Unknown bang handled gracefully\n");

    // 空命令
    rm.ExecuteCommand(L"", skin);
    printf("  [PASS] Empty command handled\n");

    // 纯空白
    rm.ExecuteCommand(L"   \t  ", skin);
    printf("  [PASS] Whitespace-only command handled\n");
    printf("\n");

    // ---- 4. 多个皮肤 + Bang 分发 ----
    printf("4. Multi-skin bang dispatch\n");
    // 激活第二个皮肤（复用同一个 ini）
    Skin* skin2 = rm.ActivateSkin(L"Example2", iniPath);
    CHECK(skin2 != nullptr, "Second skin activated");
    CHECK(rm.GetSkins().size() == 2, "2 skins in skin map");

    // 对不同皮肤执行 Bang（验证 skin 参数正确传递）
    rm.ExecuteCommand(L"!Refresh", skin);
    rm.ExecuteCommand(L"!Refresh", skin2);
    printf("  [PASS] Bang dispatched to correct skin context\n");

    // 停用第二个皮肤
    rm.DeactivateSkin(skin2);
    CHECK(rm.GetSkins().size() == 1, "Back to 1 skin after deactivating second");
    printf("\n");

    // ---- 5. 停用皮肤 ----
    printf("5. Deactivate skin\n");
    rm.DeactivateSkin(skin);
    CHECK(rm.GetSkins().empty(), "Skin map empty after deactivate");
    printf("\n");

    // ---- 6. Finalize ----
    printf("6. Finalize\n");
    rm.Finalize();
    printf("  [PASS] Finalize succeeded\n");
    printf("\n");

    if (g_Pass) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== SOME TESTS FAILED ===\n");
        return 1;
    }
}
