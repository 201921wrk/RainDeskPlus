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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>

namespace raindock {

class ConfigParser;
class Measure;

class Meter
{
public:
    virtual ~Meter() = default;

    virtual void Initialize(ConfigParser& parser, Measure* measure);
    virtual void Update();   // 刷新绑定的 Measure
    virtual void Draw(ID2D1RenderTarget* rt) = 0;   // 子类实现渲染

    int GetX() const { return m_X; }
    int GetY() const { return m_Y; }
    int GetWidth()  const { return m_W; }
    int GetHeight() const { return m_H; }
    bool GetHidden() const { return m_Hidden; }

    const std::wstring& GetName() const { return m_Name; }
    void SetName(const std::wstring& n) { m_Name = n; }

protected:
    Measure*       m_Measure = nullptr;
    std::wstring   m_Name;
    int            m_X = 0, m_Y = 0, m_W = 0, m_H = 0;
    bool           m_Hidden = false;
    bool           m_Solid = false;       // 背景填充
    D2D1_COLOR_F   m_SolidColor = {0,0,0,0};
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_METER_H_
