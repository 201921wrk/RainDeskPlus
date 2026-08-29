/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterString.cpp（文本渲染，D2D 重构版）。
 *
 * M3 实现：
 *   - Initialize 读 Text / FontFace / FontSize / FontColor / StringAlign
 *   - Text=%1 引用绑定 Measure 的字符串值（对齐上游 MeterString Text 键语义）
 *   - IDWriteTextLayout 承载布局（AutoSize：文本 metrics 决定 m_W/m_H）
 *   - Draw 用 ID2D1RenderTarget::DrawTextLayout 渲染
 * TODO(M4+): StringEffect / ClipString / Percentual / 动态变量刷新。
 */
#include "MeterString.h"

#include "Canvas.h"
#include "ConfigParser.h"
#include "Measure.h"

#include <d2d1helper.h>

namespace raindock {

MeterString::~MeterString()
{
    if (m_Brush)     { m_Brush->Release();     m_Brush = nullptr; }
    if (m_BrushRT)   { m_BrushRT->Release();   m_BrushRT = nullptr; }
    if (m_TextLayout){ m_TextLayout->Release(); m_TextLayout = nullptr; }
}

void MeterString::Initialize(ConfigParser& parser, Measure* measure)
{
    Meter::Initialize(parser, measure);
    m_Measure = measure;

    m_FontFamily = parser.ReadString(m_Name, L"FontFace", m_FontFamily);
    m_FontSize   = static_cast<float>(parser.ReadFloat(m_Name, L"FontSize", m_FontSize));
    m_Color      = parser.ReadColor(m_Name, L"FontColor", D2D1_COLOR_F{1,1,1,1});

    const std::wstring align = parser.ReadString(m_Name, L"StringAlign", L"Left");
    if (_wcsicmp(align.c_str(), L"Center") == 0)      m_AlignH = DWRITE_TEXT_ALIGNMENT_CENTER;
    else if (_wcsicmp(align.c_str(), L"Right") == 0)  m_AlignH = DWRITE_TEXT_ALIGNMENT_TRAILING;
    else                                              m_AlignH = DWRITE_TEXT_ALIGNMENT_LEADING;

    m_TextOpt = parser.ReadString(m_Name, L"Text", L"");
    ResolveText();
    CreateLayout();
}

void MeterString::Update()
{
    Meter::Update();
    // Measure 值可能已变（如时钟），重组文本；SetText 内部会按需 relayout。
    ResolveText();
}

void MeterString::ResolveText()
{
    if (!m_TextOpt.empty()) {
        const wchar_t* mv = m_Measure ? m_Measure->GetString() : nullptr;
        std::wstring out;
        out.reserve(m_TextOpt.size() + 32);
        for (size_t i = 0; i < m_TextOpt.size(); ++i) {
            if (m_TextOpt[i] == L'%' && i + 1 < m_TextOpt.size() && m_TextOpt[i + 1] == L'1') {
                if (mv) out += mv;
                ++i;
            } else {
                out += m_TextOpt[i];
            }
        }
        SetText(std::move(out));
    } else if (m_Measure) {
        const wchar_t* mv = m_Measure->GetString();
        SetText(mv ? mv : L"");
    }
}

void MeterString::SetText(const std::wstring& text)
{
    if (m_Text == text) return;
    m_Text = text;
    CreateLayout();
}

void MeterString::CreateLayout()
{
    if (m_TextLayout) { m_TextLayout->Release(); m_TextLayout = nullptr; }
    m_W = 0;
    m_H = 0;

    IDWriteFactory* wf = Canvas::GetWriteFactory();
    if (!wf || m_Text.empty()) return;

    // TextFormat 仅为构建 TextLayout 所需，layout 持有其引用，此处随即释放。
    IDWriteTextFormat* fmt = nullptr;
    HRESULT hr = wf->CreateTextFormat(m_FontFamily.c_str(), nullptr,
                                      DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL,
                                      m_FontSize, L"", &fmt);
    if (FAILED(hr) || !fmt) return;

    // 大画布建 layout 拿文本 metrics（AutoSize 语义），再收口到实际尺寸。
    IDWriteTextLayout* layout = nullptr;
    hr = wf->CreateTextLayout(m_Text.c_str(), static_cast<UINT32>(m_Text.size()),
                              fmt, 10000.0f, 10000.0f, &layout);
    fmt->Release();
    if (FAILED(hr) || !layout) return;

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);
    layout->SetTextAlignment(m_AlignH);
    layout->SetMaxWidth(metrics.width  > 0.0f ? metrics.width  + 2.0f : 1.0f);
    layout->SetMaxHeight(metrics.height > 0.0f ? metrics.height + 2.0f : 1.0f);

    m_TextLayout = layout;
    m_W = static_cast<int>(metrics.width  + 0.5f);
    m_H = static_cast<int>(metrics.height + 0.5f);
}

void MeterString::Draw(ID2D1RenderTarget* rt)
{
    if (!rt || m_Text.empty()) return;
    if (!m_TextLayout) CreateLayout();
    if (!m_TextLayout) return;

    if (!m_Brush || m_BrushRT != rt) {
        if (m_Brush) { m_Brush->Release(); m_Brush = nullptr; }
        if (m_BrushRT) { m_BrushRT->Release(); m_BrushRT = nullptr; }
        if (FAILED(rt->CreateSolidColorBrush(m_Color, &m_Brush)) || !m_Brush) return;
        m_BrushRT = rt;
        m_BrushRT->AddRef();
    }

    rt->DrawTextLayout(D2D1::Point2F(static_cast<float>(m_X), static_cast<float>(m_Y)),
                       m_TextLayout, m_Brush);
}

}  // namespace raindock
