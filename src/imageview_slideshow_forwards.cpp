// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView → SlideshowController public API forwards.

#include "imageview.h"

void ImageView::setSlideshowPadColor(const QColor &color)
{
    m_slideshow.setSlideshowPadColor(color);
}


void ImageView::setSlideshowLetterboxFill(SlideshowLetterboxFill mode)
{
    m_slideshow.setSlideshowLetterboxFill(mode);
}


void ImageView::setSessionPosition(int index, int total, bool pulseIdentity)
{
    m_slideshow.setSessionPosition(index, total, pulseIdentity);
}


void ImageView::setSlideshowProgress(bool active, int intervalMs)
{
    m_slideshow.setSlideshowProgress(active, intervalMs);
}


void ImageView::setSlideshowProgressPaused(bool paused)
{
    m_slideshow.setSlideshowProgressPaused(paused);
}


void ImageView::setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs)
{
    m_slideshow.setSlideshowTimeline(elapsedMs, totalMs);
}


void ImageView::setSlideshowCycleProgress(qreal phase01)
{
    m_slideshow.setSlideshowCycleProgress(phase01);
}


void ImageView::setSlideshowTransition(SlideshowTransition kind)
{
    m_slideshow.setSlideshowTransition(kind);
}


void ImageView::setSlideshowTransitionDurationMs(int ms)
{
    m_slideshow.setSlideshowTransitionDurationMs(ms);
}


void ImageView::cancelSlideshowTransition()
{
    m_slideshow.cancelSlideshowTransition();
}


void ImageView::setSlideshowMotion(SlideshowMotion mode)
{
    m_slideshow.setSlideshowMotion(mode);
}


void ImageView::setPanZoomFactor(qreal factor)
{
    m_slideshow.setPanZoomFactor(factor);
}


void ImageView::setSlideshowZoom(SlideshowZoom mode)
{
    m_slideshow.setSlideshowZoom(mode);
}


void ImageView::reapplySlideshowFraming()
{
    m_slideshow.reapplySlideshowFraming();
}


void ImageView::setSlideshowMotionPaused(bool paused)
{
    m_slideshow.setSlideshowMotionPaused(paused);
}


void ImageView::setSlideshowPausedHud(bool on)
{
    m_slideshow.setSlideshowPausedHud(on);
}


void ImageView::cancelSlideshowMotion()
{
    m_slideshow.cancelSlideshowMotion();
}


void ImageView::restoreImageFramingAfterSlideshow()
{
    m_slideshow.restoreImageFramingAfterSlideshow();
}


void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    m_slideshow.setSlideshowPhase(fromPath, toPath, fadeT);
}


void ImageView::setSlideshowNavHot(bool hot)
{
    m_slideshow.setSlideshowNavHot(hot);
}


void ImageView::preloadSlideshowImage(const QString &path)
{
    m_slideshow.preloadSlideshowImage(path);
}

