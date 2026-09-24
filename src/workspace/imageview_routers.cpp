// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with workspace/ ownership.

#include "imageview.h"
#include "imageitem.h"
#include "util/biltoo_thread.h"
#include "session/sessionbindbook.h"

// --- from src/imageview_canvas.cpp ---
QList<ImageItem *> ImageView::collectDoomedWorkspaceItems(const QStringList &paths,
                                                          const QVector<SessionImageId> &sessionIds) const
{
    return m_workspace.collectDoomedItems(paths, sessionIds);
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    return m_workspace.pathOnLiveCanvas(path);
}


// --- from src/imageview_canvas_focus.cpp ---
ImageItem *ImageView::primaryItem() const
{
    return m_workspace.primaryItem();
}

ImageItem *ImageView::targetItem() const
{
    return m_workspace.targetItem();
}

void ImageView::destroyCanvasItem(ImageItem *item, bool persistState)
{
    m_workspace.destroyCanvasItem(item, persistState);
}

void ImageView::updateWorkspaceSceneRect()
{
    m_workspace.updateSceneRect();
}

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    return m_workspace.findEmptyPlacement(itemSize);
}

WorkspaceItemState ImageView::defaultStateForPath(const QString &path, int ordinal) const
{
    return m_workspace.defaultStateForPath(path, ordinal);
}

// --- from src/imageview_canvas_place.cpp ---
// --- from src/imageview_session_bind.cpp ---
void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    m_workspace.purgeSatisfiedPendingBinds(path);
}

void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    m_workspace.applyPendingBindScenePos(item, bound);
}

bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    return m_workspace.installFullPreservingWorkspaceFootprint(item, image);
}

bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    return m_workspace.takePendingSessionBindForNewItem(path, item, out);
}

void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                                    bool haveBound, const PendingSessionBind &bound)
{
    m_workspace.placeNewLoadAddItem(item, path, image, haveBound, bound);
}

QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    return m_workspace.collectItemsForSessionId(sessionId);
}

QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    return m_workspace.destroySessionIdItems(doomed);
}

// --- from src/imageview_session_remove.cpp ---
void ImageView::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    m_workspace.prunePendingBindsAndSavedForSessionId(sessionId);
}

void ImageView::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    m_workspace.prunePathOrdersAfterSessionRemove(removedPaths);
}

void ImageView::restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                                  const QPointF &keptCenter, int scrollH, int scrollV)
{
    m_workspace.restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH,
                                                  scrollV);
}

void ImageView::setCurrentSessionId(SessionImageId id)
{
    if (m_session.identity().currentId == id) {
        return;
    }
    m_session.identity().setCurrentId(id);
    // Attention marker is per SessionImageId — reload draft for the new image.
    m_attentionCtrl.onCurrentSessionChanged();
}

// --- from src/imageview_pageguide.cpp ---
void ImageView::setPageGuideVisible(bool on)
{
    m_workspace.setPageGuideVisible(on);
}

void ImageView::setPageGuideFromPrinter(const QPrinter &printer)
{
    m_workspace.setPageGuideFromPrinter(printer);
}

QRectF ImageView::pageGuideSceneRect() const
{
    return m_workspace.pageGuideSceneRect();
}

void ImageView::fitPageGuideToContent(qreal marginPx)
{
    m_workspace.fitPageGuideToContent(marginPx);
}

void ImageView::setPageGuideSelected(bool on)
{
    m_workspace.setPageGuideSelected(on);
}

void ImageView::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid()) {
        return;
    }

    if (isWorkspaceMode()) {
        m_workspace.renderForPrint(painter, pageRect);
        return;
    }

    if (isImageMode()) {
        m_image.renderForPrint(painter, pageRect);
        return;
    }

    if (m_scene) {
        QRectF source = m_scene->itemsBoundingRect();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        source.adjust(-4, -4, 4, 4);
        m_scene->render(painter, pageRect, source, Qt::KeepAspectRatio);
    }
}

// --- from src/imageview_selection.cpp ---

bool ImageView::validateUniqueLiveSessionIds(const char *context) const
{
    return m_workspace.validateUniqueLiveSessionIds(context);
}

QList<ImageItem *> ImageView::transformTargets() const
{
    return m_workspace.transformTargets();
}

// --- Clipboard / duplicate (was imageview_clipboard.cpp) ---

