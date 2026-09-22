// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYDECODEBOOK_H
#define GALLERYDECODEBOOK_H

#include "imageview_types.h"

#include <QHash>
#include <QSet>
#include <QString>

/**
 * Per-path Gallery decode-window state and related path sets.
 * Decode watchdog QTimer stays on ImageView.
 */
class GalleryDecodeBook
{
public:
    bool deferPopulate = false;

    bool isDeferPopulate() const { return deferPopulate; }

    void clearDecodeStates() { m_byPath.clear(); }

    /** Mutable decode state for @p path (creates empty entry if missing). */
    GalleryDecodeState &state(const QString &path) { return m_byPath[path]; }

    const GalleryDecodeState *get(const QString &path) const
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = m_byPath.constFind(path);
        if (it == m_byPath.cend()) {
            return nullptr;
        }
        return &(*it);
    }

    GalleryDecodeState *find(const QString &path)
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = m_byPath.find(path);
        if (it == m_byPath.end()) {
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
            m_byPath.remove(path);
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
        m_byPath.clear();
        deferPopulate = false;
        m_imageModeNativeDecodePaths.clear();
    }

private:
    QHash<QString, GalleryDecodeState> m_byPath;
    QSet<QString> m_imageModeNativeDecodePaths;
};

#endif // GALLERYDECODEBOOK_H
