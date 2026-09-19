// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ATTENTIONCONTROLLER_H
#define ATTENTIONCONTROLLER_H

#include "attentionsession.h"

class ImageView;

/**
 * Attention-mode collaborator for ImageView (Phase 6 Tier 2a).
 *
 * Owns AttentionSession draft points. Orchestration remains on ImageView
 * until Tier 2b method extraction.
 */
class AttentionController
{
public:
    explicit AttentionController(ImageView *view);

    ImageView *view() const { return m_view; }

    AttentionSession &session() { return m_attention; }
    const AttentionSession &session() const { return m_attention; }

    bool active() const { return m_attention.active(); }

private:
    ImageView *m_view = nullptr; // not owned
    AttentionSession m_attention;
};

#endif // ATTENTIONCONTROLLER_H
