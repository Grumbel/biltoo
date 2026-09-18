// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo_process_memos.h"

namespace ThumtooCache {

ProcessMemos &ProcessMemos::instance()
{
    static ProcessMemos s;
    return s;
}

QSize ProcessMemos::size(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    std::lock_guard lock(m_mu);
    const auto it = m_size.constFind(path);
    if (it == m_size.cend()) {
        return {};
    }
    return it.value();
}

void ProcessMemos::noteSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || size.width() <= 0 || size.height() <= 0) {
        return;
    }
    std::lock_guard lock(m_mu);
    m_size.insert(path, size);
}

bool ProcessMemos::durableYes(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    std::lock_guard lock(m_mu);
    return m_durableYes.contains(path);
}

int ProcessMemos::durableMinScale(const QString &path) const
{
    if (path.isEmpty()) {
        return 0;
    }
    std::lock_guard lock(m_mu);
    return m_durableMinScale.value(path, 0);
}

void ProcessMemos::noteDurableYes(const QString &path, int minScale)
{
    if (path.isEmpty()) {
        return;
    }
    std::lock_guard lock(m_mu);
    m_durableYes.insert(path);
    m_durableMinScale.insert(path, minScale);
    m_durableNoUntilMs.remove(path);
}

bool ProcessMemos::durableNoActive(const QString &path, qint64 nowMs) const
{
    if (path.isEmpty()) {
        return false;
    }
    std::lock_guard lock(m_mu);
    const auto it = m_durableNoUntilMs.constFind(path);
    if (it == m_durableNoUntilMs.cend()) {
        return false;
    }
    return nowMs < it.value();
}

void ProcessMemos::noteDurableNo(const QString &path, qint64 untilMs)
{
    if (path.isEmpty()) {
        return;
    }
    std::lock_guard lock(m_mu);
    m_durableNoUntilMs.insert(path, untilMs);
}

void ProcessMemos::clearDurableNo(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    std::lock_guard lock(m_mu);
    m_durableNoUntilMs.remove(path);
}

void ProcessMemos::clearSessionReplaceDurable()
{
    std::lock_guard lock(m_mu);
    m_durableYes.clear();
    m_durableMinScale.clear();
    m_durableNoUntilMs.clear();
}

} // namespace ThumtooCache
