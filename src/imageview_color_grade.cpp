// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Interactive colour grade + deferred commit (was section of imageview_appearance).

#include "imageview.h"
#include <QString>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include "util/biltoo_thread.h"
#include <QTimer>
#include <QImage>
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "imageitem.h"

// --- Colour grade (was imageview_color_grade.cpp) ---

void ImageView::applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Prefer pure live grade when the tile still holds unbaked host pixels
    // (no orient/crop/grade bake). Avoids materializeDisplay on the GUI.
    const bool contentGeom = want.hasCrop || want.contentHFlip || want.contentVFlip
        || want.contentQuarterTurns != 0;
    if (!contentGeom && item->hasDecodedPixels() && !itemHasAppliedContentXform(item)) {
        syncLiveColorFromState(item, want.colorAdjust, true);
        const SessionImageId sid = resolveContentEditSessionId(item);
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, item->path(), appearance);
            }
        }
        return;
    }

    // SoftPreview install for content-baked tiles while dragging — pipeline owns pixels.
    if (!m_displayPipeline.installInteractiveSoftPreview(item, want)) {
        syncLiveColorFromState(item, want.colorAdjust);
        item->update();
        return;
    }
    // Filmstrip / Gallery chrome: push soft appearance while dragging so the
    // strip does not wait for the idle commit (and does not require FullSource).
    const SessionImageId sid = resolveContentEditSessionId(item);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearance);
        }
    }
}


void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommit.schedule(sid, path);
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        flushColorAdjustCommit();
    }
}


void ImageView::flushColorAdjustCommit()
{
    SessionImageId sid = kInvalidSessionImageId;
    QString path;
    if (!m_colorAdjustCommit.take(&sid, &path)) {
        return;
    }
    ImageItem *item = (sid != kInvalidSessionImageId)
        ? findItemBySessionId(sid)
        : nullptr;
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; put the commit back so idle debounce retries
    // after leaveCrop (take already cleared the bag).
    if (m_cropCtrl.isCropDraftLockedItem(item)) {
        scheduleColorAdjustCommit(sid, path.isEmpty() ? item->path() : path);
        return;
    }
    // Stage 2: freeze policy (store + live when durable; else captureState).
    WorkspaceItemState want = freezeItemAppearance(item);
    // Flush always prefers live grade (interaction authority; ItemWorld Color
    // is already updated on setTargetColorAdjustments).
    want.colorAdjust = itemLiveColor(item);
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into path-keyed XDG on every slider tick — ItemWorld Color + project
    // own durable grade; XDG is for orient/flip seed, not slider spam.
    m_displayPipeline.rematerializeItemContent(item, want);
    // Gallery: same session id may be stashed while Image mode edits — the
    // live tile update covers Image/Gallery focus; filmstrip uses the emit.
    const QImage appearance = sessionAppearanceImage(item);
    if (!appearance.isNull()) {
        const SessionImageId emitSid = sid != kInvalidSessionImageId
            ? sid
            : item->sessionId();
        if (emitSid != kInvalidSessionImageId) {
            emit sessionAppearanceChanged(emitSid,
                                          item->path().isEmpty() ? path : item->path(),
                                          appearance);
        }
    }
}


void ImageView::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    ImageItem *item = targetItem();
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return;
    }
    const SessionImageId sid = resolveContentEditSessionId(item);
    // Stage 2: freeze policy for grade slot seed.
    WorkspaceItemState slot = freezeItemAppearance(item);
    slot.sessionId = (sid != kInvalidSessionImageId) ? sid : slot.sessionId;
    slot.path = item->path().isEmpty() ? slot.path : item->path();
    slot.colorAdjust = adj;
    if (sid != kInvalidSessionImageId) {
        // ItemWorld Color is persistence authority (sparse-only). Live grade is
        // installed below via applyInteractiveColorGrade → syncLiveColorFromState.
        ItemComponents::Color c;
        c.grade = adj;
        m_itemWorld.setColor(sid, c);
    }
    // Fast path while dragging: live grade + optional host bake (no SQLite).
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
    emit statusChanged();
}

