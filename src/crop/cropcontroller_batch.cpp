// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Panel / multi-select crop apply (non-interactive; no crop-mode draft).

#include "crop/cropcontroller.h"
#include "crop/croprecipe.h"

#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "display/displaypipelinecontroller.h"
#include "host/thumtoocache.h"
#include "item/imagesizebook.h"
#include "gallery/gallerycontroller.h"

#include <QPointer>
#include <QPixmap>
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

QList<ImageItem *> cropBatchTargets(ImageView *view)
{
    QList<ImageItem *> targets = view->transformTargets();
    if (targets.isEmpty()) {
        ImageItem *item = view->targetItem();
        if (!item && view->isImageMode() && !view->liveItems().isEmpty()) {
            item = view->liveItems().first();
        }
        if (item) {
            targets.append(item);
        }
    }
    return targets;
}

QSize logicalSizeForCrop(ImageView *view, ImageItem *item)
{
    if (!view || !item) {
        return {};
    }
    const QString path = item->path();
    const SessionImageId sid = view->hostResolveContentEditSessionId(item);

    if (sid != kInvalidSessionImageId && view->itemWorld().hasCrop(sid)) {
        const ItemComponents::Crop c = view->itemWorld().crop(sid);
        if (c.sourceSize.isValid() && c.sourceSize.width() > 0 && c.sourceSize.height() > 0) {
            return c.sourceSize;
        }
    }
    {
        WorkspaceItemState st = view->freezeItemAppearance(item);
        if (st.cropSourceSize.isValid() && st.cropSourceSize.width() > 0
            && st.cropSourceSize.height() > 0) {
            return st.cropSourceSize;
        }
    }

    QSize native = ThumtooCache::cachedSize(path);
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        native = view->hostSizeBook().known(path);
    }
    if (SessionAppearance::isUsableNativeSize(native)) {
        return native;
    }

    if (!view->itemHasAppliedContentXform(item)
        || !view->itemAppliedContentXform(item).hasCrop) {
        const QSize isz = item->imageSize();
        if (isz.width() > 1 && isz.height() > 1) {
            return isz;
        }
    }
    return {};
}

/**
 * Pixels for autocrop: live item only (deep copy). Do not touch ImageCache here —
 * concurrent put/get while iterating a large selection has shown QHash SEGV in
 * qHash(path) under load; item pixels are sufficient for GIMP-style trim.
 */
QImage sampleForAutocrop(ImageItem *item)
{
    if (!item) {
        return {};
    }
    QImage src = item->sourceImage();
    if (src.isNull()) {
        const QPixmap pm = item->pixmap();
        if (!pm.isNull()) {
            src = pm.toImage();
        }
    }
    if (src.isNull()) {
        return {};
    }
    // Detach from any shared buffers before autoTrim walks pixels.
    return src.copy();
}

struct PlannedCrop {
    QPointer<ImageItem> item;
    QString path;
    SessionImageId sid = kInvalidSessionImageId;
    QSize logical;
    QRect crop;
    WorkspaceItemState beforeSt;
    QImage beforeSrc;
};

} // namespace

int CropController::applyCropRecipeToTargets(const CropPanelRecipe &recipe)
{
    return applyCropRecipeToItems(recipe, cropBatchTargets(m_view));
}

int CropController::applyCropRecipeToItems(const CropPanelRecipe &recipe,
                                           const QList<ImageItem *> &targets)
{
    if (!m_view || targets.isEmpty()) {
        return 0;
    }

    // Phase 1: pure plan — no ItemWorld / pixel mutation (avoids destroying or
    // reshuffling live items while we still hold raw ImageItem*).
    QList<PlannedCrop> plan;
    plan.reserve(targets.size());
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QSize logical = logicalSizeForCrop(m_view, item);
        if (logical.width() < 1 || logical.height() < 1) {
            continue;
        }

        QImage sample;
        if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
            sample = sampleForAutocrop(item);
            if (sample.isNull()) {
                continue;
            }
        }

        const QRect crop = CropRecipeUtil::computeCropRect(recipe, logical, sample);
        if (!CropRecipeUtil::isUsableCrop(crop, logical)) {
            continue;
        }

        PlannedCrop p;
        p.item = item;
        p.path = item->path();
        p.sid = m_view->hostResolveContentEditSessionId(item);
        p.logical = logical;
        p.crop = crop;
        p.beforeSrc = item->sourceImage().copy();
        p.beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(p.beforeSt, item->placement());
        plan.append(p);
    }

    if (plan.isEmpty()) {
        return 0;
    }

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Crop (%1)").arg(plan.size()),
        plan.size());

    int n = 0;
    for (PlannedCrop &p : plan) {
        ImageItem *item = p.item.data();
        if (!item) {
            continue;
        }

        WorkspaceItemState afterSt = m_view->freezeItemAppearance(item);
        ItemComponents::applyPlacementToState(afterSt, item->placement());
        afterSt.hasCrop = true;
        afterSt.cropRect = p.crop;
        afterSt.cropSourceSize = p.logical;
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = p.sid != kInvalidSessionImageId
            ? p.sid
            : m_view->hostResolveContentEditSessionId(item);
        afterSt.path = p.path.isEmpty() ? item->path() : p.path;

        // Store + rematerialize once; undo push redo re-applies the same state.
        applyCropAppearance(item, QImage(), afterSt);
        m_view->hostDisplayPipeline().rematerializeItemContent(item, afterSt);

        WorkspaceItemState afterSnap = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSnap, item->placement());
        afterSnap.hasCrop = true;
        afterSnap.cropRect = p.crop;
        afterSnap.cropSourceSize = p.logical;
        afterSnap.sessionId = afterSt.sessionId;

        m_view->pushItemContentCommand(
            m_view->tr("Crop"), item, p.beforeSrc,
            item->sourceImage().copy(), p.beforeSt, afterSnap);
        ++n;
    }

    if (n > 0 && m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    if (n > 0) {
        emit m_view->statusChanged();
    }
    return n;
}

int CropController::resetCropOnTargets()
{
    return resetCropOnItems(cropBatchTargets(m_view));
}

int CropController::resetCropOnItems(const QList<ImageItem *> &targets)
{
    if (!m_view || targets.isEmpty()) {
        return 0;
    }

    struct PlannedReset {
        QPointer<ImageItem> item;
        SessionImageId sid = kInvalidSessionImageId;
        WorkspaceItemState beforeSt;
        QImage beforeSrc;
    };

    QList<PlannedReset> plan;
    plan.reserve(targets.size());
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        WorkspaceItemState beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(beforeSt, item->placement());
        const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
        if (!beforeSt.hasCrop && beforeSt.cropRect.isEmpty()) {
            if (sid == kInvalidSessionImageId || !m_view->itemWorld().hasCrop(sid)) {
                continue;
            }
        }
        PlannedReset p;
        p.item = item;
        p.sid = sid;
        p.beforeSt = beforeSt;
        p.beforeSrc = item->sourceImage().copy();
        plan.append(p);
    }

    if (plan.isEmpty()) {
        return 0;
    }

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Reset crop (%1)").arg(plan.size()),
        plan.size());

    int n = 0;
    for (PlannedReset &p : plan) {
        ImageItem *item = p.item.data();
        if (!item) {
            continue;
        }

        WorkspaceItemState afterSt = p.beforeSt;
        afterSt.hasCrop = false;
        afterSt.cropRect = QRect();
        afterSt.cropSourceSize = QSize();
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = p.sid;
        afterSt.path = item->path();

        applyCropAppearance(item, QImage(), afterSt);
        m_view->hostDisplayPipeline().rematerializeItemContent(item, afterSt);

        WorkspaceItemState afterSnap = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSnap, item->placement());
        afterSnap.hasCrop = false;
        afterSnap.cropRect = QRect();
        afterSnap.cropSourceSize = QSize();
        afterSnap.sessionId = afterSt.sessionId;

        m_view->pushItemContentCommand(
            m_view->tr("Reset crop"), item, p.beforeSrc,
            item->sourceImage().copy(), p.beforeSt, afterSnap);
        ++n;
    }

    if (n > 0 && m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    if (n > 0) {
        emit m_view->statusChanged();
    }
    return n;
}
