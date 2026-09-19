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
class GallerySoftBook
{
public:
    bool deferPopulate = false;

    bool isDeferPopulate() const { return deferPopulate; }

    void clearSoft() { m_soft.clear(); }

    /** Mutable soft state for @p path (creates empty entry if missing). */
    GallerySoftState &state(const QString &path) { return m_soft[path]; }

    const GallerySoftState *get(const QString &path) const
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = m_soft.constFind(path);
        if (it == m_soft.cend()) {
            return nullptr;
        }
        return &(*it);
    }

    GallerySoftState *find(const QString &path)
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = m_soft.find(path);
        if (it == m_soft.end()) {
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
            m_soft.remove(path);
        }
    }

    bool hasImageModeNativeDecode(const QString &path) const
    {
        return !path.isEmpty() && m_imageModeNativeDecodePaths.contains(path);
    }

    void markImageModeNativeDecode(const QString &path)
    {
        if (!path.isEmpty()) {
            m_imageModeNativeDecodePaths.insert(path);
        }
    }

    void clearImageModeNativeDecode() { m_imageModeNativeDecodePaths.clear(); }

    void clear()
    {
        m_soft.clear();
        deferPopulate = false;
        m_imageModeNativeDecodePaths.clear();
    }

private:
    QHash<QString, GallerySoftState> m_soft;
    QSet<QString> m_imageModeNativeDecodePaths;
};

#endif // GALLERYSOFTBOOK_H
