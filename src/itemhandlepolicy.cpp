// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemhandlepolicy.h"

namespace ItemHandlePolicy {

bool isChromeHandle(ImageItem::Handle h)
{
    return h == ImageItem::Handle::FlipH || h == ImageItem::Handle::FlipV
        || h == ImageItem::Handle::Rotate90CCW || h == ImageItem::Handle::Rotate90CW
        || h == ImageItem::Handle::Raise || h == ImageItem::Handle::Lower
        || h == ImageItem::Handle::ResetScale || h == ImageItem::Handle::ResetRotation
        || h == ImageItem::Handle::ResetShear
        || h == ImageItem::Handle::OpacitySlider;
}

bool isRotateHandle(ImageItem::Handle h)
{
    return h == ImageItem::Handle::RotateTop || h == ImageItem::Handle::RotateRight
        || h == ImageItem::Handle::RotateBottom || h == ImageItem::Handle::RotateLeft;
}

bool isCornerScaleHandle(ImageItem::Handle h)
{
    return h == ImageItem::Handle::ScaleTopLeft || h == ImageItem::Handle::ScaleTopRight
        || h == ImageItem::Handle::ScaleBottomLeft || h == ImageItem::Handle::ScaleBottomRight;
}

bool isEdgeScaleHandle(ImageItem::Handle h)
{
    return h == ImageItem::Handle::ScaleTop || h == ImageItem::Handle::ScaleRight
        || h == ImageItem::Handle::ScaleBottom || h == ImageItem::Handle::ScaleLeft;
}

bool isScaleHandle(ImageItem::Handle h)
{
    return isCornerScaleHandle(h) || isEdgeScaleHandle(h);
}

bool isShearHandle(ImageItem::Handle h)
{
    return h == ImageItem::Handle::ShearTop || h == ImageItem::Handle::ShearBottom
        || h == ImageItem::Handle::ShearLeft || h == ImageItem::Handle::ShearRight;
}

bool isUprightChromeHandle(ImageItem::Handle h)
{
    // Raise/Lower glyphs stay screen-upright so "up" always means raise.
    return h == ImageItem::Handle::Raise || h == ImageItem::Handle::Lower;
}

} // namespace ItemHandlePolicy
