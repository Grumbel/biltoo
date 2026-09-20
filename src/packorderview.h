// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PACKORDERVIEW_H
#define PACKORDERVIEW_H

#include "imageview_types.h"
#include "sessiondocument.h"
#include "sessionpathorder.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Immutable snapshot of pack order (paths ∥ ids).
 *
 * Gallery pack / placeholders walk a PackOrderView instead of reaching into
 * SessionPathOrder or SessionDocument directly. ImageView fills the snapshot
 * via PackOrderOverlay::resolve (Explicit book, or FollowDocument when
 * collapsed and aligned).
 *
 * LoadAdd multiplicity: only fromBook() / Explicit overlay can express N tiles
 * for one session path. fromDocument() always has one row per membership.
 */

class PackOrderView
{
public:
    PackOrderView() = default;

    PackOrderView(QStringList paths, QVector<SessionImageId> ids)
        : m_paths(std::move(paths))
        , m_ids(std::move(ids))
    {
        align();
    }

    static PackOrderView fromBook(const SessionPathOrder &book)
    {
        return PackOrderView(book.pathList(), book.idList());
    }

    static PackOrderView fromDocument(const SessionDocument &doc)
    {
        return PackOrderView(doc.paths(), doc.ids());
    }

    bool isEmpty() const { return m_paths.isEmpty(); }
    int size() const { return m_paths.size(); }

    const QStringList &paths() const { return m_paths; }
    const QVector<SessionImageId> &ids() const { return m_ids; }

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

    /** True when paths and ids match the document membership (no LoadAdd extra). */
    bool alignsWithDocument(const SessionDocument &doc) const
    {
        return m_paths == doc.paths() && m_ids == doc.ids();
    }

    bool operator==(const PackOrderView &other) const
    {
        return m_paths == other.m_paths && m_ids == other.m_ids;
    }

private:
    void align()
    {
        while (m_ids.size() < m_paths.size()) {
            m_ids.append(kInvalidSessionImageId);
        }
        while (m_ids.size() > m_paths.size()) {
            m_ids.removeLast();
        }
    }

    QStringList m_paths;
    QVector<SessionImageId> m_ids;
};


/**
 * Pack-order *reads* (Tier 4 policy): ImageView::currentPackOrder() →
 * PackOrderOverlay::resolve(m_sessionDoc). SessionDocument membership alone
 * is not a pack drop-in (Explicit-empty / LoadAdd multiplicity). Identity
 * lookups may use the document; writes go through pathOrderClear / SetOrder /
 * AppendRow on the overlay. See docs/PATH_ORDER.md.
 */

#endif // PACKORDERVIEW_H
