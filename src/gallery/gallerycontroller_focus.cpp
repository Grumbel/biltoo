// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Gallery focus / reveal (selection + ensureVisible + hover HUD path).
// ImageView keeps thin public routers for MainWindow and host callers.

#include "gallery/gallerycontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"

#include <QGraphicsScene>
#include <QWidget>

void GalleryController::focusItem(ImageItem *item)
{
    if (!item || !m_view->canvasScene()) {
        return;
    }
    m_view->canvasScene()->clearSelection();
    item->setSelected(true);
    if (!m_view->isGalleryMode()) {
        return;
    }
    // Open/TTFP: first index is often already in view after pack — ensureVisible
    // on a large scene was hundreds of ms for no visual change.
    bool needScroll = true;
    if (QWidget *vp = m_view->viewport()) {
        const QRectF vis = m_view->mapToScene(vp->rect()).boundingRect();
        if (vis.isValid() && item->sceneBoundingRect().intersects(vis)) {
            needScroll = false;
        }
    }
    if (needScroll) {
        m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                              ViewTransform::kEnsureVisibleMargin);
    }
    // Keyboard focus: show filename in the HUD like mouse hover.
    const QString path = item->path();
    if (setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::focusSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    focusItem(m_view->findItemBySessionId(sessionId));
}

void GalleryController::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Before clearSelection so a selected duplicate wins over first-match.
    focusItem(m_view->findItemForPath(path));
}

void GalleryController::revealPath(const QString &path)
{
    if (path.isEmpty() || !m_view->isGalleryMode()) {
        return;
    }
    // Prefer the selected instance of this path when duplicates exist (LoadAdd).
    ImageItem *item = m_view->findItemForPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                          ViewTransform::kEnsureVisibleMargin);
    if (setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::revealSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId || !m_view->isGalleryMode()) {
        return;
    }
    ImageItem *item = m_view->findItemBySessionId(sessionId);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                          ViewTransform::kEnsureVisibleMargin);
    const QString path = item->path();
    if (!path.isEmpty() && setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}
