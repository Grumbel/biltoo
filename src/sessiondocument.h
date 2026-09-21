// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONDOCUMENT_H
#define SESSIONDOCUMENT_H

#include "imageview_types.h"
#include "sessionappearance.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Ordered session image list (DOMAIN / IDENTITY).
 * Paths may repeat; identity is SessionImageId, never the path string.
 *
 * MainWindow owns one SessionDocument as the working set. Canvas views
 * (ImageView, ThumbnailBar) observe paths/ids — they do not own the list.
 *
 * Mutations go through these methods only so path/id lengths stay aligned
 * and ids are never reused after remove.
 */
class SessionDocument
{
public:
    int size() const { return m_paths.size(); }
    bool isEmpty() const { return m_paths.isEmpty(); }

    const QStringList &paths() const { return m_paths; }
    const QVector<SessionImageId> &ids() const { return m_ids; }

    QString pathAt(int index) const;
    SessionImageId idAt(int index) const;
    int indexOfId(SessionImageId id) const;
    int indexOfPath(const QString &path) const;
    /** Last index of @p path (duplicate-safe). -1 if absent. */
    int lastIndexOfPath(const QString &path) const;

    /** Count path occurrences (duplicate-safe identity). */
    int countPathOccurrences(const QString &path) const;
    /** First non-invalid id for @p path, or invalid if none. */
    SessionImageId firstIdForPath(const QString &path) const;

    /** Never reuses an id after remove. */
    SessionImageId allocId();

    /** Clear paths/ids only (does not clear appearance or recycle ids). */
    void clearPaths();
    void clear();
    /**
     * Replace list; allocates a fresh id for every path.
     * Clears the appearance store (orphaned prior ids). Use replaceAll to
     * reorder while keeping SessionImageId identity and appearance rows.
     */
    void setPaths(const QStringList &paths);
    /**
     * Replace list with parallel paths and ids (same size required).
     * Used for sort / reorder while preserving SessionImageId identity.
     */
    void replaceAll(const QStringList &paths, const QVector<SessionImageId> &ids);
    void append(const QString &path, SessionImageId id = kInvalidSessionImageId);
    void insert(int index, const QString &path, SessionImageId id = kInvalidSessionImageId);
    /** Remove path/id at index; also drops fat appearance for that id. */
    void removeAt(int index);
    /** Pad or trim ids to match paths (legacy recovery only). */
    void ensureIdsAligned();
    /** Return false and log if any SessionImageId appears more than once. */
    bool validateUniqueIds(const char *context = nullptr) const;

    /**
     * Phase 6 Tier 4: sole SessionAppearanceStore for the session.
     * ImageView binds via bindSessionAppearance; no view-owned store remains.
     * clear() wipes this fat store only — ItemWorld sparse tables are cleared
     * by ImageView::clearWorkspace / clearAppearance.
     */
    SessionAppearanceStore &appearance() { return m_appearance; }
    const SessionAppearanceStore &appearance() const { return m_appearance; }

private:
    QStringList m_paths;
    QVector<SessionImageId> m_ids;
    SessionImageId m_nextId = 1;
    SessionAppearanceStore m_appearance;
};


#endif // SESSIONDOCUMENT_H
