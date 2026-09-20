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

#endif // PACKORDERVIEW_H
