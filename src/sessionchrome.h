// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONCHROME_H
#define SESSIONCHROME_H

#include "imageview_types.h"

#include <QString>

/**
 * Active session identity and view-mode flags shown in the HUD / status bar.
 * ViewMode is ImageView::ViewMode — stored as int-compatible enum value via
 * the host; this bag holds the identity counters only plus nav flags that
 * travel with the session.
 *
 * Note: viewMode lives on ImageView still when a full enum type is needed
 * without including ImageView here. Use SessionNavFlags for the booleans and
 * SessionIdentity for index/id.
 */
struct SessionIdentity {
    int index = -1;
    int total = 0;
    SessionImageId currentId = kInvalidSessionImageId;
    QString lastLoadError;

    void clear()
    {
        index = -1;
        total = 0;
        currentId = kInvalidSessionImageId;
        lastLoadError.clear();
    }

    void setPosition(int idx, int tot)
    {
        index = idx;
        total = tot;
    }

    void setCurrentId(SessionImageId id) { currentId = id; }

    void setLastLoadError(const QString &err) { lastLoadError = err; }

    void clearLastLoadError() { lastLoadError.clear(); }
};

struct SessionNavFlags {
    bool imageModeNavEnabled = false;
    bool galleryReturnAvailable = false;
};

/**
 * 1-based session index badge ("i/n") or empty when index/total invalid.
 * Pure string form; callers translate via tr if needed.
 */
inline QString sessionBadgeAscii(int index, int total)
{
    if (total > 0 && index >= 0 && index < total) {
        return QString::number(index + 1) + QLatin1Char('/') + QString::number(total);
    }
    return {};
}

#endif // SESSIONCHROME_H
