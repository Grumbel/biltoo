// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Interactive colour-grade apply and commit.

#include "imageview.h"
#include "itemcomponents.h"
#include "imageitem.h"
#include "contentxform.h"
#include "thumtoocache.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "viewtransform.h"

#include <QImage>
#include <QTimer>
#include "biltoo_thread.h"
#include <QPointer>
#include <QThreadPool>
#include <QMetaObject>

void ImageView::applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Prefer pure live grade when the tile still holds unbaked host pixels
    // (no orient/crop/grade bake). Avoids materializeDisplay on the GUI.
    const bool contentGeom = want.hasCrop || want.contentHFlip || want.contentVFlip
        || want.contentQuarterTurns != 0;
    if (!contentGeom && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        item->setColorAdjustments(want.colorAdjust);
        const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
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
    if (host.isNull() && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        host = item->sourceImage();
    }
    if (host.isNull()) {
        item->setColorAdjustmentsRecord(want.colorAdjust);
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
        item->setColorAdjustmentsRecord(want.colorAdjust);
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
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
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
    ImageItem *item = nullptr;
    if (sid != kInvalidSessionImageId) {
        for (ImageItem *it : m_items) {
            if (it && it->sessionId() == sid) {
                item = it;
                break;
            }
        }
    }
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    WorkspaceItemState want;
    if (sid != kInvalidSessionImageId && m_itemWorld.hasAppearance(sid)) {
        want = m_itemWorld.appearanceValue(sid);
    } else if (item) {
        want = captureState(item);
        want.colorAdjust = item->colorAdjustments();
    } else {
        return;
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; colour commit waits until crop exits.
    if (m_cropCtrl.isCropDraftLockedItem(item)) {
        return;
    }
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into thumtoo durable appearance — SessionAppearanceStore / project
    // already own it; path cache is for orient/crop hints, not slider spam.
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
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    WorkspaceItemState slot = (sid != kInvalidSessionImageId && m_itemWorld.hasAppearance(sid))
        ? m_itemWorld.appearanceValue(sid)
        : captureState(item);
    slot.sessionId = (sid != kInvalidSessionImageId) ? sid : slot.sessionId;
    slot.path = item->path().isEmpty() ? slot.path : item->path();
    slot.colorAdjust = adj;
    if (sid != kInvalidSessionImageId) {
        // Component write (dual-writes DTO color fields); then ensure path/id.
        ItemComponents::Color c;
        c.grade = adj;
        m_itemWorld.setColor(sid, c);
        if (const WorkspaceItemState *cur = m_itemWorld.getAppearance(sid)) {
            slot = *cur;
            if (slot.path.isEmpty() || slot.sessionId == kInvalidSessionImageId) {
                slot.sessionId = sid;
                slot.path = item->path();
                slot.colorAdjust = adj;
                m_itemWorld.setAppearance(sid, slot);
            }
        }
    }
    // Fast path while dragging: bake from clamped host (no SQLite / filmstrip).
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
    emit statusChanged();
}

