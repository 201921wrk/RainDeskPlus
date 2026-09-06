/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterString.h（文本渲染，D2D 重构版）。
 * 依赖：DirectWrite（IDWriteTextLayout）+ Direct2D（ID2D1RenderTarget）。
 */
#ifndef RAINDOCK_LIBRARY_METER_STRING_H_
#define RAINDOCK_LIBRARY_METER_STRING_H_

#include "Meter.h"

#include <dwrite.h>

struct IDWriteTextLayout;
struct ID2D1SolidColorBrush;

namespace raindock {

class MeterString : public Meter
{
public:
    MeterString(Skin* skin, const WCHAR* name) : Meter(skin, name) {}
    ~MeterString() override;

    UINT GetTypeID() override { return TypeID<MeterString>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Update() override;
    void Draw(ID2D1RenderTarget* rt) override;

    // 更新文本（文本变化时自动重建 TextLayout）。
    void SetText(const std::wstring& text);
    const std::wstring& GetText() const { return m_Text; }

    // 文本度量尺寸（AutoSize 语义，由 DirectWrite metrics 决定）。
    int GetTextWidth()  const { return m_W; }
    int GetTextHeight() const { return m_H; }

private:
    void CreateLayout();
    // 基于原始 Text=（%1 → Measure 字符串）重新组装 m_Text。
    void ResolveText();

    std::wstring          m_TextOpt;    // INI 原始 Text= 值（空 = 直接用 Measure 字符串）
    std::wstring          m_Text;
    std::wstring          m_FontFamily = L"Segoe UI";
    float                 m_FontSize = 12.0f;
    D2D1_COLOR_F          m_Color = {1,1,1,1};
    DWRITE_TEXT_ALIGNMENT m_AlignH = DWRITE_TEXT_ALIGNMENT_LEADING;

    IDWriteTextLayout*    m_TextLayout = nullptr;
    // Brush 与渲染目标绑定：rt 变化时重建（COM 引用持有 rt，保证比较/使用安全）。
    ID2D1SolidColorBrush* m_Brush = nullptr;
    ID2D1RenderTarget*    m_BrushRT = nullptr;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_METER_STRING_H_
