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
class SessionPathOrder
{
public:
    void clear()
    {
        m_paths.clear();
        m_ids.clear();
    }

    /** Replace order and align id vector length (invalid ids when growing). */
    void setOrder(const QStringList &newPaths, const QVector<SessionImageId> &newIds)
    {
        m_paths = newPaths;
        m_ids = newIds;
        syncIdLength();
    }

    /** Append one session row (path + optional id). */
    void appendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_paths.append(path);
        m_ids.append(id);
    }

    bool isEmpty() const { return m_paths.isEmpty(); }

    int size() const { return m_paths.size(); }

    const QStringList &pathList() const { return m_paths; }

    const QVector<SessionImageId> &idList() const { return m_ids; }

    /** Pad or trim ids so length matches paths (invalid ids when growing). */
    void syncIdLength()
    {
        while (m_ids.size() < m_paths.size()) {
            m_ids.append(kInvalidSessionImageId);
        }
        while (m_ids.size() > m_paths.size()) {
            m_ids.removeLast();
        }
    }

    int countPathOccurrences(const QString &path) const
    {
        int n = 0;
        for (const QString &p : m_paths) {
            if (p == path) {
                ++n;
            }
        }
        return n;
    }

    QString pathAt(int index) const
    {
        if (index < 0 || index >= m_paths.size()) {
            return {};
        }
        return m_paths.at(index);
    }

    SessionImageId idAt(int index) const
    {
        if (index < 0 || index >= m_ids.size()) {
            return kInvalidSessionImageId;
        }
        return m_ids.at(index);
    }

    /** First non-invalid id for @p path, or invalid if none. */
    SessionImageId firstIdForPath(const QString &path) const
    {
        if (path.isEmpty()) {
            return kInvalidSessionImageId;
        }
        const int n = qMin(m_paths.size(), m_ids.size());
        for (int i = 0; i < n; ++i) {
            if (m_paths.at(i) == path && m_ids.at(i) != kInvalidSessionImageId) {
                return m_ids.at(i);
            }
        }
        return kInvalidSessionImageId;
    }

private:
    QStringList m_paths;
    QVector<SessionImageId> m_ids;
};

#endif // SESSIONPATHORDER_H
