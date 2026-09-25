// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Interactive colour grade + deferred durable commit (owned by ImageController).

#include "image/imagecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "display/displaypipelinecontroller.h"

#include <QTimer>
#include <QObject>
#include <QImage>
#include <QUndoStack>

namespace {

struct ContentUndoMacro {
    QUndoStack *stack = nullptr;
    explicit ContentUndoMacro(QUndoStack *s, const QString &text, int targetCount)
        : stack(s && targetCount > 1 ? s : nullptr)
    {
        if (stack) {
            stack->beginMacro(text);
        }
    }
    ~ContentUndoMacro()
    {
        if (stack) {
            stack->endMacro();
        }
    }
    ContentUndoMacro(const ContentUndoMacro &) = delete;
    ContentUndoMacro &operator=(const ContentUndoMacro &) = delete;
};

} // namespace

void ImageController::ensureColorAdjustCommitTimer()
{
    if (m_colorAdjustCommitTimer) {
        return;
    }
    m_colorAdjustCommitTimer = new QTimer(m_view);
    m_colorAdjustCommitTimer->setSingleShot(true);
    m_colorAdjustCommitTimer->setInterval(ColorAdjustCommit::kIntervalMs);
    QObject::connect(m_colorAdjustCommitTimer, &QTimer::timeout, m_view, [this]() {
        flushColorAdjustCommit();
    });
}

void ImageController::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommit.schedule(sid, path);
    ensureColorAdjustCommitTimer();
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        flushColorAdjustCommit();
    }
}

void ImageController::stopColorAdjustCommitTimer()
{
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->stop();
    }
}

void ImageController::applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Prefer pure live grade when the tile still holds unbaked host pixels
    // (no orient/crop/grade bake). Avoids materializeDisplay on the GUI.
    const bool contentGeom = want.hasCrop || want.contentHFlip || want.contentVFlip
        || want.contentQuarterTurns != 0;
    if (!contentGeom && item->hasDecodedPixels() && !m_view->itemHasAppliedContentXform(item)) {
        m_view->syncLiveColorFromState(item, want.colorAdjust, true);
        const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = m_view->hostSessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit m_view->sessionAppearanceChanged(sid, item->path(), appearance);
            }
        }
        return;
    }

    // SoftPreview install for content-baked tiles while dragging — pipeline owns pixels.
    if (!m_view->hostDisplayPipeline().installInteractiveSoftPreview(item, want)) {
        m_view->syncLiveColorFromState(item, want.colorAdjust);
        item->update();
        return;
    }
    // Filmstrip / Gallery chrome: push soft appearance while dragging so the
    // strip does not wait for the idle commit (and does not require FullSource).
    const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = m_view->hostSessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit m_view->sessionAppearanceChanged(sid, item->path(), appearance);
        }
    }
}

void ImageController::flushColorAdjustCommit()
{
    SessionImageId sid = kInvalidSessionImageId;
    QString path;
    if (!m_colorAdjustCommit.take(&sid, &path)) {
        return;
    }
    ImageItem *item = (sid != kInvalidSessionImageId)
        ? m_view->findItemBySessionId(sid)
        : nullptr;
    if (!item && m_view->isImageMode() && !m_view->liveItems().isEmpty()) {
        item = m_view->liveItems().first();
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; put the commit back so idle debounce retries
    // after leaveCrop (take already cleared the bag).
    if (m_view->hostCrop().isCropDraftLockedItem(item)) {
        scheduleColorAdjustCommit(sid, path.isEmpty() ? item->path() : path);
        return;
    }
    // Stage 2: freeze policy (store + live when durable; else captureState).
    WorkspaceItemState want = m_view->freezeItemAppearance(item);
    // Flush always prefers live grade (interaction authority; ItemWorld Color
    // is already updated on setTargetColorAdjustments).
    want.colorAdjust = m_view->itemLiveColor(item);
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into path-keyed XDG on every slider tick — ItemWorld Color + project
    // own durable grade; XDG is for orient/flip seed, not slider spam.
    m_view->hostDisplayPipeline().rematerializeItemContent(item, want);
    // Gallery: same session id may be stashed while Image mode edits — the
    // live tile update covers Image/Gallery focus; filmstrip uses the emit.
    const QImage appearance = m_view->hostSessionAppearanceImage(item);
    if (!appearance.isNull()) {
        const SessionImageId emitSid = sid != kInvalidSessionImageId
            ? sid
            : item->sessionId();
        if (emitSid != kInvalidSessionImageId) {
            emit m_view->sessionAppearanceChanged(
                emitSid,
                item->path().isEmpty() ? path : item->path(),
                appearance);
        }
    }
}

void ImageController::installColorAdjustmentsOnItem(ImageItem *item, const ColorAdjustments &adj)
{
    if (!item) {
        return;
    }
    const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
    WorkspaceItemState slot = m_view->freezeItemAppearance(item);
    slot.sessionId = (sid != kInvalidSessionImageId) ? sid : slot.sessionId;
    slot.path = item->path().isEmpty() ? slot.path : item->path();
    slot.colorAdjust = adj;
    if (sid != kInvalidSessionImageId) {
        ItemComponents::Color c;
        c.grade = adj;
        m_view->itemWorld().setColor(sid, c);
    }
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
}

void ImageController::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    ImageItem *item = m_view->targetItem();
    if (!item && m_view->isImageMode() && !m_view->liveItems().isEmpty()) {
        item = m_view->liveItems().first();
    }
    if (!item) {
        return;
    }
    // Slider path: primary target only (batch uses applyColorAdjustmentsToTargets).
    installColorAdjustmentsOnItem(item, adj);
    emit m_view->statusChanged();
}

int ImageController::applyColorAdjustmentsToTargets(const ColorAdjustments &adj)
{
    QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        ImageItem *item = m_view->targetItem();
        if (!item && m_view->isImageMode() && !m_view->liveItems().isEmpty()) {
            item = m_view->liveItems().first();
        }
        if (item) {
            targets.append(item);
        }
    }
    if (targets.isEmpty()) {
        return 0;
    }

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Colour grade (%1)").arg(targets.size()),
        targets.size());

    int n = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QImage beforeSrc = item->sourceImage().copy();
        WorkspaceItemState beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(beforeSt, item->placement());
        beforeSt.colorAdjust = m_view->itemLiveColor(item);

        installColorAdjustmentsOnItem(item, adj);

        WorkspaceItemState afterSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSt, item->placement());
        afterSt.colorAdjust = adj;
        afterSt.sessionId = beforeSt.sessionId;
        m_view->pushItemContentCommand(
            m_view->tr("Colour grade"), item, beforeSrc,
            item->sourceImage().copy(), beforeSt, afterSt);
        ++n;
    }
    if (n > 0) {
        emit m_view->statusChanged();
    }
    return n;
}
