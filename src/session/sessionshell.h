// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONSHELL_H
#define SESSIONSHELL_H

#include "session/sessionchrome.h"
#include "session/sessionbindbook.h"
#include "session/packorderoverlay.h"

/**
 * Session shell bags on ImageView: identity counters, LoadAdd bind queue,
 * and pack-order overlay. Decode/canvas placement stay on ImageView /
 * DisplayPipeline; this groups the session-wipe-adjacent state.
 */
class SessionShell
{
public:
    SessionIdentity &identity() { return m_identity; }
    const SessionIdentity &identity() const { return m_identity; }

    SessionBindBook &bindBook() { return m_bindBook; }
    const SessionBindBook &bindBook() const { return m_bindBook; }

    PackOrderOverlay &pathOrder() { return m_pathOrder; }
    const PackOrderOverlay &pathOrder() const { return m_pathOrder; }

    /** Clear bind queue + staged appearance (session wipe). */
    void clearBinds() { m_bindBook.clear(); }

    void clearIdentity() { m_identity.clear(); }

    void clearPathOrder() { m_pathOrder.clearExplicit(); }

private:
    SessionIdentity m_identity;
    SessionBindBook m_bindBook;
    PackOrderOverlay m_pathOrder;
};

#endif // SESSIONSHELL_H
