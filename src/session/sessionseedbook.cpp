// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessionseedbook.h"

bool SessionSeedBook::seedAttempted(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_seedAttempted.contains(id);
}

void SessionSeedBook::markSeedAttempted(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_seedAttempted.insert(id);
    }
}

void SessionSeedBook::clearSeedAttempted(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_seedAttempted.remove(id);
    }
}

void SessionSeedBook::remove(SessionImageId id)
{
    clearSeedAttempted(id);
}

void SessionSeedBook::clear()
{
    m_seedAttempted.clear();
}
