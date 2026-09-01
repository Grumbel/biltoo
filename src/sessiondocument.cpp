// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessiondocument.h"

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

SessionImageId SessionDocument::allocId()
{
    return m_nextId++;
}

void SessionDocument::clear()
{
    m_paths.clear();
    m_ids.clear();
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

void SessionDocument::append(const QString &path, SessionImageId id)
{
    m_paths.append(path);
    m_ids.append(id != kInvalidSessionImageId ? id : allocId());
}

void SessionDocument::insert(int index, const QString &path, SessionImageId id)
{
    if (index < 0 || index > m_paths.size()) {
        append(path, id);
        return;
    }
    m_paths.insert(index, path);
    m_ids.insert(index, id != kInvalidSessionImageId ? id : allocId());
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
