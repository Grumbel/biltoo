// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "item/itemhandlepolicy.h"

#include <QCoreApplication>

namespace ItemHandlePolicy {

bool isChromeHandle(ItemHandle h)
{
    return h == ItemHandle::FlipH || h == ItemHandle::FlipV
        || h == ItemHandle::Rotate90CCW || h == ItemHandle::Rotate90CW
        || h == ItemHandle::Raise || h == ItemHandle::Lower
        || h == ItemHandle::ResetScale || h == ItemHandle::ResetRotation
        || h == ItemHandle::ResetShear
        || h == ItemHandle::OpacitySlider;
}

bool isRotateHandle(ItemHandle h)
{
    return h == ItemHandle::RotateTop || h == ItemHandle::RotateRight
        || h == ItemHandle::RotateBottom || h == ItemHandle::RotateLeft;
}

bool isCornerScaleHandle(ItemHandle h)
{
    return h == ItemHandle::ScaleTopLeft || h == ItemHandle::ScaleTopRight
        || h == ItemHandle::ScaleBottomLeft || h == ItemHandle::ScaleBottomRight;
}

bool isEdgeScaleHandle(ItemHandle h)
{
    return h == ItemHandle::ScaleTop || h == ItemHandle::ScaleRight
        || h == ItemHandle::ScaleBottom || h == ItemHandle::ScaleLeft;
}

bool isScaleHandle(ItemHandle h)
{
    return isCornerScaleHandle(h) || isEdgeScaleHandle(h);
}

bool isShearHandle(ItemHandle h)
{
    return h == ItemHandle::ShearTop || h == ItemHandle::ShearBottom
        || h == ItemHandle::ShearLeft || h == ItemHandle::ShearRight;
}

bool isUprightChromeHandle(ItemHandle h)
{
    // Raise/Lower glyphs stay screen-upright so "up" always means raise.
    return h == ItemHandle::Raise || h == ItemHandle::Lower;
}

QString toolTip(ItemHandle h)
{
    using H = ItemHandle;
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
