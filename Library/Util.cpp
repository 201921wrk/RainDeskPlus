/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 最小本地版本：运行时类型标识。
 */
#include "Util.h"

#include <atomic>

UINT GetUniqueID()
{
    // TypeID<T>() 的类型 id 可能在多个线程首次实例化同名模板时并发申请，
    // 普通 static 的 "读-改-写" 不是原子操作，会返回重复 id（详见 D10 审查 #15）。
    static std::atomic<UINT> id{ 0 };
    return id.fetch_add(1, std::memory_order_relaxed);
}
