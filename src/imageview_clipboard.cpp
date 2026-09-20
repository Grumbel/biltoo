// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas selection, transform targets, and workspace clipboard.

#include "imageview.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "sessionappearance.h"

#include <QSet>
#include <QUndoStack>
#include <QGraphicsItem>
#include "imagecache.h"
#include "contentxform.h"

void ImageView::duplicateSelected()
{
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return;
    }
    QList<ImageItem *> sources;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        return;
    }

    m_scene->clearSelection();
    for (ImageItem *src : sources) {
        // Display-ready copy of current pixels — never createItemFromImage with
        // sourceImage/preview: that ImageCache::put's baked samples as host.
        QImage display = src->sourceImage();
        SessionAppearance::PixelKind kind = SessionAppearance::PixelKind::FullSource;
        if (display.isNull()) {
            display = src->previewImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            display = src->displayImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            continue;
        }

        WorkspaceItemState content;
        if (src->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_itemWorld.getAppearance(src->sessionId())) {
                content = *app;
            }
        }
        content.path = src->path();
        content.hasCrop = src->sessionHasCrop();
        content.cropRect = src->sessionCropRect();
        content.contentHFlip = src->contentHFlip();
        content.contentVFlip = src->contentVFlip();
        content.colorAdjust = src->colorAdjustments();
        if (src->hasAppliedContentXform()
            && !SessionAppearance::hasContentAppearance(content)) {
            ContentXform::Value x = src->appliedContentXform();
            x.applyToState(content);
        }

        QSize intrinsic = src->imageSize();
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            const QSize native = layoutSizeForPath(src->path(), QImage());
            intrinsic = ContentXform::layoutSize(native, content);
        }
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            // Do not adopt sample pixel size (LQIP/soft).
            intrinsic = QSize(1, 1);
        }

        auto *copy = new ImageItem(src->path(), intrinsic);
        applyItemModeFlags(copy);
        m_scene->addItem(copy);
        m_items.append(copy);
        // Attach already-baked display; do not put into ImageCache.
        attachDisplaySample(copy, display, content, kind);
        m_pendingAppearance.insert(copy, content);
        if (isWorkspaceMode()) {
            ItemComponents::Placement pl = src->placement();
            pl.pos += QPointF(40.0, 40.0); // visible beside the original
            pl.z = src->stackZ() + 0.01;
            copy->applyPlacement(pl);
        } else {
            // Gallery: upright tile; MainWindow packs after binding session ids.
            ItemComponents::Placement pl;
            pl.pos = src->pos();
            copy->applyPlacement(pl);
        }
        copy->setSelected(true);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}


QList<WorkspaceItemState> ImageView::captureSelectedWorkspaceClipboard() const
{
    QList<WorkspaceItemState> out;
    if (!isWorkspaceMode()) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        WorkspaceItemState s = captureState(item);
        s.path = item->path();
        s.sessionId = item->sessionId();
        // Prefer store for content meta not fully on the item (cropRotation, …).
        if (item->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_itemWorld.getAppearance(item->sessionId())) {
                s.cropRotation = app->cropRotation;
                s.cropSourceSize = app->cropSourceSize;
                s.contentQuarterTurns = app->contentQuarterTurns;
                if (s.colorAdjust.isIdentity() && !app->colorAdjust.isIdentity()) {
                    s.colorAdjust = app->colorAdjust;
                }
            }
        }
        out.append(s);
    }
    return out;
}


void ImageView::removeSelectedCanvasItems()
{
    if (!isWorkspaceMode() || !m_scene) {
        return;
    }
    QList<ImageItem *> toRemove;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (item && m_items.contains(item)) {
            toRemove.append(item);
        }
    }
    if (toRemove.isEmpty()) {
        return;
    }
    setUpdatesEnabled(false);
    m_scene->blockSignals(true);
    for (ImageItem *item : toRemove) {
        // Persist pose + content so filmstrip membership toggle can restore pose.
        rememberItemState(item);
        destroyCanvasItem(item);
    }
    m_scene->blockSignals(false);
    setUpdatesEnabled(true);
    viewport()->update();
    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::placeWorkspaceClipboardItems(const QList<WorkspaceItemState> &items,
                                             const QVector<SessionImageId> &newIds,
                                             const QList<int> &sessionIndices)
{
    if (!isWorkspaceMode() || items.isEmpty() || newIds.size() != items.size()) {
        return;
    }
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_bindBook.clearSelectIds();
    for (int i = 0; i < items.size(); ++i) {
        const WorkspaceItemState &st = items.at(i);
        const SessionImageId sid = newIds.at(i);
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        if (st.path.isEmpty() || sid == kInvalidSessionImageId) {
            continue;
        }
        m_bindBook.addSelectId(sid);
        // Appearance (content + pose) must already be in the store under sid.
        addImageForSession(st.path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}

