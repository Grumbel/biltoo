// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image-mode edge nav hover state + affordance paint (owned by ImageController).

#include "image/imagecontroller.h"
#include "imageview.h"
#include "image/edgenavpolicy.h"
#include "image/toolpolicy.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWidget>

namespace {

bool isNavEdge(EdgeNavPolicy::Zone zone)
{
    return zone == EdgeNavPolicy::Zone::Previous || zone == EdgeNavPolicy::Zone::Next
        || zone == EdgeNavPolicy::Zone::GalleryReturn;
}

} // namespace

EdgeNavPolicy::Zone ImageController::hoverEdge() const
{
    return m_hoverEdge;
}

EdgeNavPolicy::Zone ImageController::edgeZoneAt(const QPoint &viewPos) const
{
    if (!m_view->isImageMode()) {
        return EdgeNavPolicy::Zone::None;
    }
    if (m_view->hostCrop().session().active() || m_view->hostAttention().session().active()) {
        return EdgeNavPolicy::Zone::None;
    }
    return EdgeNavPolicy::zoneAt(
        viewPos, m_view->width(), m_view->height(),
        sessionNav().isGalleryReturnAvailable(),
        sessionNav().isImageModeNavEnabled());
}

int ImageController::edgeZoneWidth() const
{
    return EdgeNavPolicy::zoneWidth(m_view->width());
}

int ImageController::edgeZoneHeight() const
{
    return EdgeNavPolicy::zoneHeight(m_view->height());
}

bool ImageController::setHoverEdge(EdgeNavPolicy::Zone zone)
{
    if (zone == m_hoverEdge) {
        return false;
    }
    m_hoverEdge = zone;
    if (isNavEdge(m_hoverEdge)) {
        m_view->setCursor(Qt::PointingHandCursor);
    } else if (!m_view->hostChrome().isPanning()
               && !m_view->hostWorkspace().itemInteract().isRotating()) {
        m_view->setCursor(ToolPolicy::cursorFor(m_view->currentTool()));
    }
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
    return true;
}

void ImageController::clearHoverEdge()
{
    (void)setHoverEdge(EdgeNavPolicy::Zone::None);
}

void ImageController::updateHoverEdge(const QPoint &viewPos)
{
    (void)setHoverEdge(edgeZoneAt(viewPos));
}

void ImageController::drawEdgeAffordances(QPainter &painter) const
{
    if (m_hoverEdge == EdgeNavPolicy::Zone::None || !m_view->isImageMode()) {
        return;
    }
    if (m_hoverEdge != EdgeNavPolicy::Zone::GalleryReturn
        && !sessionNav().isImageModeNavEnabled()) {
        return;
    }

    const EdgeNavPolicy::Zone zone = m_hoverEdge;
    QWidget *vp = m_view->viewport();
    if (!vp) {
        return;
    }
    const QRect vr = vp->rect();
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
            painter.drawEllipse(QRectF(-1.0, -1.0, 2.0, 2.0));
            painter.restore();
        };

        if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
            paintRadialLobe(QPointF(fr.center().x(), fr.top()),
                            fr.width() * 0.5, fr.height(), fr);
        } else if (zone == EdgeNavPolicy::Zone::Previous) {
            paintRadialLobe(QPointF(fr.left(), fr.center().y()),
                            fr.width(), fr.height() * 0.5, fr);
        } else {
            paintRadialLobe(QPointF(fr.right(), fr.center().y()),
                            fr.width(), fr.height() * 0.5, fr);
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
