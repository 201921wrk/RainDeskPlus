/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterString.h（文本渲染，直接复用）。
 * 依赖：DirectWrite（IDWriteTextFormat / IDWriteTextLayout）。
 */
#ifndef RAINDOCK_LIBRARY_METER_STRING_H_
#define RAINDOCK_LIBRARY_METER_STRING_H_

#include "Meter.h"

struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace raindock {

class MeterString : public Meter
{
public:
    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Draw(ID2D1RenderTarget* rt) override;

    void SetText(const std::wstring& text) { m_Text = text; }
    const std::wstring& GetText() const { return m_Text; }

private:
    std::wstring        m_Text;
    std::wstring        m_FontFamily = L"Segoe UI";
    float               m_FontSize = 12.0f;
    D2D1_COLOR_F        m_Color = {1,1,1,1};
    IDWriteTextFormat*  m_TextFormat = nullptr;   // 由共享 DirectWrite 工厂创建
    IDWriteTextLayout*  m_TextLayout = nullptr;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_METER_STRING_H_
