// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace scene rect, empty placement, and content-presence queries.
// ImageView keeps thin routers (DisplayPipelineHost::updateWorkspaceSceneRect).

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "workspace/workspacegeometry.h"

#include <QGraphicsScene>
#include <QWidget>
#include <QtMath>

bool WorkspaceController::hasContent() const
{
    if (!m_view->liveItems().isEmpty()) {
        return true;
    }
    if (!m_stashedItems.isEmpty()) {
        return true;
    }
    return !m_savedItems.isEmpty();
}

void WorkspaceController::updateSceneRect()
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene || !m_view->isWorkspaceMode()) {
        return;
    }
    QRectF bounds = scene->itemsBoundingRect();
    if (m_pageGuide.isVisible()) {
        bounds = bounds.united(pageGuideSceneRect());
    }
    // Viewport in scene coordinates — ensure room to pan around content.
    QWidget *vp = m_view->viewport();
    if (!vp) {
        return;
    }
    const QRectF viewScene = m_view->mapToScene(vp->rect()).boundingRect();
    const QSizeF margins = WorkspaceGeometry::sceneMargins(viewScene.size());
    const qreal mx = margins.width();
    const qreal my = margins.height();
    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = WorkspaceGeometry::paddedSceneRect(viewScene);
    } else {
        bounds.adjust(-mx, -my, mx, my);
        // Keep a viewport-sized halo so middle-drag can always move a little.
        bounds = bounds.united(viewScene.adjusted(-mx, -my, mx, my));
    }
    // Avoid feedback loops from tiny float noise.
    const QRectF cur = scene->sceneRect();
    if (qAbs(cur.left() - bounds.left()) < 1.0
        && qAbs(cur.top() - bounds.top()) < 1.0
        && qAbs(cur.width() - bounds.width()) < 1.0
        && qAbs(cur.height() - bounds.height()) < 1.0) {
        return;
    }
    scene->setSceneRect(bounds);
}

QPointF WorkspaceController::findEmptyPlacement(const QSizeF &itemSize) const
{
    QWidget *vp = m_view->viewport();
    if (!vp) {
        return QPointF();
    }
    const QRectF viewRect = m_view->mapToScene(vp->rect()).boundingRect();
    QSizeF size = itemSize;
    if (size.width() < 1.0 || size.height() < 1.0) {
        size = QSizeF(200.0, 200.0);
    }

    // Cap the collision footprint so huge images still leave room nearby
    const qreal maxEdge = WorkspaceGeometry::placementMaxEdge(
        viewRect.width(), viewRect.height());
    size = WorkspaceGeometry::cappedFootprint(size, maxEdge);

    const qreal gap = 32.0;
    const QList<ImageItem *> &items = m_view->liveItems();
    auto overlaps = [&](const QPointF &centre) {
        const QRectF proposed(centre.x() - size.width() / 2.0 - gap,
                              centre.y() - size.height() / 2.0 - gap,
                              size.width() + 2.0 * gap,
                              size.height() + 2.0 * gap);
        for (ImageItem *item : items) {
            if (item && item->sceneBoundingRect().intersects(proposed)) {
                return true;
            }
        }
        return false;
    };

    QPointF candidate = viewRect.center();
    if (items.isEmpty() || !overlaps(candidate)) {
        return candidate;
    }

    // Spiral search around the viewport centre
    const qreal stepX = size.width() + gap;
    const qreal stepY = size.height() + gap;
    for (int ring = 1; ring <= 48; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dy = -ring; dy <= ring; ++dy) {
                if (qMax(qAbs(dx), qAbs(dy)) != ring) {
                    continue;
                }
                candidate = viewRect.center() + QPointF(dx * stepX, dy * stepY);
                if (!overlaps(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Last resort: to the right of everything currently on the canvas
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        const QRectF bounds = scene->itemsBoundingRect();
        if (bounds.isValid()) {
            return QPointF(bounds.right() + gap + size.width() / 2.0, bounds.center().y());
        }
    }
    return viewRect.center();
}
