/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 最小本地版本：运行时类型标识。
 */
#include "Util.h"

UINT GetUniqueID()
{
    static UINT id = 0;
    return id++;
}
