// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Slideshow letterbox composite and seekbar overlay paint.

#include "imageview.h"
#include "imageitem.h"
#include "viewtransform.h"

#include <QPainter>

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

