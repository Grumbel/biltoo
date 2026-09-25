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
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "item/imagesizebook.h"
#include "gallery/gallerycontroller.h"

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

QImage sampleForAutocrop(ImageItem *item, const QString &path)
{
    QImage src = CropSession::pickAutoCropSourcePixels(item);
    if (!src.isNull()) {
        return src;
    }
    return ImageCache::get(path);
}

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

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Crop (%1)").arg(targets.size()),
        targets.size());

    int n = 0;
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
            sample = sampleForAutocrop(item, item->path());
            if (sample.isNull()) {
                continue;
            }
        }

        const QRect crop = CropRecipeUtil::computeCropRect(recipe, logical, sample);
        if (!CropRecipeUtil::isUsableCrop(crop, logical)) {
            continue;
        }

        const QImage beforeSrc = item->sourceImage().copy();
        WorkspaceItemState beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(beforeSt, item->placement());

        WorkspaceItemState afterSt = m_view->freezeItemAppearance(item);
        ItemComponents::applyPlacementToState(afterSt, item->placement());
        afterSt.hasCrop = true;
        afterSt.cropRect = crop;
        afterSt.cropSourceSize = logical;
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = m_view->hostResolveContentEditSessionId(item);
        afterSt.path = item->path();

        applyCropAppearance(item, QImage(), afterSt);
        m_view->hostDisplayPipeline().rematerializeItemContent(item, afterSt);

        WorkspaceItemState afterSnap = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSnap, item->placement());
        afterSnap.hasCrop = true;
        afterSnap.cropRect = crop;
        afterSnap.cropSourceSize = logical;
        afterSnap.sessionId = afterSt.sessionId;

        m_view->pushItemContentCommand(
            m_view->tr("Crop"), item, beforeSrc,
            item->sourceImage().copy(), beforeSt, afterSnap);
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

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Reset crop (%1)").arg(targets.size()),
        targets.size());

    int n = 0;
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

        const QImage beforeSrc = item->sourceImage().copy();
        WorkspaceItemState afterSt = beforeSt;
        afterSt.hasCrop = false;
        afterSt.cropRect = QRect();
        afterSt.cropSourceSize = QSize();
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = sid;
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
            m_view->tr("Reset crop"), item, beforeSrc,
            item->sourceImage().copy(), beforeSt, afterSnap);
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
