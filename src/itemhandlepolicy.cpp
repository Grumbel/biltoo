// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemhandlepolicy.h"

#include <QCoreApplication>

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

QString toolTip(ImageItem::Handle h)
{
    using H = ImageItem::Handle;
    switch (h) {
    case H::None:
        return {};
    case H::ScaleTopLeft:
    case H::ScaleTopRight:
    case H::ScaleBottomLeft:
    case H::ScaleBottomRight:
        return QCoreApplication::translate("ImageItem",
            "Scale (Shift: opposite edge; Ctrl: about centre)");
    case H::ScaleTop:
    case H::ScaleBottom:
        return QCoreApplication::translate("ImageItem", "Scale height");
    case H::ScaleLeft:
    case H::ScaleRight:
        return QCoreApplication::translate("ImageItem", "Scale width");
    case H::ShearTop:
    case H::ShearBottom:
    case H::ShearLeft:
    case H::ShearRight:
        return QCoreApplication::translate("ImageItem", "Shear");
    case H::RotateTop:
    case H::RotateRight:
    case H::RotateBottom:
    case H::RotateLeft:
        return QCoreApplication::translate("ImageItem", "Rotate");
    case H::FlipH:
        return QCoreApplication::translate("ImageItem", "Flip horizontal");
    case H::FlipV:
        return QCoreApplication::translate("ImageItem", "Flip vertical");
    case H::Rotate90CCW:
        return QCoreApplication::translate("ImageItem", "Rotate 90° counter-clockwise");
    case H::Rotate90CW:
        return QCoreApplication::translate("ImageItem", "Rotate 90° clockwise");
    case H::Raise:
        return QCoreApplication::translate("ImageItem", "Raise (bring forward)");
    case H::Lower:
        return QCoreApplication::translate("ImageItem", "Lower (send backward)");
    case H::ResetScale:
        return QCoreApplication::translate("ImageItem", "Reset scale to 1:1");
    case H::ResetRotation:
        return QCoreApplication::translate("ImageItem", "Reset rotation to 0°");
    case H::ResetShear:
        return QCoreApplication::translate("ImageItem", "Reset shear");
    case H::OpacitySlider:
        return QCoreApplication::translate("ImageItem", "Opacity");
    }
    return {};
}

} // namespace ItemHandlePolicy
