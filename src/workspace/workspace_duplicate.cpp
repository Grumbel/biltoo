// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Duplicate selected tiles (Workspace + Gallery). Placement differs by mode;
// MainWindow pre-allocates SessionImageIds. ImageView keeps a thin router.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "content/contentxform.h"
#include "display/displaypipelinecontroller.h"
#include "item/itemworld.h"

#include <QGraphicsScene>
#include <QWidget>
#include <QtGlobal>

void WorkspaceController::duplicateSelected(const QVector<SessionImageId> &newIds,
                                            int firstSessionIndex)
{
    if (!m_view->isWorkspaceMode() && !m_view->isGalleryMode()) {
        return;
    }
    // Walk live items (session/canvas order), not scene selection order, so
    // parallel newIds from MainWindow::selectedPaths() stay aligned.
    QList<ImageItem *> sources;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->isSelected()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        if (ImageItem *item = m_view->targetItem()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        return;
    }
    if (newIds.size() < sources.size()) {
        qCritical("duplicateSelected: newIds size %lld < sources %lld — shortfall tiles stay unbound",
                  static_cast<long long>(newIds.size()),
                  static_cast<long long>(sources.size()));
    }

    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    scene->clearSelection();
    int idIdx = 0;
    int sessionIdx = firstSessionIndex;
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
            // Still consume a pre-allocated id slot so parallel vectors stay aligned
            // with MainWindow session rows (one row per selected source).
            if (idIdx < newIds.size()) {
                ++idIdx;
            }
            if (sessionIdx >= 0) {
                ++sessionIdx;
            }
            continue;
        }

        // Freeze policy: store + live when durable and not mid-edit.
        WorkspaceItemState content = m_view->freezeItemAppearance(src);
        content.path = src->path();
        // freeze may carry live color lag; durable Color is store authority.
        if (src->sessionId() != kInvalidSessionImageId
            && m_view->itemWorld().hasColor(src->sessionId())) {
            content.colorAdjust = m_view->itemWorld().color(src->sessionId()).grade;
        }

        QSize intrinsic = src->imageSize();
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            const QSize native = m_view->layoutSizeForPath(src->path(), QImage());
            intrinsic = ContentXform::layoutSize(native, content);
        }
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            // Do not adopt sample pixel size (LQIP/soft).
            intrinsic = QSize(1, 1);
        }

        auto *copy = new ImageItem(src->path(), intrinsic);
        m_view->applyItemModeFlags(copy);
        scene->addItem(copy);
        m_view->liveItems().append(copy);
        // Attach already-baked display; do not put into ImageCache.
        m_view->hostDisplayPipeline().attachDisplaySample(copy, display, content, kind);
        if (m_view->isWorkspaceMode()) {
            ItemComponents::Placement pl = src->placement();
            pl.pos += QPointF(40.0, 40.0); // visible beside the original
            pl.z += 0.01;
            copy->applyPlacement(pl);
        } else {
            // Gallery: upright tile; pack runs after membership update.
            ItemComponents::Placement pl;
            pl.pos = src->pos();
            copy->applyPlacement(pl);
        }

        // Bind immediately when MainWindow pre-allocated a SessionImageId.
        const SessionImageId id = (idIdx < newIds.size()) ? newIds.at(idIdx)
                                                          : kInvalidSessionImageId;
        ++idIdx;
        if (id != kInvalidSessionImageId) {
            if (ImageItem *owner = m_view->findItemBySessionId(id)) {
                if (owner != copy) {
                    qCritical("duplicateSelected: SessionImageId %lld already on another tile — leave unbound",
                              static_cast<long long>(id));
                    m_view->hostBindBook().pendingAppearance().insert(copy, content);
                } else {
                    m_view->setItemSessionId(copy, id);
                }
            } else {
                m_view->setItemSessionId(copy, id);
            }
            if (copy->sessionId() == id) {
                WorkspaceItemState slot = content;
                ItemComponents::applyPlacementToState(slot, copy->placement());
                slot.sessionId = id;
                if (sessionIdx >= 0) {
                    copy->setSessionIndex(sessionIdx);
                    slot.sessionIndex = sessionIdx;
                } else {
                    slot.sessionIndex = m_view->sessionListIndex(copy);
                }
                slot.path = copy->path();
                m_view->itemWorld().setAppearance(id, slot);
                m_view->syncLiveColorFromState(copy, slot.colorAdjust, true);
                const QImage appearance = m_view->hostSessionAppearanceImage(copy);
                if (!appearance.isNull()) {
                    emit m_view->sessionAppearanceChanged(id, copy->path(), appearance);
                    const bool hasCrop = m_view->itemWorld().hasCrop(id);
                    emit m_view->sessionCropApplied(id, copy->path(), appearance, hasCrop);
                }
            }
        } else {
            // Id missing or shortfall vs sources — programming error; stage so a
            // PendingItemAppearanceBook may still recover appearance on bind.
            qCritical("duplicateSelected: no SessionImageId for copy path=%s — stage pending",
                      qPrintable(copy->path()));
            m_view->hostBindBook().pendingAppearance().insert(copy, content);
        }
        if (sessionIdx >= 0) {
            ++sessionIdx;
        }
        copy->setSelected(true);
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}
