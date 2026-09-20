// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "textsearchpolicy.h"
#include "canvaspatterngeometry.h"
#include "viewtransform.h"
#include "hudgeometry.h"
#include "slideshowclocks.h"
#include "textlayergeometry.h"
#include "pageguidegeometry.h"
#include "edgenavpolicy.h"
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include "thumtoocache.h"
#include "pagepath.h"
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include "imageitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "biltoo_logging.h"
#include <QGraphicsItem>

void ImageView::drawEdgeAffordances(QPainter &painter)
{
    if (m_hoverEdge == EdgeZone::None || !isImageMode()) {
        return;
    }
    if (m_hoverEdge != EdgeZone::GalleryReturn && !m_sessionNav.isImageModeNavEnabled()) {
        return;
    }

    EdgeNavPolicy::Zone zone = EdgeNavPolicy::Zone::None;
    switch (m_hoverEdge) {
    case EdgeZone::Previous:
        zone = EdgeNavPolicy::Zone::Previous;
        break;
    case EdgeZone::Next:
        zone = EdgeNavPolicy::Zone::Next;
        break;
    case EdgeZone::GalleryReturn:
        zone = EdgeNavPolicy::Zone::GalleryReturn;
        break;
    default:
        return;
    }

    const QRect vr = viewport()->rect();
    const EdgeNavPolicy::ChromeLayout layout = EdgeNavPolicy::chromeLayout(
        zone, vr, edgeZoneWidth(), edgeZoneHeight());
    if (layout.fillRect.isEmpty()) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    const int r = EdgeNavPolicy::kDefaultButtonRadius;

    auto drawChevronButton = [&](int cx, int cy, auto buildChevron) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 140));
        painter.drawEllipse(QPoint(cx, cy), r, r);
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawEllipse(QPoint(cx, cy), r - 3, r - 3);
        QPainterPath chevron;
        buildChevron(chevron, cx, cy);
        QPen pen(QColor(40, 40, 40), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.strokePath(chevron, pen);
    };

    // Soft radial lobe (~80% of the edge via layout.fillRect). QRadialGradient
    // in a scaled unit circle gives a smooth elliptical falloff without hard
    // linear strips.
    {
        const QRectF fr = layout.fillRect;
        const QColor core(0, 0, 0, 110);
        const QColor mid(0, 0, 0, 55);
        const QColor edge(0, 0, 0, 0);

        auto paintRadialLobe = [&](QPointF centre, qreal rx, qreal ry,
                                   const QRectF &clip) {
            if (rx < 1.0 || ry < 1.0 || clip.isEmpty()) {
                return;
            }
            painter.save();
            painter.setClipRect(clip);
            painter.translate(centre);
            painter.scale(rx, ry);
            QRadialGradient g(QPointF(0, 0), 1.0);
            g.setColorAt(0.00, core);
            g.setColorAt(0.45, mid);
            g.setColorAt(1.00, edge);
            painter.setPen(Qt::NoPen);
            painter.setBrush(g);
            // Unit circle; scale maps it to the ellipse.
            painter.drawEllipse(QRectF(-1.0, -1.0, 2.0, 2.0));
            painter.restore();
        };

        if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
            // Centre on top edge mid; only lower half visible via clip.
            paintRadialLobe(QPointF(fr.center().x(), fr.top()),
                            fr.width() * 0.5, fr.height(),
                            fr);
        } else if (zone == EdgeNavPolicy::Zone::Previous) {
            // Centre on left edge mid; only right half of the ellipse shows.
            paintRadialLobe(QPointF(fr.left(), fr.center().y()),
                            fr.width(), fr.height() * 0.5,
                            fr);
        } else {
            // Next: centre on right edge mid.
            paintRadialLobe(QPointF(fr.right(), fr.center().y()),
                            fr.width(), fr.height() * 0.5,
                            fr);
        }
    }

    const int cx = layout.buttonCenter.x();
    const int cy = layout.buttonCenter.y();
    if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 10, py + 5);
            chevron.lineTo(px, py - 6);
            chevron.lineTo(px + 10, py + 5);
        });
    } else if (zone == EdgeNavPolicy::Zone::Previous) {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px + 5, py - 10);
            chevron.lineTo(px - 6, py);
            chevron.lineTo(px + 5, py + 10);
        });
    } else {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 5, py - 10);
            chevron.lineTo(px + 6, py);
            chevron.lineTo(px - 5, py + 10);
        });
    }
}

void ImageView::paintWorkspaceViewportChrome(QPainter &painter)
{
    if (!m_cropCtrl.session().active() && isWorkspaceMode() && m_scene) {
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                // Only paint chrome for items we still own (guards against a
                // stale selection entry after destroyCanvasItem).
                if (ii->isInteractive() && m_items.contains(ii) && ii->scene() == m_scene) {
                    selected.append(ii);
                }
            }
        }
        std::sort(selected.begin(), selected.end(),
                  [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
        if (selected.size() == 1) {
            selected.first()->paintInteractionChrome(&painter);
        } else if (selected.size() > 1) {
            // Multi-select: per-item outline only; group scale handles on the union.
            for (ImageItem *item : selected) {
                item->paintSelectionFrame(&painter);
            }
            paintGroupSelectionChrome(&painter, selected);
        }
        if (m_pageGuide.isInteractive()) {
            paintPageGuideHandles(&painter);
        }
    }

}

void ImageView::paintViewportOverlays(QPainter &painter)
{
    paintTextRubberBandOverlay(painter);

    // Viewport-device-pixel overlays (handles, HUD, slideshow cover). Called from
    // drawForeground with an identity transform so this works on both the
    // raster and QOpenGLWidget viewports — a second QPainter on the GL viewport
    // after QGraphicsView::paintEvent clears the framebuffer (white screen).

    // Workspace chrome in *viewport* device pixels (not scene drawForeground).
    // Painting here keeps handles a constant on-screen size under any view or
    // item scale — the same coordinate space as edge affordances and the HUD.
    if (m_cropCtrl.session().active()) {
        m_cropCtrl.paintCropOverlay(painter);
    }
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.paintAttentionOverlay(painter);
    }
    paintWorkspaceViewportChrome(painter);

    // Letterbox composite fills the viewport during slideshow; edge chevrons
    // and HUD must paint after it or they are covered.
    paintSlideshowLetterboxComposite(painter);
    paintEmptySessionInvite(painter);

    if (!m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && m_hoverEdge != EdgeZone::None && isImageMode()
        && (m_sessionNav.isImageModeNavEnabled() || m_hoverEdge == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    paintHudPanels(painter);
    paintSlideshowSeekbar(painter);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    if (!m_perf.isEnabled()) {
        QGraphicsView::paintEvent(event);
        return;
    }
    QElapsedTimer t;
    t.start();
    QGraphicsView::paintEvent(event);
    m_perf.notePaintUs(t.nsecsElapsed() / 1000);
}
