// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "workspacegeometry.h"
#include "imageitem.h"

#include <QScrollBar>
#include <QtMath>


bool ImageView::hasWorkspaceContent() const
{
    if (!m_items.isEmpty()) {
        return true;
    }
    if (!m_workspace.stashedItems().isEmpty()) {
        return true;
    }
    return !m_workspace.savedItems().isEmpty();
}








void ImageView::updateWorkspaceSceneRect()
{
    if (!m_scene || !isWorkspaceMode()) {
        return;
    }
    QRectF bounds = m_scene->itemsBoundingRect();
    if (m_pageGuide.isVisible()) {
        bounds = bounds.united(pageGuideSceneRect());
    }
    // Viewport in scene coordinates — ensure room to pan around content.
    const QRectF viewScene = mapToScene(viewport()->rect()).boundingRect();
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
    const QRectF cur = m_scene->sceneRect();
    if (qAbs(cur.left() - bounds.left()) < 1.0
        && qAbs(cur.top() - bounds.top()) < 1.0
        && qAbs(cur.width() - bounds.width()) < 1.0
        && qAbs(cur.height() - bounds.height()) < 1.0) {
        return;
    }
    m_scene->setSceneRect(bounds);
}

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    const QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();
    QSizeF size = itemSize;
    if (size.width() < 1.0 || size.height() < 1.0) {
        size = QSizeF(200.0, 200.0);
    }

    // Cap the collision footprint so huge images still leave room nearby
    const qreal maxEdge = WorkspaceGeometry::placementMaxEdge(
        viewRect.width(), viewRect.height());
    size = WorkspaceGeometry::cappedFootprint(size, maxEdge);

    const qreal gap = 32.0;
    auto overlaps = [&](const QPointF &centre) {
        const QRectF proposed(centre.x() - size.width() / 2.0 - gap,
                              centre.y() - size.height() / 2.0 - gap,
                              size.width() + 2.0 * gap,
                              size.height() + 2.0 * gap);
        for (ImageItem *item : m_items) {
            if (item->sceneBoundingRect().intersects(proposed)) {
                return true;
            }
        }
        return false;
    };

    QPointF candidate = viewRect.center();
    if (m_items.isEmpty() || !overlaps(candidate)) {
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
    const QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isValid()) {
        return QPointF(bounds.right() + gap + size.width() / 2.0, bounds.center().y());
    }
    return viewRect.center();
}

WorkspaceItemState ImageView::defaultStateForPath(const QString &path, int ordinal) const
{
    WorkspaceItemState s;
    s.path = path;
    s.pos = QPointF(40.0 * ordinal, 30.0 * ordinal);
    s.scale = 1.0;
    s.scaleY = 1.0;
    s.rotation = 0.0;
    s.opacity = 1.0;
    s.z = ordinal;
    return s;
}
