// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONSEEDBOOK_H
#define SESSIONSEEDBOOK_H

#include "imageview_types.h"

#include <QSet>

/**
 * Per-session seed-attempt book (Stage 4b residual).
 *
 * Tracks "XDG/path seed already attempted" so paint paths do not re-hit
 * locatorId every frame. Content appearance lives in ItemWorld sparse tables;
 * project/clipboard assemble via appearanceValue — not this book.
 */
class SessionSeedBook
{
public:
    void clear();
    int size() const { return m_seedAttempted.size(); }
    bool isEmpty() const { return m_seedAttempted.isEmpty(); }

    bool seedAttempted(SessionImageId id) const;
    void markSeedAttempted(SessionImageId id);
    void clearSeedAttempted(SessionImageId id);
    /** Drop seed flag for @p id (session row remove). */
    void remove(SessionImageId id);

private:
    QSet<SessionImageId> m_seedAttempted;
};

#endif // SESSIONSEEDBOOK_H
