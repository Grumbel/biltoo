// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-guide session lives on WorkspaceController. ImageView keeps a thin
// public API for shell/print and input routing + renderForPrint.

#include "imageview.h"
#include "imageitem.h"
#include "workspace/pageguidegeometry.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QPrinter>
#include <QScrollBar>
#include <QMouseEvent>

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

    if (isWorkspaceMode() && m_scene) {
        const QRectF source = (m_workspace.pageGuideSession().isVisible() && m_workspace.pageGuideSceneRect().isValid())
            ? m_workspace.pageGuideSceneRect()
            : contentExportBounds();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        // High-res materialize — same path as PNG export (not Soft scene samples).
        paintHighResExportItems(painter, source, pageRect);
        return;
    }

    if (isImageMode()) {
        ImageItem *item = primaryItem();
        if (!item) {
            item = targetItem();
        }
        if (item && !item->path().isEmpty()) {
            const QImage img = blockingExportDisplayForItem(item);
            if (!img.isNull()) {
                QSizeF fitted(img.size());
                fitted.scale(pageRect.size(), Qt::KeepAspectRatio);
                const QRectF target(
                    pageRect.center().x() - fitted.width() / 2.0,
                    pageRect.center().y() - fitted.height() / 2.0,
                    fitted.width(), fitted.height());
                painter->save();
                painter->translate(target.center());
                const ItemComponents::Placement pl = item->placement();
                painter->rotate(pl.rotation);
                painter->scale(pl.hFlip ? -1.0 : 1.0, pl.vFlip ? -1.0 : 1.0);
                painter->translate(-target.center());
                painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter->drawImage(target, img);
                painter->restore();
                return;
            }
        }
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


bool ImageView::tryMouseMovePageGuide(QMouseEvent *event)
{
    if (m_workspace.pageGuideSession().isDragging()) {
        m_workspace.updatePageGuideResize(mapToScene(event->pos()), event->modifiers());
        event->accept();
        return true;
    }
    if (isWorkspaceMode() && m_workspace.pageGuideSession().isInteractive()
        && !(event->buttons() & Qt::LeftButton)) {
        const int ph = m_workspace.pageGuideHandleAt(event->pos());
        if (m_workspace.pageGuideSession().setHoverHandle(ph)) {
            viewport()->update();
        }
        if (ph >= 0) {
            switch (ph) {
            case 0: case 4:
                viewport()->setCursor(Qt::SizeFDiagCursor);
                break;
            case 2: case 6:
                viewport()->setCursor(Qt::SizeBDiagCursor);
                break;
            case 1: case 5:
                viewport()->setCursor(Qt::SizeVerCursor);
                break;
            case 3: case 7:
                viewport()->setCursor(Qt::SizeHorCursor);
                break;
            default:
                break;
            }
            event->accept();
            return true;
        }
    }
    return false;
}

bool ImageView::tryMouseReleasePageGuide(QMouseEvent *event)
{
    if (!m_workspace.pageGuideSession().isDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    m_workspace.endPageGuideResize();
    event->accept();
    return true;
}
