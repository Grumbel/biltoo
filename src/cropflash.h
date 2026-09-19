// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPFLASH_H
#define CROPFLASH_H

#include "cropsession.h"

#include <QCoreApplication>
#include <QString>

/**
 * Crop-mode HUD copy (titles/details). Translation context is "ImageView"
 * so existing .ts / tr catalogs stay valid. ImageView only calls flashHud.
 */
namespace CropFlash {

struct Hud {
    QString title;
    QString detail;
};

inline QString tr(const char *source)
{
    return QCoreApplication::translate("ImageView", source);
}

inline Hud modeEntered()
{
    return {tr("Crop mode"), tr("Apply commits · Esc cancels")};
}

inline Hud loadFailed()
{
    return {tr("Crop"), tr("Could not load full image")};
}

inline Hud loadingFull()
{
    return {tr("Crop"), tr("Loading full image…")};
}

inline Hud notCached()
{
    return {tr("Crop"), tr("Image not cached yet — try again")};
}

inline Hud bakeFailed()
{
    return {tr("Crop"), tr("Crop bake failed")};
}

inline Hud reset()
{
    return {tr("Crop reset"), tr("Full image")};
}

inline Hud applied(int width, int height)
{
    return {tr("Cropped"),
            QStringLiteral("%1×%2").arg(width).arg(height)};
}

inline Hud needSingleTarget()
{
    return {tr("Crop"), tr("Select a single image")};
}

inline Hud noImage()
{
    return {tr("Crop"), tr("No image")};
}

inline Hud fullReady()
{
    return {tr("Crop"), tr("Full image ready")};
}

inline Hud applyHostStatus(CropSession::ApplyHostStatus hostSt)
{
    const QString msg =
        (hostSt == CropSession::ApplyHostStatus::NeedFull)
            ? tr("Full image not ready — try again")
            : tr("No pixels to crop");
    return {tr("Crop"), msg};
}

inline QString undoCropText()
{
    return tr("Crop");
}

inline QString undoResetText()
{
    return tr("Crop reset");
}

} // namespace CropFlash

#endif // CROPFLASH_H
