// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONPATHORDER_H
#define SESSIONPATHORDER_H

#include "imageview_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Session row order: paths with parallel SessionImageId slots (IDENTITY.md).
 * Gallery pack and LoadAdd bind use this sequence; content is still id-keyed.
 */
struct SessionPathOrder {
    QStringList paths;
    QVector<SessionImageId> ids;

    void clear()
    {
        paths.clear();
        ids.clear();
    }

    /** Pad or trim @p ids so it matches @p paths length (invalid ids when growing). */
    void syncIdLength()
    {
        while (ids.size() < paths.size()) {
            ids.append(kInvalidSessionImageId);
        }
        while (ids.size() > paths.size()) {
            ids.removeLast();
        }
    }

    int countPathOccurrences(const QString &path) const
    {
        int n = 0;
        for (const QString &p : paths) {
            if (p == path) {
                ++n;
            }
        }
        return n;
    }

    SessionImageId idAt(int index) const
    {
        if (index < 0 || index >= ids.size()) {
            return kInvalidSessionImageId;
        }
        return ids.at(index);
    }
};

#endif // SESSIONPATHORDER_H
