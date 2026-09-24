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
                  [](ImageItem *a, ImageItem *b) {
                      return a->placement().z < b->placement().z;
                  });
        if (selected.size() == 1) {
            selected.first()->paintInteractionChrome(&painter);
        } else if (selected.size() > 1) {
            // Multi-select: per-item outline only; group scale handles on the union.
            for (ImageItem *item : selected) {
                item->paintSelectionFrame(&painter);
            }
            m_workspace.paintGroupSelectionChrome(&painter, selected);
        }
        if (m_workspace.pageGuideSession().isInteractive()) {
            m_workspace.paintPageGuideHandles(&painter);
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

    if (!m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && hostHoverEdge() != EdgeZone::None && isImageMode()
        && (m_image.sessionNav().isImageModeNavEnabled() || hostHoverEdge() == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    paintHudPanels(painter);
    paintSlideshowSeekbar(painter);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    GUI_BUDGET("ImageView::paintEvent");
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

void ImageView::paintGallerySelectionFrames(QPainter *painter, const QRectF &exposed) const
{
    if (!painter || !m_scene) {
        return;
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    painter->save();
    painter->setBrush(Qt::NoBrush);
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Double ring so selection reads on light and dark tiles (single cyan was
    // easy to lose). Cosmetic widths = device pixels under any zoom.
    QPen outer(QColor(0, 0, 0, 200));
    outer.setCosmetic(true);
    outer.setWidthF(7.0);
    QPen inner(QColor(0, 200, 255, 255));
    inner.setCosmetic(true);
    inner.setWidthF(3.0);
    // Soft wash so the cell is obviously "in" the selection set.
    const QColor wash(0, 180, 255, 36);
    for (QGraphicsItem *gi : selected) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->isInteractive()) {
            continue;
        }
        // Same rect the content paint uses (gallery clip when Grid-Crop).
        const QRectF local = item->displayContentRect();
        const QPolygonF scenePoly = item->mapToScene(local);
        const QRectF bounds = scenePoly.boundingRect();
        if (!exposed.isNull() && !exposed.intersects(bounds)) {
            continue;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(wash);
        painter->drawPolygon(scenePoly);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(outer);
        painter->drawPolygon(scenePoly);
        painter->setPen(inner);
        painter->drawPolygon(scenePoly);
    }
    painter->restore();
}

// --- Slideshow overlays (was imageview_slideshow_paint.cpp) ---

void ImageView::paintSlideshowLetterboxComposite(QPainter &painter)
{
    // Letterbox underlay. During transitions, crossfade from→to underlays.
    // Blurs are cached by stable path key; each paint only draws two pixmaps
    // with opacity (GPU). CPU blur runs once per slide/viewport, not per frame.
    auto blurKey = [](const QString &path) -> qint64 {
        return path.isEmpty() ? qint64(0) : qint64(qHash(path));
    };
    auto fillPad = [&](const QRect &vr, const QImage &fromSrc = QImage(),
                       const QImage &toSrc = QImage(), qreal t = -1.0,
                       const QString &fromPath = QString(),
                       const QString &toPath = QString()) {
        if (!m_slideshow.settings().isZoomBlurLetterbox()) {
            painter.fillRect(vr, slideshowPadColor());
            return;
        }
        const bool haveFrom = !fromSrc.isNull();
        const bool haveTo = !toSrc.isNull();
        const qreal tt = ViewTransform::clamp01(t);
        const QString fPath = !fromPath.isEmpty() ? fromPath
            : (m_slideshow.phase().hasFromPath() ? m_slideshow.phase().fromPathRef() : m_slideshow.dwell().biasPathRef());
        const QString tPath = !toPath.isEmpty() ? toPath : m_slideshow.phase().toPathRef();
        if (haveFrom && haveTo && t >= 0.0) {
            painter.setOpacity(1.0);
            m_slideshow.paintZoomBlurUnderlay(&painter, fromSrc, vr, blurKey(fPath));
            if (tt > 0.0) {
                painter.setOpacity(tt);
                m_slideshow.paintZoomBlurUnderlay(&painter, toSrc, vr, blurKey(tPath));
                painter.setOpacity(1.0);
            }
            return;
        }
        if (haveFrom) {
            m_slideshow.paintZoomBlurUnderlay(&painter, fromSrc, vr, blurKey(fPath));
            return;
        }
        if (haveTo) {
            m_slideshow.paintZoomBlurUnderlay(&painter, toSrc, vr, blurKey(tPath));
            return;
        }
        painter.fillRect(vr, slideshowPadColor());
    };

    // Pure-phase composite (SLIDESHOW.md): wall clock sets fadeT; we only blit.
    if (m_slideshow.hud().isProgressActive()
        && (m_slideshow.phase().hasFromImage() || m_slideshow.dwell().hasSourceImage() || m_slideshow.phase().hasToImage())) {
        const QRect vr = viewport()->rect();
        // Prefer member references (not a local QImage copy) so paintMotionCover
        // can match the dwell atlas by address as well as by path.
        const QImage &fromImg = m_slideshow.phase().hasFromImage() ? m_slideshow.phase().fromImageRef() : m_slideshow.dwell().sourceImageRef();
        const qreal fromT = m_slideshow.phase().fromMotionTValue();
        const qreal toT = m_slideshow.phase().toMotionTValue();
        if (m_slideshow.phase().inTransition() && m_slideshow.phase().hasToImage()) {
            const qreal t = m_slideshow.phase().clampedFadeT();
            fillPad(vr, fromImg, m_slideshow.phase().toImageRef(), t, m_slideshow.phase().fromPathRef(), m_slideshow.phase().toPathRef());
            if (m_slideshow.settings().isFadeBlack()) {
                // V envelope: A→black (t in [0,0.5]), then black→B (t in [0.5,1]).
                if (t < 0.5) {
                    if (!fromImg.isNull()) {
                        painter.setOpacity(1.0);
                        m_slideshow.paintMotionCover(&painter, fromImg, fromT,
                                         m_slideshow.dwell().biasAPoint(), m_slideshow.dwell().biasBPoint(), m_slideshow.phase().fromPathRef());
                    }
                    painter.setOpacity(t * 2.0);
                    painter.fillRect(vr, Qt::black);
                    painter.setOpacity(1.0);
                } else {
                    painter.setOpacity(1.0);
                    m_slideshow.paintMotionCover(&painter, m_slideshow.phase().toImageRef(), toT,
                                     m_slideshow.phase().toBiasAPoint(), m_slideshow.phase().toBiasBPoint(), m_slideshow.phase().toPathRef());
                    painter.setOpacity((1.0 - t) * 2.0);
                    painter.fillRect(vr, Qt::black);
                    painter.setOpacity(1.0);
                }
            } else if (m_slideshow.settings().isSlideTransition()) {
                // Projector: A exits left, B enters from the right; both in motion.
                const int w = vr.width();
                const int xOld = int(qRound(-t * w));
                const int xNew = int(qRound((1.0 - t) * w));
                painter.setOpacity(1.0);
                painter.setClipRect(vr);
                if (!fromImg.isNull()) {
                    painter.save();
                    painter.translate(xOld, 0);
                    m_slideshow.paintMotionCover(&painter, fromImg, fromT,
                                     m_slideshow.dwell().biasAPoint(), m_slideshow.dwell().biasBPoint(), m_slideshow.phase().fromPathRef());
                    painter.restore();
                }
                painter.save();
                painter.translate(xNew, 0);
                m_slideshow.paintMotionCover(&painter, m_slideshow.phase().toImageRef(), toT,
                                 m_slideshow.phase().toBiasAPoint(), m_slideshow.phase().toBiasBPoint(), m_slideshow.phase().toPathRef());
                painter.restore();
                painter.setClipping(false);
            } else if (m_slideshow.settings().isNoneTransition()) {
                // Hard cut at the end of the transition window (no blend).
                if (t < 1.0 - 1e-6) {
                    if (!fromImg.isNull()) {
                        painter.setOpacity(1.0);
                        m_slideshow.paintMotionCover(&painter, fromImg, fromT,
                                         m_slideshow.dwell().biasAPoint(), m_slideshow.dwell().biasBPoint(), m_slideshow.phase().fromPathRef());
                    }
                } else {
                    painter.setOpacity(1.0);
                    m_slideshow.paintMotionCover(&painter, m_slideshow.phase().toImageRef(), toT,
                                     m_slideshow.phase().toBiasAPoint(), m_slideshow.phase().toBiasBPoint(), m_slideshow.phase().toPathRef());
                }
            } else {
                // Crossfade: A 1→0, B 0→1; both in motion.
                if (!fromImg.isNull()) {
                    painter.setOpacity(1.0 - t);
                    m_slideshow.paintMotionCover(&painter, fromImg, fromT,
                                     m_slideshow.dwell().biasAPoint(), m_slideshow.dwell().biasBPoint(), m_slideshow.phase().fromPathRef());
                }
                painter.setOpacity(t);
                m_slideshow.paintMotionCover(&painter, m_slideshow.phase().toImageRef(), toT,
                                 m_slideshow.phase().toBiasAPoint(), m_slideshow.phase().toBiasBPoint(), m_slideshow.phase().toPathRef());
                painter.setOpacity(1.0);
            }
        } else if (!fromImg.isNull()) {
            fillPad(vr, fromImg, QImage(), -1.0, m_slideshow.phase().fromPathRef());
            m_slideshow.paintMotionCover(&painter, fromImg, fromT,
                             m_slideshow.dwell().biasAPoint(), m_slideshow.dwell().biasBPoint(), m_slideshow.phase().fromPathRef());
        }
        // Pure phase painted the slide. Fall through so HUD / seekbar / pause
        // cues still draw (return here used to kill the entire overlay pass).
    }


}


void ImageView::paintSlideshowSeekbar(QPainter &painter)
{
    // Slideshow timeline (extended HUD only): video-player style progress bar
    // plus elapsed / total and remaining. Driven by setSlideshowTimeline from
    // the host clock. Falls back to per-interval dwell line if no timeline.
    if (m_slideshow.hud().isProgressActive()
        && (m_hudPrefs.isVisible() || m_slideshow.hud().isSeekbarVisible() || m_slideshow.hud().isSeekDragging())) {
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();
        if (viewW > 0 && viewH > 0) {
            qreal fraction = 0.0;
            if (m_slideshow.hud().timelineTotal() > 0) {
                fraction = qreal(m_slideshow.hud().timelineElapsed())
                    / qreal(m_slideshow.hud().timelineTotal());
            } else if (m_slideshow.hud().isCycleProgressValid()) {
                fraction = m_slideshow.hud().cycleProgress();
            } else if (m_slideshow.hud().hasProgressInterval()) {
                qint64 elapsed = m_slideshow.hud().progressBase();
                if (!m_slideshow.hud().isProgressClockPaused()
                    && m_slideshow.hud().isProgressElapsedValid()) {
                    elapsed += m_slideshow.hud().progressElapsedMs();
                }
                fraction = qreal(elapsed) / qreal(m_slideshow.hud().progressInterval());
            }
            fraction = ViewTransform::clamp01(fraction);

            QColor c = m_hudPrefs.effectiveTextColor(QColor(255, 255, 255));
            QColor track = c;
            track.setAlpha(60);
            c.setAlpha(200);
            painter.setPen(Qt::NoPen);
            painter.setBrush(track);
            // Thin dwell/session line when HUD pinned; thicker mpv seekbar when
            // the cursor is in the bottom hover zone.
            const int barH = HudGeometry::progressBarHeight(m_slideshow.hud().isSeekbarVisible());
            painter.drawRect(0, viewH - barH, viewW, barH);
            if (fraction > 0.0) {
                const int barW = HudGeometry::progressFillWidth(fraction, viewW);
                painter.setBrush(c);
                painter.drawRect(0, viewH - barH, barW, barH);
            }

            if (m_slideshow.hud().timelineTotal() > 0) {
                const qint64 remain = m_slideshow.hud().timelineTotal()
                    - m_slideshow.hud().timelineElapsed();
                const QString timeLine =
                    QStringLiteral("%1 / %2   −%3")
                        .arg(SlideshowClocks::formatClockMs(m_slideshow.hud().timelineElapsed()),
                             SlideshowClocks::formatClockMs(m_slideshow.hud().timelineTotal()),
                             SlideshowClocks::formatClockMs(remain));

                QFont f = painter.font();
                f.setPointSize(m_hudPrefs.effectiveFontPointSize());
                painter.setFont(f);
                const QFontMetrics fm(f);
                const int textW = fm.horizontalAdvance(timeLine);
                const int textH = fm.height();
                const HudGeometry::PanelBox clock =
                    HudGeometry::placeTimelineClock(viewW, viewH, textW, textH);
                painter.setBrush(m_hudPrefs.effectivePanelColor());
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(
                    HudGeometry::panelRect(clock), HudGeometry::kTimelineCornerRadius,
                    HudGeometry::kTimelineCornerRadius);
                painter.setPen(m_hudPrefs.effectiveTextColor());
                painter.drawText(
                    QRect(clock.x + HudGeometry::kTimelinePadX,
                          clock.y + HudGeometry::kTimelinePadY, textW, textH),
                    Qt::AlignLeft | Qt::AlignVCenter, timeLine);
            }
        }
    }
}

// --- HUD / empty invite (was imageview_hud_paint.cpp) ---

void ImageView::paintEmptySessionInvite(QPainter &painter)
{
    // Empty session: invite the user to open or drop images.
    // Suppress while progress is active (expand / size resolve / tile load).
    if (m_items.isEmpty() && !m_image.hasClassicPath() && !m_cropCtrl.session().active()
        && m_centreProgress.titleRef().isEmpty() && !hostGallerySizeResolve().active()) {
        painter.save();
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont titleFont = font();
        titleFont.setPointSize(HudGeometry::clampTitlePointSize(titleFont.pointSize()));
        titleFont.setBold(true);
        QFont hintFont = font();
        hintFont.setPointSize(HudGeometry::clampHintPointSize(hintFont.pointSize()));
        const QString title = isWorkspaceMode()
            ? tr("Drop images here")
            : tr("Drop images here or open a file");
        const QString hint = isWorkspaceMode()
            ? tr("Drag files or filmstrip thumbnails onto the canvas to place images")
            : tr("File → Open…  ·  Ctrl+O  ·  drag and drop");
        const QFontMetrics titleFm(titleFont);
        const QFontMetrics hintFm(hintFont);
        const int gap = 8;
        const int totalH = titleFm.height() + gap + hintFm.height();
        const int cy = viewport()->height() / 2 - totalH / 2;
        painter.setFont(titleFont);
        painter.setPen(QColor(220, 220, 220, 230));
        painter.drawText(QRect(0, cy, viewport()->width(), titleFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, title);
        painter.setFont(hintFont);
        painter.setPen(QColor(180, 180, 180, 200));
        painter.drawText(QRect(0, cy + titleFm.height() + gap, viewport()->width(),
                               hintFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, hint);

        // Edge-zone captions (Image mode uses these corners once a session is open).
        if (isImageMode() || (!isWorkspaceMode() && !isGalleryMode())) {
            QFont edgeFont = font();
            edgeFont.setPointSize(HudGeometry::clampEdgePointSize(edgeFont.pointSize()));
            painter.setFont(edgeFont);
            painter.setPen(QColor(160, 160, 160, 180));
            const QFontMetrics efm(edgeFont);
            const int em = 16;
            const int vw = viewport()->width();
            const int vh = viewport()->height();
            const QString prevLabel = tr("← Previous");
            const QString nextLabel = tr("Next →");
            const QString backLabel = tr("↑ Back to Gallery / Workspace");
            painter.drawText(QRect(em, vh / 2 - efm.height() / 2, efm.horizontalAdvance(prevLabel) + 8,
                                   efm.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, prevLabel);
            const int nextW = efm.horizontalAdvance(nextLabel) + 8;
            painter.drawText(QRect(vw - em - nextW, vh / 2 - efm.height() / 2, nextW, efm.height()),
                             Qt::AlignRight | Qt::AlignVCenter, nextLabel);
            const int backW = efm.horizontalAdvance(backLabel) + 8;
            painter.drawText(QRect((vw - backW) / 2, em, backW, efm.height()),
                             Qt::AlignHCenter | Qt::AlignTop, backLabel);
        }
        painter.restore();
    }

}


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
    const QString loadingLine = m_hudPrefs.isVisible() ? loadingStatusHudLine() : QString();
    if (m_cropCtrl.session().active() || m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_hudFlash.isIdentityPulse()
        || m_slideshow.hud().isPausedHud() || hostGallerySizeResolve().active()
        || !m_centreProgress.titleRef().isEmpty()
        || !ssPrefetchLine.isEmpty()
        || !m_gallery.hoverPath().isEmpty()) {
        // Prefer the user preference (Preferences → HUD), not the widget font.
        QFont f = font();
        const int pt = m_hudPrefs.effectiveFontPointSize();
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
            painter.setBrush(m_hudPrefs.effectivePanelColor());
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(m_hudPrefs.effectiveTextColor());
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
        } else if (!m_centreProgress.titleRef().isEmpty()) {
            QList<HudLine> lines;
            lines.append({m_centreProgress.titleRef(), true});
            if (!m_centreProgress.detailRef().isEmpty()) {
                lines.append({m_centreProgress.detailRef(), false});
            }
            // Non-blocking background work → sticky top-left. Only true
            // blockers (archive expand / open progress) use the viewport centre.
            const bool corner =
                m_centreProgress.matchesTitlePrefix(tr("Loading tiles"))
                || m_centreProgress.matchesTitlePrefix(tr("Improving previews"))
                || m_centreProgress.matchesTitlePrefix(tr("Resolving sizes"));
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
        } else if (m_hudFlash.isVisible() && m_hudFlash.hasAction()) {
            QString actionLine = m_hudFlash.actionText();
            if (m_hudFlash.hasDetail()) {
                actionLine += QLatin1Char(' ') + m_hudFlash.detailText();
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        } else if (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse()) {
            QList<HudLine> topLeft;
            if (!loadingLine.isEmpty()) {
                topLeft.append({loadingLine, true});
            }
            if (!ssPrefetchLine.isEmpty()) {
                topLeft.append({ssPrefetchLine, false});
            }
            if (m_perf.isEnabled()) {
                topLeft.append({
                    tr("FPS %1 · paint %2 ms · decode-win %3 ms (max %4)")
                        .arg(m_perf.fpsValue(), 0, 'f', 1)
                        .arg(m_perf.lastPaintUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_perf.lastDecodeWindowUsValue() / 1000.0, 0, 'f', 1)
                        .arg(m_perf.maxDecodeWindowUsValue() / 1000.0, 0, 'f', 1),
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
        if (!badge.isEmpty() && (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse())) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned HUD, identity pulse after user nav, or gallery hover
        if (m_hudPrefs.isVisible() || m_hudFlash.isIdentityPulse() || !m_gallery.hoverPath().isEmpty()) {
            QList<HudLine> bottom;
            const QString name = hudFileName();
            if (!name.isEmpty()) {
                bottom.append({name, true});
            }
            if (m_hudPrefs.isVisible()) {
                const QString tech = statusText();
                if (!tech.isEmpty() && tech != name) {
                    bottom.append({tech, false});
                }
            }
            drawPanel(bottom, margin, 0, false, true);
        }
    }


}
