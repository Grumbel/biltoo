// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYSOFTBOOK_H
#define GALLERYSOFTBOOK_H

#include "imageview_types.h"

#include <QHash>
#include <QSet>
#include <QString>

/**
 * Per-path Gallery soft/decode-window state and related path sets.
 * Soft watchdog QTimer stays on ImageView.
 */
struct GallerySoftBook {
    QHash<QString, GallerySoftState> soft;
    bool deferPopulate = false;
    QSet<QString> imageModeNativeDecodePaths;

    void clearSoft() { soft.clear(); }

    void setDeferPopulate(bool on) { deferPopulate = on; }

    void resetPath(const QString &path)
    {
        if (!path.isEmpty()) {
            soft.remove(path);
        }
    }

    bool hasImageModeNativeDecode(const QString &path) const
    {
        return !path.isEmpty() && imageModeNativeDecodePaths.contains(path);
    }

    void markImageModeNativeDecode(const QString &path)
    {
        if (!path.isEmpty()) {
            imageModeNativeDecodePaths.insert(path);
        }
    }

    void clearImageModeNativeDecode() { imageModeNativeDecodePaths.clear(); }

    void clear()
    {
        soft.clear();
        deferPopulate = false;
        imageModeNativeDecodePaths.clear();
    }
};

#endif // GALLERYSOFTBOOK_H
