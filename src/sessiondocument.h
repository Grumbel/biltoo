// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONDOCUMENT_H
#define SESSIONDOCUMENT_H

#include "imageview_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Ordered session image list (DOMAIN / IDENTITY).
 * Paths may repeat; identity is SessionImageId, never the path string.
 *
 * MainWindow owns one SessionDocument as the working set. Canvas views
 * (ImageView, ThumbnailBar) observe paths/ids — they do not own the list.
 */
class SessionDocument
{
public:
    int size() const { return m_paths.size(); }
    bool isEmpty() const { return m_paths.isEmpty(); }

    const QStringList &paths() const { return m_paths; }
    /** Mutable access for bulk assign / sort in-place during migration. */
    QStringList &paths() { return m_paths; }

    const QVector<SessionImageId> &ids() const { return m_ids; }
    QVector<SessionImageId> &ids() { return m_ids; }

    QString pathAt(int index) const;
    SessionImageId idAt(int index) const;
    int indexOfId(SessionImageId id) const;
    int indexOfPath(const QString &path) const;

    /** Never reuses an id after remove. */
    SessionImageId allocId();

    void clear();
    void setPaths(const QStringList &paths);
    void append(const QString &path, SessionImageId id = kInvalidSessionImageId);
    void insert(int index, const QString &path, SessionImageId id = kInvalidSessionImageId);
    void removeAt(int index);
    /** Keep ids aligned when paths are reordered (e.g. sort). */
    void ensureIdsAligned();

private:
    QStringList m_paths;
    QVector<SessionImageId> m_ids;
    SessionImageId m_nextId = 1;
};

#endif // SESSIONDOCUMENT_H
