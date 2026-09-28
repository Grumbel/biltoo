// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SPREADBOOK_H
#define SPREADBOOK_H

#include "session/spreadstate.h"
#include "session/sessiondocument.h"

/**
 * Session-facing spread membership (docs/SPREAD.md P0).
 * Does not own pixels; Image viewpoint projects this state.
 */
class SpreadBook
{
public:
    const SpreadState &state() const { return m_state; }
    SpreadState &state() { return m_state; }

    void clear() { m_state.clear(); }

    bool isActive() const { return m_state.isActive(); }

    /** Fixed-N window around @p anchorId using session order. */
    bool setFixedN(const SessionDocument &session, SessionImageId anchorId, int n = 2,
                   SpreadBindingHint binding = SpreadBindingHint::StrictPairs);

    /** Members = ordered selection (session order of @p orderedIds). */
    bool setFromSelection(const SessionDocument &session,
                          const QVector<SessionImageId> &orderedIds);

    /**
     * Rebuild members for current policy after anchor/session change.
     * @return true if membership changed.
     */
    bool rebuild(const SessionDocument &session);

    /** Paths for current members (empty string if id missing). */
    QStringList memberPaths(const SessionDocument &session) const;

private:
    SpreadState m_state;
};

#endif // SPREADBOOK_H
