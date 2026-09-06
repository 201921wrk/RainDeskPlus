/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Meter.h（可视化元素基类，直接复用）。
 * 渲染接口用 Direct2D 的 ID2D1RenderTarget。
 */
#ifndef RAINDOCK_LIBRARY_METER_H_
#define RAINDOCK_LIBRARY_METER_H_

#include <cstdint>
#include <string>

#include "Section.h"
#include "Util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>

// 上游 Mouse 类位于全局作用域；此处前置声明供 per-meter 鼠标动作使用。
class Mouse;

namespace raindock {

class ConfigParser;
class Measure;
class Skin;

class Meter : public ::Section
{
public:
    virtual ~Meter();

    virtual void Initialize(ConfigParser& parser, Measure* measure);
    virtual void Update();   // 刷新绑定的 Measure
    virtual void Draw(ID2D1RenderTarget* rt) = 0;   // 子类实现渲染

    int GetX() const { return m_X; }
    int GetY() const { return m_Y; }
    int GetWidth()  const { return m_W; }
    int GetHeight() const { return m_H; }
    bool GetHidden() const { return m_Hidden; }

    // per-meter 鼠标动作（懒创建，Meter.cpp 中接入上游 ::Mouse）。
    ::Mouse& GetMouse();

    UINT GetBaseTypeID() override { return TypeID<Meter>(); }

protected:
    Meter(Skin* skin, const WCHAR* name) : Section(skin, name) {}

    Measure*       m_Measure = nullptr;
    int            m_X = 0, m_Y = 0, m_W = 0, m_H = 0;
    bool           m_Hidden = false;
    bool           m_Solid = false;       // 背景填充
    D2D1_COLOR_F   m_SolidColor = {0,0,0,0};

private:
    ::Mouse*       m_Mouse = nullptr;     // 懒创建，Meter.cpp 拥有所有权
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_METER_H_
