// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Interactive colour grade + deferred commit (was section of imageview_appearance).

#include "imageview.h"
#include <QString>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include "biltoo_thread.h"
#include <QTimer>
#include <QImage>
#include "viewtransform.h"
#include "itemcomponents.h"
#include "crop/cropsession.h"
#include "display/imagecache.h"
#include "contentxform.h"
#include "session/sessionappearance.h"
#include "thumtoocache.h"
#include "imageitem.h"
#include "imageloader.h"

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

    const QString path = item->path();
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull() && item->hasDecodedPixels() && !itemHasAppliedContentXform(item)) {
        host = item->sourceImage();
    }
    if (host.isNull()) {
        syncLiveColorFromState(item, want.colorAdjust);
        item->update();
        return;
    }
    // materializeDisplay asserts NOT GUI when long edge > kGuiMaterializeMaxEdge.
    // Interactive path must stay ≤ that limit (full bake is async on commit).
    const int kInteractiveGradeMaxEdge = ContentXform::kGuiMaterializeMaxEdge;
    if (ImageCache::longEdge(host) > kInteractiveGradeMaxEdge) {
        host = ImageCache::clampToMaxEdge(host, kInteractiveGradeMaxEdge);
    }
    const auto kind = SessionAppearance::PixelKind::SoftPreview;
    const QImage display = SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        syncLiveColorFromState(item, want.colorAdjust);
        return;
    }
    if (item->hasDecodedPixels()
        && ImageCache::longEdge(item->sourceImage()) > kInteractiveGradeMaxEdge) {
        // Soft stand-in for the drag; keep session id / path on the item.
        item->clearDecodedPixels();
    }
    attachDisplaySample(item, display, want, kind);
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
    rematerializeItemContent(item, want);
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

