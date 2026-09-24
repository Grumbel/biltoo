// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "hud/hudmodel.h"
#include "text/textsearchpolicy.h"
#include "view/canvaspatterngeometry.h"
#include "view/viewtransform.h"
#include "hud/hudgeometry.h"
#include "slideshow/slideshowclocks.h"
#include "text/textlayergeometry.h"
#include "workspace/pageguidegeometry.h"
#include "image/edgenavpolicy.h"
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include "imageitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "util/biltoo_logging.h"
#include <QGraphicsItem>


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
    m_workspace.paintViewportChrome(painter);

    // Letterbox composite fills the viewport during slideshow; edge chevrons
    // and HUD must paint after it or they are covered.
    m_slideshow.paintLetterboxComposite(painter);
    m_shell.paintEmptySessionInvite(painter);

    if (!m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && hostHoverEdge() != EdgeZone::None && isImageMode()
        && (m_image.sessionNav().isImageModeNavEnabled() || hostHoverEdge() == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    paintHudPanels(painter);
    m_slideshow.paintSeekbar(painter);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    GUI_BUDGET("ImageView::paintEvent");
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    if (!m_hud.perf().isEnabled()) {
        QGraphicsView::paintEvent(event);
        return;
    }
    QElapsedTimer t;
    t.start();
    QGraphicsView::paintEvent(event);
    m_hud.perf().notePaintUs(t.nsecsElapsed() / 1000);
}


// --- Slideshow overlays (was imageview_slideshow_paint.cpp) ---




// --- HUD / empty invite (was imageview_hud_paint.cpp) ---



void ImageView::paintHudPanels(QPainter &painter)
{
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    // Crop mode: always show a pinned “Crop mode” cue so the tool state is clear.
    const QString ssPrefetchLine = m_slideshow.slideshowPrefetchHudLine();
    // Loading · … only in the extended (pinned) HUD — not as a free-floating
    // chip during slideshow or normal Image browsing.
    const QString loadingLine = m_hud.appearance().isVisible() ? loadingStatusHudLine() : QString();
    if (m_cropCtrl.session().active() || m_hud.appearance().isVisible() || m_hud.flash().isVisible() || m_hud.flash().isIdentityPulse()
        || m_slideshow.hud().isPausedHud() || hostGallerySizeResolve().active()
        || !m_hud.centreProgress().titleRef().isEmpty()
        || !ssPrefetchLine.isEmpty()
        || !m_gallery.hoverPath().isEmpty()) {
        // Prefer the user preference (Preferences → HUD), not the widget font.
        QFont f = font();
        const int pt = m_hud.appearance().effectiveFontPointSize();
        f.setPointSize(pt);
        QFont boldF = f;
        boldF.setBold(true);
        const QFontMetrics fm(f);
        const QFontMetrics fmBold(boldF);
        const int margin = 10;
        const int pad = 8;
        const int lineGap = 2;
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();

        struct HudLine {
            QString text;
            bool bold = false;
        };

        auto drawPanel = [&](const QList<HudLine> &lines, int anchorX, int anchorY,
                             bool fromRight, bool fromBottom, bool centre = false) {
            if (lines.isEmpty()) {
                return;
            }
            const int maxBgW = HudGeometry::maxPanelBgW(viewW, margin);
            const int maxTextW = HudGeometry::maxPanelTextW(maxBgW, pad);
            QList<HudLine> drawn;
            int textW = 0;
            int textH = 0;
            for (const HudLine &hl : lines) {
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                for (const QString &w : HudGeometry::wrapHudLine(hl.text, m, maxTextW)) {
                    drawn.append({w, hl.bold});
                    HudGeometry::accumulateLineSize(&textW, &textH, m, w, maxTextW);
                }
            }
            if (drawn.isEmpty()) {
                return;
            }
            if (drawn.size() > 1) {
                textH += lineGap * (drawn.size() - 1);
            }
            const HudGeometry::PanelBox box = HudGeometry::placePanel(
                viewW, viewH, textW, textH, margin, pad, anchorX, anchorY,
                fromRight, fromBottom, centre);
            const QRect bg = HudGeometry::panelRect(box);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_hud.appearance().effectivePanelColor());
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(m_hud.appearance().effectiveTextColor());
            int ty = bg.top() + pad;
            const int textAreaW = bg.width() - 2 * pad;
            for (const HudLine &hl : drawn) {
                const QFont &lf = hl.bold ? boldF : f;
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                painter.setFont(lf);
                painter.drawText(QRect(bg.left() + pad, ty, textAreaW, m.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter, hl.text);
                ty += m.height() + lineGap;
            }
        };

        // Top-left: crop mode cue (persistent while active), slideshow paused
        // cue (persistent until resume/stop), or transient flash.
        if (m_cropCtrl.session().active()) {
            drawPanel({{tr("Crop mode"), true},
                       {tr("Handles · Reset · Apply · Esc"), false}},
                      margin, margin, false, false);
        } else if (m_slideshow.hud().isPausedHud()) {
            drawPanel({{tr("❚❚  Paused"), true},
                       {tr("Space: resume · Esc: leave"), false}},
                      margin, margin, false, false);
        } else if (!m_hud.centreProgress().titleRef().isEmpty()) {
            QList<HudLine> lines;
            lines.append({m_hud.centreProgress().titleRef(), true});
            if (!m_hud.centreProgress().detailRef().isEmpty()) {
                lines.append({m_hud.centreProgress().detailRef(), false});
            }
            // Non-blocking background work → sticky top-left. Only true
            // blockers (archive expand / open progress) use the viewport centre.
            const bool corner =
                m_hud.centreProgress().matchesTitlePrefix(tr("Loading tiles"))
                || m_hud.centreProgress().matchesTitlePrefix(tr("Improving previews"))
                || m_hud.centreProgress().matchesTitlePrefix(tr("Resolving sizes"));
            if (corner) {
                drawPanel(lines, margin, margin, false, false, false);
            } else {
                drawPanel(lines, 0, 0, false, false, true);
            }
        } else if (hostGallerySizeResolve().active() && hostGallerySizeResolve().total() > 0) {
            // Fallback if title was cleared but gate still active (interactive).
            const int done = ViewTransform::nonNeg(
                qint64(hostGallerySizeResolve().total())
                - qint64(hostGallerySizeResolve().pendingCount()));
            drawPanel({{tr("Resolving sizes…"), true},
                       {tr("%1 / %2").arg(done).arg(hostGallerySizeResolve().total()), false}},
                      margin, margin, false, false, false);
        } else if (m_hud.flash().isVisible() && m_hud.flash().hasAction()) {
            QString actionLine = m_hud.flash().actionText();
            if (m_hud.flash().hasDetail()) {
                actionLine += QLatin1Char(' ') + m_hud.flash().detailText();
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        } else if (m_hud.appearance().isVisible() || m_hud.flash().isIdentityPulse()) {
            QList<HudLine> topLeft;
            if (!loadingLine.isEmpty()) {
                topLeft.append({loadingLine, true});
            }
            if (!ssPrefetchLine.isEmpty()) {
                topLeft.append({ssPrefetchLine, false});
            }
            if (m_hud.perf().isEnabled()) {
                topLeft.append({
                    tr("FPS %1 · paint %2 ms · decode-win %3 ms (max %4)")
                        .arg(m_hud.perf().fpsValue(), 0, 'f', 1)
                        .arg(m_hud.perf().lastPaintUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_hud.perf().lastDecodeWindowUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_hud.perf().maxDecodeWindowUsValue() / 1000.0, 0, 'f', 1),
                    false});
            }
            ImageItem *focus = targetItem();
            if (!focus) {
                focus = primaryItem();
            }
            if (focus) {
                const QString q = pixelQualityLabel(focus);
                if (!q.isEmpty()) {
                    const int edge = focus->displayPixelLongEdge();
                    const QString line = edge > 0 && !focus->hasDecodedPixels()
                        ? tr("%1 · %2px").arg(q).arg(edge)
                        : q;
                    topLeft.append({line, false});
                }
            }
            if (!topLeft.isEmpty()) {
                drawPanel(topLeft, margin, margin, false, false);
            }
        }

        // Top-right: session index — pinned HUD or brief identity pulse after
        // user navigation. Not during pure action flashes (slideshow start, …)
        // and not on automatic slideshow advance (pulseIdentity=false).
        const QString badge = m_slideshow.sessionBadgeText();
        if (!badge.isEmpty() && (m_hud.appearance().isVisible() || m_hud.flash().isIdentityPulse())) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned HUD, identity pulse after user nav, or gallery hover
        if (m_hud.appearance().isVisible() || m_hud.flash().isIdentityPulse() || !m_gallery.hoverPath().isEmpty()) {
            QList<HudLine> bottom;
            const QString name = hudFileName();
            if (!name.isEmpty()) {
                bottom.append({name, true});
            }
            if (m_hud.appearance().isVisible()) {
                const QString tech = statusText();
                if (!tech.isEmpty() && tech != name) {
                    bottom.append({tech, false});
                }
            }
            drawPanel(bottom, margin, 0, false, true);
        }
    }


}
