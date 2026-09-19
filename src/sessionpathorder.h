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

    /** Replace order and align id vector length (invalid ids when growing). */
    void setOrder(const QStringList &newPaths, const QVector<SessionImageId> &newIds)
    {
        paths = newPaths;
        ids = newIds;
        syncIdLength();
    }

    /** Append one session row (path + optional id). */
    void appendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        paths.append(path);
        ids.append(id);
    }

    bool isEmpty() const { return paths.isEmpty(); }

    int size() const { return paths.size(); }

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

    QString pathAt(int index) const
    {
        if (index < 0 || index >= paths.size()) {
            return {};
        }
        return paths.at(index);
    }

    SessionImageId idAt(int index) const
    {
        if (index < 0 || index >= ids.size()) {
            return kInvalidSessionImageId;
        }
        return ids.at(index);
    }

    /** First non-invalid id for @p path, or invalid if none. */
    SessionImageId firstIdForPath(const QString &path) const
    {
        if (path.isEmpty()) {
            return kInvalidSessionImageId;
        }
        const int n = qMin(paths.size(), ids.size());
        for (int i = 0; i < n; ++i) {
            if (paths.at(i) == path && ids.at(i) != kInvalidSessionImageId) {
                return ids.at(i);
            }
        }
        return kInvalidSessionImageId;
    }
};

#endif // SESSIONPATHORDER_H
