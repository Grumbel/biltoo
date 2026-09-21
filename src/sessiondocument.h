// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONDOCUMENT_H
#define SESSIONDOCUMENT_H

#include "imageview_types.h"
#include "sessionseedbook.h"

#include <QString>
#include <QList>
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
    /** True when @p id is a non-invalid row in the session list. */
    bool hasId(SessionImageId id) const { return indexOfId(id) >= 0; }
    int indexOfPath(const QString &path) const;
    /** Last index of @p path (duplicate-safe). -1 if absent. */
    int lastIndexOfPath(const QString &path) const;

    /** Count path occurrences (duplicate-safe identity). */
    int countPathOccurrences(const QString &path) const;
    /** First non-invalid id for @p path, or invalid if none. */
    SessionImageId firstIdForPath(const QString &path) const;

    /**
     * List index for @p path preferring SessionImageId over pure path match.
     * Uses firstIdForPath then indexOfId; falls back to indexOfPath when the
     * path has no bound id (fully unbound row). -1 if absent.
     */
    int indexOfPathPreferId(const QString &path) const;

    /**
     * Index of the @p occurrence-th match of @p path (0 = first). -1 if fewer
     * than occurrence+1 rows share that path. Duplicate-safe membership helper.
     */
    int indexOfPathOccurrence(const QString &path, int occurrence) const;

    /**
     * Map each path in @p paths to successive session occurrences (IDENTITY:
     * same path twice selects the first then second row). Skips empty paths and
     * omits duplicate indices. Order follows @p paths.
     */
    QList<int> indicesForPathsByOccurrence(const QStringList &paths) const;

    /**
     * List indices for each valid id in @p ids (skips invalid / missing).
     * Order follows @p ids; duplicate ids yield duplicate indices.
     */
    QList<int> indicesForIds(const QVector<SessionImageId> &ids) const;

    /** Never reuses an id after remove. */
    SessionImageId allocId();

    /** Clear paths/ids only (does not clear seed book or recycle ids). */
    void clearPaths();
    void clear();
    /**
     * Replace list; allocates a fresh id for every path.
     * Clears the seed book (orphaned prior ids). Use replaceAll to reorder
     * while keeping SessionImageId identity; ItemWorld sparse rows are the
     * caller's responsibility on full Open/Replace.
     */
    void setPaths(const QStringList &paths);
    /**
     * Replace list with parallel paths and ids (same size required).
     * Used for sort / reorder while preserving SessionImageId identity.
     */
    void replaceAll(const QStringList &paths, const QVector<SessionImageId> &ids);
    void append(const QString &path, SessionImageId id = kInvalidSessionImageId);
    void insert(int index, const QString &path, SessionImageId id = kInvalidSessionImageId);
    /** Remove path/id at index; also drops seed flag for that id. */
    void removeAt(int index);
    /** Pad or trim ids to match paths (legacy recovery only). */
    void ensureIdsAligned();
    /** Return false and log if any SessionImageId appears more than once. */
    bool validateUniqueIds(const char *context = nullptr) const;

    /**
     * Seed-attempt book for the session (Stage 4b residual).
     * ImageView binds via bindSessionSeedBook → hostSeedBook().
     * Content appearance is ItemWorld sparse tables; clear() only resets seeds.
     */
    SessionSeedBook &seedBook() { return m_seedBook; }
    const SessionSeedBook &seedBook() const { return m_seedBook; }

private:
    QStringList m_paths;
    QVector<SessionImageId> m_ids;
    SessionImageId m_nextId = 1;
    SessionSeedBook m_seedBook;
};


#endif // SESSIONDOCUMENT_H
