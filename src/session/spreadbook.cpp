// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/spreadbook.h"

bool SpreadBook::setFixedN(const SessionDocument &session, SessionImageId anchorId, int n,
                           SpreadBindingHint binding)
{
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {
        n = 8; // docs/SPREAD.md v1 hard cap
    }
    m_state.policy = SpreadMembershipPolicy::FixedN;
    m_state.fixedN = n;
    m_state.binding = binding;
    m_state.anchor = anchorId;
    m_state.stride = SpreadStride::BySpread;
    return rebuild(session);
}

bool SpreadBook::setFromSelection(const SessionDocument &session,
                                  const QVector<SessionImageId> &orderedIds)
{
    m_state.policy = SpreadMembershipPolicy::Selection;
    m_state.members.clear();
    constexpr int kMaxSpreadMembers = 8; // docs/SPREAD.md v1 hard cap
    for (SessionImageId id : orderedIds) {
        if (m_state.members.size() >= kMaxSpreadMembers) {
            break;
        }
        if (session.hasId(id) && !m_state.members.contains(id)) {
            m_state.members.append(id);
        }
    }
    if (m_state.members.isEmpty()) {
        m_state.clear();
        return false;
    }
    m_state.anchor = m_state.members.first();
    m_state.fixedN = m_state.members.size();
    return true;
}

bool SpreadBook::rebuild(const SessionDocument &session)
{
    if (m_state.policy == SpreadMembershipPolicy::Off) {
        m_state.members.clear();
        return false;
    }
    if (m_state.policy == SpreadMembershipPolicy::Selection
        || m_state.policy == SpreadMembershipPolicy::Explicit) {
        // Keep explicit/selection list; drop ids no longer in session.
        QVector<SessionImageId> kept;
        for (SessionImageId id : m_state.members) {
            if (session.hasId(id)) {
                kept.append(id);
            }
        }
        const bool changed = kept != m_state.members;
        m_state.members = kept;
        if (m_state.members.isEmpty()) {
            m_state.clear();
            return true;
        }
        if (!session.hasId(m_state.anchor)) {
            m_state.anchor = m_state.members.first();
        }
        return changed;
    }
    // FixedN
    int anchorIndex = session.indexOfId(m_state.anchor);
    if (anchorIndex < 0 && !session.isEmpty()) {
        anchorIndex = 0;
        m_state.anchor = session.idAt(0);
    }
    if (anchorIndex < 0) {
        m_state.members.clear();
        return false;
    }
    const QVector<SessionImageId> next =
        buildFixedNMembers(session.ids(), anchorIndex, m_state.fixedN, m_state.binding);
    const bool changed = next != m_state.members;
    m_state.members = next;
    if (!m_state.members.isEmpty() && !m_state.members.contains(m_state.anchor)) {
        m_state.anchor = m_state.members.first();
    }
    return changed;
}

QStringList SpreadBook::memberPaths(const SessionDocument &session) const
{
    QStringList paths;
    paths.reserve(m_state.members.size());
    for (SessionImageId id : m_state.members) {
        const int idx = session.indexOfId(id);
        paths.append(idx >= 0 ? session.pathAt(idx) : QString());
    }
    return paths;
}
