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
 * Tier 4 prep: Gallery pack / placeholders walk a PackOrderView instead of
 * reaching into SessionPathOrder or SessionDocument directly. Until the
 * ImageView harness lands, the view is still filled from the path-order book;
 * when multiplicities match the document, fromDocument() is valid.
 *
 * LoadAdd multiplicity: only fromBook() can express N tiles for one session
 * path. fromDocument() always has one row per session membership.
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
 * Where pack-order *reads* are allowed to come from (Tier 4 policy).
 *
 * - ViewBook: ImageView::m_pathOrderBook — LoadAdd multiplicity, ad-hoc place,
 *   Gallery stash. Default for all pack / LoadAdd / size-resolve readers today.
 * - SessionDocument: MainWindow session membership (one row per open file).
 *   Safe only when the book aligns with the document (no extra multiplicity /
 *   invalid ids). Prefer for identity lookups; not a drop-in for pack order.
 *
 * Writes always go to the view book (pathOrderClear / SetOrder / AppendRow).
 * SessionDocument is mutated only by MainWindow session APIs.
 */
enum class PackOrderReadSource {
    ViewBook,
    SessionDocument,
};

/**
 * Resolve a pack-order snapshot under @p source.
 * SessionDocument source requires a non-null @p doc; otherwise falls back to book.
 */
inline PackOrderView packOrderForRead(PackOrderReadSource source,
                                      const SessionPathOrder &book,
                                      const SessionDocument *doc)
{
    if (source == PackOrderReadSource::SessionDocument && doc) {
        return PackOrderView::fromDocument(*doc);
    }
    return PackOrderView::fromBook(book);
}

#endif // PACKORDERVIEW_H
