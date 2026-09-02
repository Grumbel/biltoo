// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessiondocument.h"

#include <QDebug>
#include <QSet>

QString SessionDocument::pathAt(int index) const
{
    if (index < 0 || index >= m_paths.size()) {
        return {};
    }
    return m_paths.at(index);
}

SessionImageId SessionDocument::idAt(int index) const
{
    if (index < 0 || index >= m_ids.size()) {
        return kInvalidSessionImageId;
    }
    return m_ids.at(index);
}

int SessionDocument::indexOfId(SessionImageId id) const
{
    if (id == kInvalidSessionImageId) {
        return -1;
    }
    for (int i = 0; i < m_ids.size(); ++i) {
        if (m_ids.at(i) == id) {
            return i;
        }
    }
    return -1;
}

int SessionDocument::indexOfPath(const QString &path) const
{
    return m_paths.indexOf(path);
}

int SessionDocument::lastIndexOfPath(const QString &path) const
{
    return m_paths.lastIndexOf(path);
}

SessionImageId SessionDocument::allocId()
{
    return m_nextId++;
}

void SessionDocument::clear()
{
    m_paths.clear();
    m_ids.clear();
    // Do not reset m_nextId — removed ids must never be recycled (IDENTITY.md).
}

void SessionDocument::setPaths(const QStringList &paths)
{
    m_paths = paths;
    m_ids.clear();
    m_ids.reserve(m_paths.size());
    for (int i = 0; i < m_paths.size(); ++i) {
        m_ids.append(allocId());
    }
}

void SessionDocument::replaceAll(const QStringList &paths, const QVector<SessionImageId> &ids)
{
    Q_ASSERT(paths.size() == ids.size());
    m_paths = paths;
    m_ids = ids;
    // Advance next id past any retained ids so we never reuse.
    for (SessionImageId id : m_ids) {
        if (id >= m_nextId) {
            m_nextId = id + 1;
        }
    }
    // Dedup in place: later slots with a repeated id get a fresh id.
    QSet<SessionImageId> seen;
    for (int i = 0; i < m_ids.size(); ++i) {
        SessionImageId id = m_ids.at(i);
        if (id == kInvalidSessionImageId) {
            m_ids[i] = allocId();
            continue;
        }
        if (seen.contains(id)) {
            qCritical("SessionDocument::replaceAll: duplicate SessionImageId %lld at %d — reallocating",
                      static_cast<long long>(id), i);
            m_ids[i] = allocId();
            id = m_ids.at(i);
        }
        seen.insert(id);
    }
}

void SessionDocument::append(const QString &path, SessionImageId id)
{
    m_paths.append(path);
    if (id != kInvalidSessionImageId) {
        // Never allow the same SessionImageId twice in one session list.
        if (indexOfId(id) >= 0) {
            qCritical("SessionDocument::append: refusing duplicate SessionImageId %lld for %s — allocating fresh id",
                      static_cast<long long>(id), qPrintable(path));
            id = allocId();
        }
        m_ids.append(id);
        if (id >= m_nextId) {
            m_nextId = id + 1;
        }
    } else {
        m_ids.append(allocId());
    }
}

void SessionDocument::insert(int index, const QString &path, SessionImageId id)
{
    if (index < 0 || index > m_paths.size()) {
        append(path, id);
        return;
    }
    m_paths.insert(index, path);
    if (id != kInvalidSessionImageId) {
        if (indexOfId(id) >= 0) {
            qCritical("SessionDocument::insert: refusing duplicate SessionImageId %lld for %s — allocating fresh id",
                      static_cast<long long>(id), qPrintable(path));
            id = allocId();
        }
        m_ids.insert(index, id);
        if (id >= m_nextId) {
            m_nextId = id + 1;
        }
    } else {
        m_ids.insert(index, allocId());
    }
}

void SessionDocument::removeAt(int index)
{
    if (index < 0 || index >= m_paths.size()) {
        return;
    }
    m_paths.removeAt(index);
    if (index < m_ids.size()) {
        m_ids.removeAt(index);
    }
}

void SessionDocument::ensureIdsAligned()
{
    while (m_ids.size() < m_paths.size()) {
        m_ids.append(allocId());
    }
    while (m_ids.size() > m_paths.size()) {
        m_ids.removeLast();
    }
}

bool SessionDocument::validateUniqueIds(const char *context) const
{
    QSet<SessionImageId> seen;
    bool ok = true;
    for (int i = 0; i < m_ids.size(); ++i) {
        const SessionImageId id = m_ids.at(i);
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (seen.contains(id)) {
            qCritical("SessionDocument: duplicate SessionImageId %lld at index %d (%s)",
                      static_cast<long long>(id), i,
                      context ? context : "validateUniqueIds");
            ok = false;
        } else {
            seen.insert(id);
        }
    }
    if (m_ids.size() != m_paths.size()) {
        qCritical("SessionDocument: ids/paths size mismatch %d vs %d (%s)",
                  m_ids.size(), m_paths.size(),
                  context ? context : "validateUniqueIds");
        ok = false;
    }
    return ok;
}
