/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/MeterImage.h（图像渲染，直接复用）。
 * 依赖：WIC（图像解码）+ Direct2D 位图。
 */
#ifndef RAINDOCK_LIBRARY_METER_IMAGE_H_
#define RAINDOCK_LIBRARY_METER_IMAGE_H_

#include "Meter.h"

#include <string>

struct ID2D1Bitmap;

namespace raindock {

class MeterImage : public Meter
{
public:
    MeterImage(Skin* skin, const WCHAR* name) : Meter(skin, name) {}

    UINT GetTypeID() override { return TypeID<MeterImage>(); }

    void Initialize(ConfigParser& parser, Measure* measure) override;
    void Draw(ID2D1RenderTarget* rt) override;

private:
    std::wstring   m_ImagePath;
    ID2D1Bitmap*   m_Bitmap = nullptr;   // 解码后的 D2D 位图
    bool           m_PreserveAspectRatio = true;
    D2D1_RECT_F    m_DrawRect = {0,0,0,0};
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_METER_IMAGE_H_
