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
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "biltoo_logging.h"

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

    // Half-ellipse underlay: ~80% of the edge (layout.fillRect), bulging inward.
    {
        QPainterPath lobe;
        const QRectF fr = layout.fillRect;
        if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
            // Full ellipse centred on the top edge; only the lower half is visible.
            lobe.addEllipse(QRectF(fr.left(), -fr.height(), fr.width(), fr.height() * 2.0));
        } else if (zone == EdgeNavPolicy::Zone::Previous) {
            lobe.addEllipse(QRectF(-fr.width(), fr.top(), fr.width() * 2.0, fr.height()));
        } else {
            lobe.addEllipse(QRectF(fr.left(), fr.top(), fr.width() * 2.0, fr.height()));
        }
        QLinearGradient grad;
        if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
            grad = QLinearGradient(0, 0, 0, fr.height());
            grad.setColorAt(0.0, QColor(0, 0, 0, 90));
            grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        } else if (zone == EdgeNavPolicy::Zone::Previous) {
            grad = QLinearGradient(fr.left(), 0, fr.right(), 0);
            grad.setColorAt(0.0, QColor(0, 0, 0, 90));
            grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        } else {
            grad = QLinearGradient(fr.left(), 0, fr.right(), 0);
            grad.setColorAt(0.0, QColor(0, 0, 0, 0));
            grad.setColorAt(1.0, QColor(0, 0, 0, 90));
        }
        painter.save();
        painter.setClipRect(fr);
        painter.fillPath(lobe, grad);
        painter.restore();
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

void ImageView::paintTextRubberBandOverlay(QPainter &painter)
{
    if (m_textLayer.isRubberbanding() && m_textLayer.hasRubberRect()) {
        painter.save();
        QPen pen(QColor(40, 120, 220, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(QColor(60, 160, 255, 40));
        painter.drawRect(m_textLayer.rubberRectRef().normalized());
        painter.restore();
    }

}

void ImageView::paintWorkspaceViewportChrome(QPainter &painter)
{
    if (!m_crop.active() && isWorkspaceMode() && m_scene) {
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
        if (!m_ssSettings.isZoomBlurLetterbox()) {
            painter.fillRect(vr, slideshowPadColor());
            return;
        }
        const bool haveFrom = !fromSrc.isNull();
        const bool haveTo = !toSrc.isNull();
        const qreal tt = ViewTransform::clamp01(t);
        const QString fPath = !fromPath.isEmpty() ? fromPath
            : (m_ss.hasFromPath() ? m_ss.fromPathRef() : m_ssDwell.biasPathRef());
        const QString tPath = !toPath.isEmpty() ? toPath : m_ss.toPathRef();
        if (haveFrom && haveTo && t >= 0.0) {
            painter.setOpacity(1.0);
            paintZoomBlurUnderlay(&painter, fromSrc, vr, blurKey(fPath));
            if (tt > 0.0) {
                painter.setOpacity(tt);
                paintZoomBlurUnderlay(&painter, toSrc, vr, blurKey(tPath));
                painter.setOpacity(1.0);
            }
            return;
        }
        if (haveFrom) {
            paintZoomBlurUnderlay(&painter, fromSrc, vr, blurKey(fPath));
            return;
        }
        if (haveTo) {
            paintZoomBlurUnderlay(&painter, toSrc, vr, blurKey(tPath));
            return;
        }
        painter.fillRect(vr, slideshowPadColor());
    };

    // Pure-phase composite (SLIDESHOW.md): wall clock sets fadeT; we only blit.
    if (m_ssHud.isProgressActive()
        && (m_ss.hasFromImage() || m_ssDwell.hasSourceImage() || m_ss.hasToImage())) {
        const QRect vr = viewport()->rect();
        // Prefer member references (not a local QImage copy) so paintMotionCover
        // can match the dwell atlas by address as well as by path.
        const QImage &fromImg = m_ss.hasFromImage() ? m_ss.fromImageRef() : m_ssDwell.sourceImageRef();
        const qreal fromT = m_ss.fromMotionTValue();
        const qreal toT = m_ss.toMotionTValue();
        if (m_ss.inTransition() && m_ss.hasToImage()) {
            const qreal t = m_ss.clampedFadeT();
            fillPad(vr, fromImg, m_ss.toImageRef(), t, m_ss.fromPathRef(), m_ss.toPathRef());
            if (m_ssSettings.isFadeBlack()) {
                // V envelope: A→black (t in [0,0.5]), then black→B (t in [0.5,1]).
                if (t < 0.5) {
                    if (!fromImg.isNull()) {
                        painter.setOpacity(1.0);
                        paintMotionCover(&painter, fromImg, fromT,
                                         m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
                    }
                    painter.setOpacity(t * 2.0);
                    painter.fillRect(vr, Qt::black);
                    painter.setOpacity(1.0);
                } else {
                    painter.setOpacity(1.0);
                    paintMotionCover(&painter, m_ss.toImageRef(), toT,
                                     m_ss.toBiasAPoint(), m_ss.toBiasBPoint(), m_ss.toPathRef());
                    painter.setOpacity((1.0 - t) * 2.0);
                    painter.fillRect(vr, Qt::black);
                    painter.setOpacity(1.0);
                }
            } else if (m_ssSettings.isSlideTransition()) {
                // Projector: A exits left, B enters from the right; both in motion.
                const int w = vr.width();
                const int xOld = int(qRound(-t * w));
                const int xNew = int(qRound((1.0 - t) * w));
                painter.setOpacity(1.0);
                painter.setClipRect(vr);
                if (!fromImg.isNull()) {
                    painter.save();
                    painter.translate(xOld, 0);
                    paintMotionCover(&painter, fromImg, fromT,
                                     m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
                    painter.restore();
                }
                painter.save();
                painter.translate(xNew, 0);
                paintMotionCover(&painter, m_ss.toImageRef(), toT,
                                 m_ss.toBiasAPoint(), m_ss.toBiasBPoint(), m_ss.toPathRef());
                painter.restore();
                painter.setClipping(false);
            } else if (m_ssSettings.isNoneTransition()) {
                // Hard cut at the end of the transition window (no blend).
                if (t < 1.0 - 1e-6) {
                    if (!fromImg.isNull()) {
                        painter.setOpacity(1.0);
                        paintMotionCover(&painter, fromImg, fromT,
                                         m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
                    }
                } else {
                    painter.setOpacity(1.0);
                    paintMotionCover(&painter, m_ss.toImageRef(), toT,
                                     m_ss.toBiasAPoint(), m_ss.toBiasBPoint(), m_ss.toPathRef());
                }
            } else {
                // Crossfade: A 1→0, B 0→1; both in motion.
                if (!fromImg.isNull()) {
                    painter.setOpacity(1.0 - t);
                    paintMotionCover(&painter, fromImg, fromT,
                                     m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
                }
                painter.setOpacity(t);
                paintMotionCover(&painter, m_ss.toImageRef(), toT,
                                 m_ss.toBiasAPoint(), m_ss.toBiasBPoint(), m_ss.toPathRef());
                painter.setOpacity(1.0);
            }
        } else if (!fromImg.isNull()) {
            fillPad(vr, fromImg, QImage(), -1.0, m_ss.fromPathRef());
            paintMotionCover(&painter, fromImg, fromT,
                             m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
        }
        // Pure phase painted the slide. Fall through so HUD / seekbar / pause
        // cues still draw (return here used to kill the entire overlay pass).
    }


}

void ImageView::paintEmptySessionInvite(QPainter &painter)
{
    // Empty session: invite the user to open or drop images.
    // Suppress while centre progress is active (archive expand / size resolve).
    if (m_items.isEmpty() && !hasClassicPath() && !m_crop.active()
        && m_centreProgress.titleRef().isEmpty() && !gallerySizeResolveActive()) {
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
    const QString ssPrefetchLine = slideshowPrefetchHudLine();
    // Loading · … only in the extended (pinned) HUD — not as a free-floating
    // chip during slideshow or normal Image browsing.
    const QString loadingLine = m_hudPrefs.isVisible() ? loadingStatusHudLine() : QString();
    if (m_crop.active() || m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_hudFlash.isIdentityPulse()
        || m_ssHud.isPausedHud() || gallerySizeResolveActive()
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
        if (m_crop.active()) {
            drawPanel({{tr("Crop mode"), true},
                       {tr("Handles · Reset · Apply · Esc"), false}},
                      margin, margin, false, false);
        } else if (m_ssHud.isPausedHud()) {
            drawPanel({{tr("❚❚  Paused"), true},
                       {tr("Space: resume · Esc: leave"), false}},
                      margin, margin, false, false);
        } else if (!m_centreProgress.titleRef().isEmpty()) {
            QList<HudLine> lines;
            lines.append({m_centreProgress.titleRef(), true});
            if (!m_centreProgress.detailRef().isEmpty()) {
                lines.append({m_centreProgress.detailRef(), false});
            }
            drawPanel(lines, 0, 0, false, false, true);
        } else if (gallerySizeResolveActive() && m_gallerySizeResolve.total() > 0) {
            // Fallback if title was cleared but gate still active.
            const int done = ViewTransform::nonNeg(
                qint64(m_gallerySizeResolve.total())
                - qint64(m_gallerySizeResolve.pendingCount()));
            drawPanel({{tr("Resolving sizes…"), true},
                       {tr("%1 / %2").arg(done).arg(m_gallerySizeResolve.total()), false}},
                      0, 0, false, false, true);
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
        const QString badge = sessionBadgeText();
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

void ImageView::paintSlideshowSeekbar(QPainter &painter)
{
    // Slideshow timeline (extended HUD only): video-player style progress bar
    // plus elapsed / total and remaining. Driven by setSlideshowTimeline from
    // the host clock. Falls back to per-interval dwell line if no timeline.
    if (m_ssHud.isProgressActive()
        && (m_hudPrefs.isVisible() || m_ssHud.isSeekbarVisible() || m_ssHud.isSeekDragging())) {
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();
        if (viewW > 0 && viewH > 0) {
            qreal fraction = 0.0;
            if (m_ssHud.timelineTotal() > 0) {
                fraction = qreal(m_ssHud.timelineElapsed())
                    / qreal(m_ssHud.timelineTotal());
            } else if (m_ssHud.isCycleProgressValid()) {
                fraction = m_ssHud.cycleProgress();
            } else if (m_ssHud.hasProgressInterval()) {
                qint64 elapsed = m_ssHud.progressBase();
                if (!m_ssHud.isProgressClockPaused()
                    && m_ssHud.isProgressElapsedValid()) {
                    elapsed += m_ssHud.progressElapsedMs();
                }
                fraction = qreal(elapsed) / qreal(m_ssHud.progressInterval());
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
            const int barH = HudGeometry::progressBarHeight(m_ssHud.isSeekbarVisible());
            painter.drawRect(0, viewH - barH, viewW, barH);
            if (fraction > 0.0) {
                const int barW = HudGeometry::progressFillWidth(fraction, viewW);
                painter.setBrush(c);
                painter.drawRect(0, viewH - barH, barW, barH);
            }

            if (m_ssHud.timelineTotal() > 0) {
                const qint64 remain = m_ssHud.timelineTotal()
                    - m_ssHud.timelineElapsed();
                const QString timeLine =
                    QStringLiteral("%1 / %2   −%3")
                        .arg(SlideshowClocks::formatClockMs(m_ssHud.timelineElapsed()),
                             SlideshowClocks::formatClockMs(m_ssHud.timelineTotal()),
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
    if (m_crop.active()) {
        paintCropOverlay(painter);
    }
    if (m_attention.active()) {
        paintAttentionOverlay(painter);
    }
    paintWorkspaceViewportChrome(painter);

    // Letterbox composite fills the viewport during slideshow; edge chevrons
    // and HUD must paint after it or they are covered.
    paintSlideshowLetterboxComposite(painter);
    paintEmptySessionInvite(painter);

    if (!m_crop.active() && !m_attention.active() && m_hoverEdge != EdgeZone::None && isImageMode()
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

void ImageView::paintCanvasBackground(QPainter *painter, const QRectF &rect,
                                           qreal viewScale)
{
    if (!painter) {
        return;
    }
    viewScale = ViewTransform::sanitizeViewScale(viewScale);

    auto fillChecker = [&](const QColor &a, const QColor &b) {
        const qreal cell = CanvasPatternGeometry::checkerCellScene(viewScale);
        const qreal x0 = std::floor(rect.left() / cell) * cell;
        const qreal y0 = std::floor(rect.top() / cell) * cell;
        const qreal x1 = std::ceil(rect.right() / cell) * cell;
        const qreal y1 = std::ceil(rect.bottom() / cell) * cell;
        for (qreal y = y0; y < y1; y += cell) {
            for (qreal x = x0; x < x1; x += cell) {
                const int ix = static_cast<int>(std::floor(x / cell));
                const int iy = static_cast<int>(std::floor(y / cell));
                const bool dark = ((ix + iy) & 1) != 0;
                painter->fillRect(QRectF(x, y, cell, cell), dark ? a : b);
            }
        }
    };

    auto fillImageTile = [&](QPixmap &tileCache, const QString &path,
                             auto storeTile) {
        if (tileCache.isNull() && !path.isEmpty()) {
            QPixmap px(path);
            if (!px.isNull()) {
                storeTile(px, path);
            }
        }
        if (tileCache.isNull()) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
            return;
        }
        const QPixmap &tile = tileCache;
        qreal tw = qreal(ViewTransform::atLeast1(tile.width()));
        qreal th = qreal(ViewTransform::atLeast1(tile.height()));
        const qreal lod = CanvasPatternGeometry::tileLodFactor(tw, viewScale);
        const qreal cellW = tw * lod;
        const qreal cellH = th * lod;
        const qreal x0 = std::floor(rect.left() / cellW) * cellW;
        const qreal y0 = std::floor(rect.top() / cellH) * cellH;
        const qreal x1 = std::ceil(rect.right() / cellW) * cellW;
        const qreal y1 = std::ceil(rect.bottom() / cellH) * cellH;
        for (qreal y = y0; y < y1; y += cellH) {
            for (qreal x = x0; x < x1; x += cellW) {
                painter->drawPixmap(QRectF(x, y, cellW, cellH), tile,
                                    QRectF(0, 0, tw, th));
            }
        }
    };

    auto paintMaterial = [&](const WorkspaceBackground &wb, QPixmap &tileCache,
                             auto storeTile) {
        if (wb.mode == WorkspaceBackgroundMode::Solid) {
            painter->fillRect(rect, wb.color.isValid() ? wb.color : m_canvasBg.primaryColor());
        } else if (wb.mode == WorkspaceBackgroundMode::Checkerboard) {
            const QColor a = wb.color.isValid() ? wb.color : m_canvasBg.primaryColor();
            const QColor b = wb.colorAlt.isValid() ? wb.colorAlt : a.lighter(120);
            fillChecker(a, b);
        } else if (wb.mode == WorkspaceBackgroundMode::ImageTile) {
            fillImageTile(tileCache, wb.imagePath, storeTile);
        } else if (wb.mode == WorkspaceBackgroundMode::ContentBlur) {
            // Image mode: cover-scale blur under the sharp item (viewport space).
            // Gallery has no single subject — fall back to solid.
            QImage src;
            QString path;
            if (isImageMode()) {
                ImageItem *item = primaryItem();
                if (!item && hasClassicPath()) {
                    item = findItemByPath(classicPath());
                }
                if (item) {
                    src = item->displayImage();
                    if (src.isNull()) {
                        src = item->sourceImage();
                    }
                    path = item->path();
                }
                if (path.isEmpty() && hasClassicPath()) {
                    path = classicPath();
                }
            }
            if (!src.isNull() && viewport()) {
                painter->save();
                painter->resetTransform();
                const QRect vr = viewport()->rect();
                const qint64 key = path.isEmpty() ? qint64(0) : qint64(qHash(path));
                paintZoomBlurUnderlay(painter, src, vr, key);
                painter->restore();
            } else {
                painter->fillRect(rect, m_canvasBg.primaryColor());
            }
        } else {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        }
    };

    const bool wsOverride = isWorkspaceMode()
        && !m_canvasBg.isWorkspaceAppDefault()
        && !m_canvasBg.isWorkspaceShowDefault();
    const bool viewOverride =
        (isGalleryMode() || isImageMode()) && !m_canvasBg.isViewAppDefault();

    if (wsOverride) {
        paintMaterial(m_canvasBg.workspaceRef(), m_canvasBg.workspaceTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setWorkspaceTile(px, path);
                      });
    } else if (viewOverride) {
        paintMaterial(m_canvasBg.viewRef(), m_canvasBg.viewTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setViewTile(px, path);
                      });
    } else {
        if (!m_canvasBg.useChecker(isWorkspaceMode())) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        } else {
            fillChecker(m_canvasBg.primaryColor(), m_canvasBg.checkerAlt());
        }
    }
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    paintCanvasBackground(painter, rect, transform().m11());

    // Page guide paper (under images): plain white sheet in scene units.
    if (m_pageGuide.isVisible() && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::white);
            painter->drawRect(page);
            painter->restore();
        }
    }
}



void ImageView::setShowTextRegions(bool on)
{
    if (!m_textLayer.setShowRegions(on)) {
        return;
    }
    if (m_textLayer.needsLayer()) {
        refreshTextLayer();
    } else {
        m_textLayer.resetLayerContent();
        m_textLayer.clearSearchMatches();
    }
    viewport()->update();
}

void ImageView::refreshTextLayer()
{
    m_textLayer.resetLayerContent();
    m_textLayer.clearSearchMatches();
    if (!m_textLayer.needsLayer()) {
        return;
    }
    // Search/outlines need a page ref; allow extract outside pure Image mode
    // (e.g. user opened Find while still on a page path).
    const QString path = classicPath();
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return;
    }
    // Prefer cache; ensure may do source I/O (acceptable for toggle / Find).
    ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(path);
    if (layer.regions.isEmpty()) {
        layer = ThumtooCache::ensurePageTextLayer(path);
    }
    m_textLayer.setLayerContent(layer, path);
    if (m_textLayer.hasSearchQuery()) {
        recomputeTextSearchMatches();
    }
}

bool ImageView::textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy)
{
    return TextSearchPolicy::matches(regionText, query, fuzzy);
}

void ImageView::setTextSearchFuzzy(bool on)
{
    if (!m_textLayer.setSearchFuzzy(on)) {
        return;
    }
    if (m_textLayer.hasSearchQuery()) {
        recomputeTextSearchMatches();
        viewport()->update();
    }
}

void ImageView::recomputeTextSearchMatches()
{
    m_textLayer.clearSearchMatches();
    if (!m_textLayer.hasSearchQuery() || !m_textLayer.hasRegions()) {
        return;
    }
    const QString qn = TextSearchPolicy::normalizeForSearch(m_textLayer.searchQueryRef());
    const QString qa = TextSearchPolicy::alnumOnly(m_textLayer.searchQueryRef());
    for (int i = 0; i < m_textLayer.regionCount(); ++i) {
        const auto &r = m_textLayer.regionAt(i);
        if (r.text.isEmpty()) {
            continue;
        }
        if (TextSearchPolicy::regionMatchesQuery(r.text, qn, qa, m_textLayer.isSearchFuzzy())) {
            m_textLayer.addSearchMatch(i);
        }
    }
}

int ImageView::setTextSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (m_textLayer.searchQueryRef() == trimmed && m_textLayer.hasRegions()) {
        return m_textLayer.searchMatchesRef().size();
    }
    m_textLayer.setSearchQuery(trimmed);
    if (!m_textLayer.hasSearchQuery()) {
        m_textLayer.clearSearchMatches();
        if (!m_textLayer.showsRegions()) {
            m_textLayer.resetLayerContent();
        }
        viewport()->update();
        return 0;
    }
    // Ensure layer is loaded for the current page.
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        refreshTextLayer();
    } else {
        recomputeTextSearchMatches();
    }
    viewport()->update();
    return m_textLayer.searchMatchesRef().size();
}

bool ImageView::hasTextLayer() const
{
    return m_textLayer.hasRegions() && m_textLayer.layerPathRef() == classicPath();
}

int ImageView::textLayerRegionCount() const
{
    if (!hasTextLayer()) {
        return 0;
    }
    return m_textLayer.regionCount();
}


bool ImageView::hitTextLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const
{
    if (pageOut) {
        *pageOut = 0;
    }
    if (uriOut) {
        uriOut->clear();
    }
    if (!isImageMode() || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    ImageItem *item = primaryItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        return false;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0 || !m_textLayer.pageBoundsValid()) {
        return false;
    }
    const QPointF scene = mapToScene(viewPos);
    const QPointF local = item->mapFromScene(scene);
    if (!item->contentRect().contains(local)) {
        return false;
    }
    const QPointF imgPt = local - item->offset();
    for (const ThumtooCache::TextRegion &r : m_textLayer.regions()) {
        if (r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        const QRectF img = textRegionImageRect(r);
        if (img.contains(imgPt)) {
            if (pageOut) {
                *pageOut = r.linkPage;
            }
            if (uriOut) {
                *uriOut = r.linkUri;
            }
            return true;
        }
    }
    return false;
}

bool ImageView::pageYUpForTextLayer() const
{
    const QString docPath = PagePath::documentFilePath(classicPath());
    return PagePath::isDjvuFile(docPath);
}


QRectF ImageView::textRegionImageRect(const ThumtooCache::TextRegion &region) const
{
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.pageBoundsValid()) {
        return {};
    }

    // Full unoriented page raster size — never recover from oriented imageSize().
    // Display size after 1–3 turns is swapped; after 4 turns QImage may drift by
    // a pixel; crop also changes imageSize(). Page text is authored against the
    // native page raster.
    const QString path = classicPath();
    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        const QSize known = m_sizeBook.known(path);
        if (!known.isEmpty()) {
            sourceSize = known;
        }
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        // Last resort: invert orientation from the live item (no crop only).
        sourceSize = item->imageSize();
        WorkspaceItemState stGuess;
        if (item->sessionId() != kInvalidSessionImageId) {
            stGuess = sessionAppearanceValue(item->sessionId());
        }
        int turns = stGuess.contentQuarterTurns % 4;
        if (turns < 0) {
            turns += 4;
        }
        if (!stGuess.hasCrop && (turns % 2) != 0) {
            sourceSize.transpose();
        }
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }

    WorkspaceItemState st;
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    if (sid != kInvalidSessionImageId) {
        st = sessionAppearanceValue(sid);
    }
    // Durable XDG row when session slot is empty (same as imageWithSessionAppearance).
    if (!SessionAppearance::hasContentAppearance(st) && !path.isEmpty()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && (stored.contentHFlip || stored.contentVFlip
                || stored.contentQuarterTurns != 0 || stored.hasCrop)) {
            st.contentHFlip = stored.contentHFlip;
            st.contentVFlip = stored.contentVFlip;
            st.contentQuarterTurns = stored.contentQuarterTurns;
            st.hasCrop = stored.hasCrop;
            st.cropRect = stored.cropRect;
            st.cropSourceSize = stored.cropSourceSize;
            st.cropRotation = stored.cropRotation;
        }
    }

    const bool pageYUp = pageYUpForTextLayer();
    const QRectF inSource = ThumtooCache::pageRectToImageRect(
        region.bbox, m_textLayer.pageBounds(), sourceSize, pageYUp);
    if (inSource.isEmpty()) {
        return {};
    }

    // Flip → CW quarter-turns → crop (see docs/CONTENT_COORDINATES.md).
    // Content geometry is intrinsic/logical (imageSize / offset / contentRect).
    // Soft samples are painted *into* that rect — never rescale text into the
    // soft pixmap pixel size (that made highlights tiny until full res arrived).
    return SessionAppearance::mapSourceRectToContentDisplay(inSource, sourceSize, st);
}

QRectF ImageView::textRubberBandImageRect() const
{
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.hasRubberRect()) {
        return {};
    }
    const QRectF sceneRect = mapToScene(m_textLayer.rubberRectRef()).boundingRect();
    // Map scene corners to item local, then subtract offset → image pixels.
    const QRectF local = item->mapFromScene(sceneRect).boundingRect();
    return local.translated(-item->offset());
}

void ImageView::finishTextRubberBand()
{
    const QRect viewRect = m_textLayer.rubberRectRef().normalized();
    m_textLayer.endRubber();
    m_textLayer.clearSelectedRegions();
    if (viewRect.width() < 4 || viewRect.height() < 4) {
        viewport()->update();
        return;
    }
    // Ensure text layer (refreshTextLayer skips when neither search nor outlines).
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        const bool hadShow = m_textLayer.showsRegions();
        m_textLayer.setShowRegions(true);
        refreshTextLayer();
        m_textLayer.setShowRegions(hadShow);
    }
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.hasRegions() || !m_textLayer.pageBoundsValid()) {
        viewport()->update();
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0) {
        viewport()->update();
        return;
    }
    // Recompute rubber from stored origin - use viewRect mapped to image.
    m_textLayer.setRubberRect(viewRect);
    const QRectF imgRubber = textRubberBandImageRect();
    m_textLayer.clearRubberRect();
    if (imgRubber.isEmpty()) {
        viewport()->update();
        return;
    }
    QVector<QRectF> regionRects(m_textLayer.regionCount());
    for (int i = 0; i < m_textLayer.regionCount(); ++i) {
        const auto &r = m_textLayer.regionAt(i);
        if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        regionRects[i] = textRegionImageRect(r);
    }
    QVector<int> selected =
        TextLayerGeometry::indicesIntersecting(regionRects, imgRubber);
    // Reading order: top-to-bottom, then left-to-right by image rect.
    TextLayerGeometry::sortReadingOrder(&selected, regionRects);
    m_textLayer.setSelectedRegions(selected);
    viewport()->update();
}

QString ImageView::selectedText() const
{
    QStringList lines;
    for (int idx : m_textLayer.selectedRegionsRef()) {
        if (idx < 0 || idx >= m_textLayer.regionCount()) {
            continue;
        }
        const QString &tx = m_textLayer.regionAt(idx).text;
        if (!tx.isEmpty()) {
            lines.append(tx);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void ImageView::clearTextSelection()
{
    if (!m_textLayer.hasSelection() && !m_textLayer.isRubberbanding()) {
        return;
    }
    m_textLayer.clearSelection();
    viewport()->update();
}

bool ImageView::copySelectedText()
{
    const QString text = selectedText();
    if (text.isEmpty()) {
        return false;
    }
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        return false;
    }
    clip->setText(text);
    return true;
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
    QPen pen(QColor(0, 180, 255, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(4.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->setRenderHint(QPainter::Antialiasing, true);
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
        // Inset ~2 local units equivalent is awkward after map; cosmetic stroke
        // sits on the edge. drawPolygon follows rotated/sheared cells.
        painter->drawPolygon(scenePoly);
    }
    painter->restore();
}

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    // Page guide outline above images so the frame stays visible when tiles
    // cover the white sheet (scene coordinates).
    if (m_pageGuide.isVisible() && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            QPen pen(QColor(40, 100, 200, 220));
            pen.setStyle(Qt::DashLine);
            pen.setWidthF(0);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(page);
            QRectF margin = page.adjusted(page.width() * 0.05, page.height() * 0.05,
                                          -page.width() * 0.05, -page.height() * 0.05);
            QPen marginPen(QColor(40, 100, 200, 120));
            marginPen.setStyle(Qt::DotLine);
            marginPen.setCosmetic(true);
            painter->setPen(marginPen);
            painter->drawRect(margin);
            painter->restore();
        }
    }

    // Text search highlights + optional region outlines (Image mode page docs).
    if (isImageMode() && m_textLayer.hasRegions()
        && (m_textLayer.showsRegions() || m_textLayer.hasSearchMatches())) {
        if (ImageItem *item = primaryItem()) {
            const QSize sz = item->imageSize();
            if (sz.width() > 0 && sz.height() > 0 && m_textLayer.pageBoundsValid()) {
                painter->save();
                // Search hits: filled yellow first (under outlines / selection).
                if (m_textLayer.hasSearchMatches()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(255, 220, 40, 110));
                    for (int idxMatch : m_textLayer.searchMatchesRef()) {
                        if (idxMatch < 0 || idxMatch >= m_textLayer.regionCount()) {
                            continue;
                        }
                        const auto &r = m_textLayer.regionAt(idxMatch);
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                // Rubber-band text selection (cyan).
                if (m_textLayer.hasSelection()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(60, 160, 255, 100));
                    for (int idxSel : m_textLayer.selectedRegionsRef()) {
                        if (idxSel < 0 || idxSel >= m_textLayer.regionCount()) {
                            continue;
                        }
                        const auto &r = m_textLayer.regionAt(idxSel);
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                if (m_textLayer.showsRegions()) {
                    painter->setBrush(Qt::NoBrush);
                    for (const ThumtooCache::TextRegion &r : m_textLayer.regions()) {
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        const QPolygonF scenePoly = item->mapToScene(local);
                        if (r.role == ThumtooCache::TextRegion::Role::Link) {
                            QPen pen(QColor(40, 180, 80, 200));
                            pen.setCosmetic(true);
                            pen.setWidthF(0);
                            painter->setPen(pen);
                        } else {
                            QPen pen(QColor(220, 80, 40, 180));
                            pen.setCosmetic(true);
                            pen.setWidthF(0);
                            painter->setPen(pen);
                        }
                        painter->drawPolygon(scenePoly);
                    }
                }
                painter->restore();
            }
        }
    }

    // Viewport-space overlays on the same painter as the scene (required for
    // QOpenGLWidget: a second QPainter(viewport()) after paintEvent whites out).
    if (!painter) {
        return;
    }
    // Gallery selection frames: scene-space overlay so item ItemCoordinateCache
    // is not invalidated on select or scroll (was painted inside ImageItem::paint).
    if (isGalleryMode()) {
        paintGallerySelectionFrames(painter, rect);
    }
    // Bare Gallery: skip HUD/edges/slideshow overlay pass.
    if (isGalleryMode() && !m_hudPrefs.isVisible() && !m_hudFlash.isVisible() && !m_hudFlash.isIdentityPulse()
        && !m_ssHud.isPausedHud() && !gallerySizeResolveActive()
        && m_centreProgress.titleRef().isEmpty()
        && m_hoverEdge == EdgeZone::None && !m_crop.active()
        && !m_ssDwell.isMotionActive() 
        ) {
        return;
    }
    painter->save();
    painter->resetTransform();
    if (viewport()) {
        const qreal dpr = viewport()->devicePixelRatioF();
        if (!qFuzzyCompare(dpr, 1.0)) {
            painter->scale(dpr, dpr);
        }
    }
    paintViewportOverlays(*painter);
    painter->restore();
}

void ImageView::paintGroupSelectionChrome(QPainter *painter, const QList<ImageItem *> &items) const
{
    if (!painter) {
        return;
    }
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return;
    }
    const QRect viewRect = mapFromScene(sceneBounds).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Group chrome: violet family so it is distinct from single-select blue
    // and crop amber.
    const QColor frameCol(150, 90, 220, 220);
    const QColor handleFill(180, 120, 255, 240);
    const QColor handleFillHot(210, 160, 255, 255);
    const QColor handleEdge(80, 40, 140);
    const QColor rotFill(200, 140, 255);
    const QColor rotFillHot(255, 230, 120);

    QPen framePen(frameCol, 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(1.75);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(viewRect);

    QPointF pts[8];
    PageGuideGeometry::handlePoints(viewRect, pts);
    // Corners 0,2,4,6: rounded line-arc-line; edges 1,3,5,7: short bars.
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = m_groupXform.isHandleHot(id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        if (hot) {
            QPen glow(handleFill, 0);
            glow.setCosmetic(true);
            glow.setWidthF(thick * 0.55);
            glow.setCapStyle(Qt::RoundCap);
            glow.setJoinStyle(Qt::RoundJoin);
            painter->setPen(glow);
            painter->drawPath(path);
        }
    };
    auto drawEdgeBar = [&](const QPointF &mid, const QPointF &along, int id) {
        const bool hot = m_groupXform.isHandleHot(id);
        const QPointF a = unit(along);
        const QPointF perp(-a.y(), a.x());
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal len = hs * 1.8;
        const qreal thick = hs * 0.35;
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(hp);
        painter->setBrush(hot ? handleFillHot : handleFill);
        QPolygonF bar;
        bar << mid + a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) - perp * (thick / 2)
            << mid + a * (len / 2) - perp * (thick / 2);
        painter->drawPolygon(bar);
        painter->setBrush(Qt::NoBrush);
    };
    // pts: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    drawCorner(pts[0], QPointF(1, 0), QPointF(0, 1), 0);
    drawEdgeBar(pts[1], QPointF(1, 0), 1);
    drawCorner(pts[2], QPointF(-1, 0), QPointF(0, 1), 2);
    drawEdgeBar(pts[3], QPointF(0, 1), 3);
    drawCorner(pts[4], QPointF(-1, 0), QPointF(0, -1), 4);
    drawEdgeBar(pts[5], QPointF(1, 0), 5);
    drawCorner(pts[6], QPointF(1, 0), QPointF(0, -1), 6);
    drawEdgeBar(pts[7], QPointF(0, 1), 7);

    // Rotate knobs outside mid-edges (same offset language as single-item).
    constexpr qreal kRotateOffset = 28.0;
    const QPointF rot[4] = {
        QPointF(viewRect.center().x(), viewRect.top() - kRotateOffset),
        QPointF(viewRect.right() + kRotateOffset, viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom() + kRotateOffset),
        QPointF(viewRect.left() - kRotateOffset, viewRect.center().y()),
    };
    const QPointF edgeMid[4] = {
        QPointF(viewRect.center().x(), viewRect.top()),
        QPointF(viewRect.right(), viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    QPen stem(frameCol, 0);
    stem.setCosmetic(true);
    stem.setWidthF(1.25);
    for (int i = 0; i < 4; ++i) {
        const int handleId = 8 + i;
        const bool hot = m_groupXform.isHandleHot(handleId);
        painter->setPen(stem);
        painter->drawLine(edgeMid[i], rot[i]);
        const qreal rad = hot ? 7.0 : 5.0;
        painter->setBrush(hot ? rotFillHot : rotFill);
        QPen rp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        rp.setCosmetic(true);
        rp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(rp);
        painter->drawEllipse(rot[i], rad, rad);
    }

    painter->restore();
}


void ImageView::paintPageGuideHandles(QPainter *painter) const
{
    if (!painter || !m_pageGuide.isInteractive()) {
        return;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return;
    }
    const QRect viewRect = mapFromScene(page).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Same language as group scale grips, in the page-guide blue family.
    const QColor handleEdge(20, 60, 120);
    const QColor handleHot(255, 255, 255);

    const QPointF pts[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = m_pageGuide.isHandleHot(id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    auto drawEdge = [&](const QPointF &c, const QPointF &along, int id) {
        const bool hot = m_pageGuide.isHandleHot(id);
        const QPointF d = unit(along);
        const qreal hs = hot ? 11.0 : 9.0;
        const qreal half = hs * 1.1;
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hs * (hot ? 0.42 : 0.32));
        hp.setCapStyle(Qt::RoundCap);
        painter->setPen(hp);
        painter->drawLine(c - d * half, c + d * half);
    };
    // Corners: 0 TL, 2 TR, 4 BR, 6 BL
    drawCorner(pts[0], pts[1] - pts[0], pts[7] - pts[0], 0);
    drawCorner(pts[2], pts[1] - pts[2], pts[3] - pts[2], 2);
    drawCorner(pts[4], pts[5] - pts[4], pts[3] - pts[4], 4);
    drawCorner(pts[6], pts[5] - pts[6], pts[7] - pts[6], 6);
    // Edges: 1 T, 3 R, 5 B, 7 L
    drawEdge(pts[1], pts[2] - pts[0], 1);
    drawEdge(pts[3], pts[4] - pts[2], 3);
    drawEdge(pts[5], pts[4] - pts[6], 5);
    drawEdge(pts[7], pts[6] - pts[0], 7);
    painter->restore();
}
