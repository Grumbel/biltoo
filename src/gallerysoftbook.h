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

    /** Mutable soft state for @p path (creates empty entry if missing). */
    GallerySoftState &state(const QString &path) { return soft[path]; }

    const GallerySoftState *get(const QString &path) const
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = soft.constFind(path);
        if (it == soft.cend()) {
            return nullptr;
        }
        return &(*it);
    }

    GallerySoftState *find(const QString &path)
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = soft.find(path);
        if (it == soft.end()) {
            return nullptr;
        }
        return &(*it);
    }

    /** @return true when defer-populate flag changed. */
    bool setDeferPopulate(bool on)
    {
        if (deferPopulate == on) {
            return false;
        }
        deferPopulate = on;
        return true;
    }

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
