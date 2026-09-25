// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONSEARCHINDEX_H
#define SESSIONSEARCHINDEX_H

#include "imageview_types.h"

#include <QHash>
#include <QString>
#include <QVector>

/**
 * Ephemeral Find tags keyed by SessionImageId (and path fallback).
 *
 * Not an ItemWorld component: search hits are query-scoped UI state, not durable
 * appearance. Filmstrip / gallery read this; DocumentSearch writes it.
 *
 * Generation: writers bump or stamp @c generation so stale worker results cannot
 * replace a newer query's tags.
 */
class SessionSearchIndex
{
public:
    struct Hit {
        SessionImageId id = kInvalidSessionImageId;
        QString path;
        quint16 matchCount = 0;
    };

    quint64 generation() const { return m_generation; }
    const QString &query() const { return m_query; }
    bool isEmpty() const { return m_byId.isEmpty() && m_byPath.isEmpty(); }
    int taggedCount() const
    {
        // Prefer id tags; path-only fills gaps for unbound rows.
        return qMax(m_byId.size(), m_byPath.size());
    }

    void clear()
    {
        ++m_generation;
        m_query.clear();
        m_byId.clear();
        m_byPath.clear();
        m_orderedIds.clear();
    }

    /** Start a new query; clears tags and stamps generation. */
    quint64 beginQuery(const QString &query)
    {
        clear();
        m_query = query.trimmed();
        return m_generation;
    }

    /**
     * Replace tags if @p generation matches. Builds ordered id list in the order
     * hits are supplied (typically session / document scan order).
     */
    bool commit(quint64 generation, const QString &query, const QVector<Hit> &hits)
    {
        if (generation != m_generation) {
            return false;
        }
        m_query = query.trimmed();
        m_byId.clear();
        m_byPath.clear();
        m_orderedIds.clear();
        m_orderedIds.reserve(hits.size());
        for (const Hit &h : hits) {
            if (h.matchCount == 0) {
                continue;
            }
            if (h.id != kInvalidSessionImageId) {
                const quint16 prev = m_byId.value(h.id, 0);
                m_byId.insert(h.id, quint16(qMin(65535, int(prev) + int(h.matchCount))));
                if (!m_orderedIds.contains(h.id)) {
                    m_orderedIds.append(h.id);
                }
            }
            if (!h.path.isEmpty()) {
                const quint16 prev = m_byPath.value(h.path, 0);
                m_byPath.insert(h.path, quint16(qMin(65535, int(prev) + int(h.matchCount))));
            }
        }
        return true;
    }

    /** Tag one row (page-local Find) without clearing other hits. */
    void addOrUpdate(SessionImageId id, const QString &path, quint16 matchCount)
    {
        if (matchCount == 0) {
            if (id != kInvalidSessionImageId) {
                m_byId.remove(id);
                m_orderedIds.removeAll(id);
            }
            if (!path.isEmpty()) {
                m_byPath.remove(path);
            }
            return;
        }
        if (id != kInvalidSessionImageId) {
            m_byId.insert(id, matchCount);
            if (!m_orderedIds.contains(id)) {
                m_orderedIds.append(id);
            }
        }
        if (!path.isEmpty()) {
            m_byPath.insert(path, matchCount);
        }
    }

    quint16 matchCount(SessionImageId id, const QString &path = QString()) const
    {
        if (id != kInvalidSessionImageId) {
            const auto it = m_byId.constFind(id);
            if (it != m_byId.cend()) {
                return it.value();
            }
        }
        if (!path.isEmpty()) {
            return m_byPath.value(path, 0);
        }
        return 0;
    }

    bool hasHit(SessionImageId id, const QString &path = QString()) const
    {
        return matchCount(id, path) > 0;
    }

    /** Session ids in first-seen hit order (for next/previous). */
    const QVector<SessionImageId> &orderedIds() const { return m_orderedIds; }

private:
    quint64 m_generation = 0;
    QString m_query;
    QHash<SessionImageId, quint16> m_byId;
    QHash<QString, quint16> m_byPath;
    QVector<SessionImageId> m_orderedIds;
};

#endif // SESSIONSEARCHINDEX_H
