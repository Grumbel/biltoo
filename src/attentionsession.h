// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ATTENTIONSESSION_H
#define ATTENTIONSESSION_H

#include "imageview_types.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

/**
 * Attention-point edit session draft and interaction state.
 *
 * Points are normalized in content space; draft is bound to one SessionImageId
 * for the gesture lifetime (IDENTITY). Enter/detect/undo orchestration stays
 * on ImageView.
 */
class AttentionSession
{
public:
    bool active() const { return mode; }

    /** True when mode is on and draft points are bound to @p sid. */
    bool hasDraftFor(SessionImageId sid) const
    {
        return mode && draftValid
            && sid != kInvalidSessionImageId
            && sid == draftSessionId
            && !draftPts.isEmpty();
    }

    void clearInteraction()
    {
        dragging = false;
        rubberbanding = false;
        rubberOrigin = {};
        rubberRect = {};
        selected.clear();
        dragStartPts.clear();
        dragOriginView = {};
        gestureActive = false;
        gestureBefore.clear();
    }

    void clearDraft()
    {
        draftValid = false;
        draftPts.clear();
        draftSessionId = kInvalidSessionImageId;
    }

    void clear()
    {
        mode = false;
        clearInteraction();
        clearDraft();
    }

    bool mode = false;
    bool dragging = false;
    bool rubberbanding = false;
    QPoint rubberOrigin;
    QRect rubberRect;
    QVector<int> selected;
    QVector<QPointF> dragStartPts;
    QPoint dragOriginView;
    QVector<QPointF> gestureBefore;
    bool gestureActive = false;
    bool draftValid = false;
    QVector<QPointF> draftPts;
    SessionImageId draftSessionId = kInvalidSessionImageId;
};

#endif // ATTENTIONSESSION_H
