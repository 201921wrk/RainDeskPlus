/*
 * RainDeskPlus - Desktop beautification platform
 * 测试占位（GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 骨架阶段测试占位：RAINDOCK_BUILD_TESTS=ON 且接入 GoogleTest 后，
 * 在根 CMakeLists.txt 取消 `add_subdirectory(Tests)` 注释。
 *
 * 计划覆盖（参考 docs/AGENT_PROMPTS.md §4 测试工程师 Agent）：
 *   - ConfigParser: INI 解析（变量/段/类型转换/边界）
 *   - DockMagnify: 距离衰减曲线（近/远/边界）
 *   - DockBar: AddItem/RemoveItem/ClearItems 幂等性、重复 id 替换
 *   - MeasureTime: 格式化输出
 */
#if 0
#include <gtest/gtest.h>
TEST(Placeholder, Smoke)
{
    EXPECT_EQ(1, 1);
}
#endif

// 无 GoogleTest 时的占位 main，避免空编译单元。
int main() { return 0; }
