// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ATTENTIONSESSION_H
#define ATTENTIONSESSION_H

#include "imageview_types.h"
#include "viewtransform.h"

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

    bool isDragging() const { return dragging; }

    bool isRubberbanding() const { return rubberbanding; }

    bool isGestureActive() const { return gestureActive; }

    bool isDraftValid() const { return draftValid; }

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

    void setDraft(const QVector<QPointF> &pts, SessionImageId sid)
    {
        draftPts = pts;
        draftValid = true;
        draftSessionId = sid;
    }

    /** @return true when mode flag changed (interaction cleared on change). */
    bool setMode(bool on)
    {
        if (mode == on) {
            return false;
        }
        mode = on;
        clearInteraction();
        return true;
    }

    void enterMode()
    {
        setMode(true);
    }

    void leaveMode()
    {
        setMode(false);
        // Draft may be kept for re-entry; callers clear explicitly if needed.
    }

    void setSelected(const QVector<int> &sel) { selected = sel; }

    bool hasSelection() const { return !selected.isEmpty(); }

    void clearSelected() { selected.clear(); }

    /**
     * Start dragging selected points from @p origin (viewport).
     * @p startPts snapshot of all draft points; @p beforePts for undo (may differ
     * when inserting a point).
     */
    void beginPointDrag(const QPoint &origin, const QVector<QPointF> &startPts,
                        const QVector<QPointF> &beforePts)
    {
        rubberbanding = false;
        dragging = !selected.isEmpty();
        dragOriginView = origin;
        dragStartPts = startPts;
        gestureBefore = beforePts;
        gestureActive = dragging;
    }

    void endPointDrag()
    {
        dragging = false;
        // gestureActive cleared by commit path after undo is recorded.
    }

    void clearGesture()
    {
        gestureActive = false;
        gestureBefore.clear();
    }

    void clear()
    {
        mode = false;
        clearInteraction();
        clearDraft();
    }

    /**
 * Start rubber-band multi-select at @p origin (viewport).
 * @p clearSelection when false keeps current selected (Shift additive).
 */
    void beginRubber(const QPoint &origin, bool clearSelection = true)
    {
        rubberbanding = true;
        dragging = false;
        gestureActive = false;
        rubberOrigin = origin;
        rubberRect = QRect(origin, QSize());
        if (clearSelection) {
            selected.clear();
        }
    }

    /** Update rubber rect from origin to @p pos (viewport). */
    void updateRubber(const QPoint &pos)
    {
        rubberRect = ViewTransform::rubberRect(rubberOrigin, pos);
    }

    /** End rubber-band; keep selected indices. */
    void endRubber()
    {
        rubberbanding = false;
        rubberOrigin = {};
        rubberRect = {};
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
