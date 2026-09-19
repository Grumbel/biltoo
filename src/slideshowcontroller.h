// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWCONTROLLER_H
#define SLIDESHOWCONTROLLER_H

#include "slideshowtypes.h"
#include "motionscrollchrome.h"

#include <QElapsedTimer>
#include <QTimer>

class ImageView;

/**
 * Slideshow collaborator for ImageView (Phase 6 Tier 1).
 *
 * Owns pure-phase / dwell / HUD / settings / ZoomBlur / motion-scroll state that
 * previously lived as ImageView::m_ss*. Behaviour methods still run on ImageView
 * in this tip (Tier 1a — state ownership); they will move onto this type once
 * the SlideshowHost surface is widened (Tier 1b).
 *
 * ImageView remains the QGraphicsView shell and the public MainWindow-facing API.
 */
class SlideshowController
{
public:
    explicit SlideshowController(ImageView *view);

    ImageView *view() const { return m_view; }

    SlideshowPhaseState &phase() { return m_ss; }
    const SlideshowPhaseState &phase() const { return m_ss; }

    SlideshowDwellState &dwell() { return m_ssDwell; }
    const SlideshowDwellState &dwell() const { return m_ssDwell; }

    SlideshowProgressHud &hud() { return m_ssHud; }
    const SlideshowProgressHud &hud() const { return m_ssHud; }

    SlideshowSettings &settings() { return m_ssSettings; }
    const SlideshowSettings &settings() const { return m_ssSettings; }

    SlideshowZoomBlurState &zoomBlur() { return m_ssZoomBlur; }
    const SlideshowZoomBlurState &zoomBlur() const { return m_ssZoomBlur; }

    MotionScrollChrome &motionScroll() { return m_motionScroll; }
    const MotionScrollChrome &motionScroll() const { return m_motionScroll; }

    QTimer *&motionTimer() { return m_motionTimer; }
    QTimer *motionTimer() const { return m_motionTimer; }

    QTimer *&progressTimer() { return m_slideshowProgressTimer; }
    QTimer *progressTimer() const { return m_slideshowProgressTimer; }

    QElapsedTimer &lastCenterClick() { return m_lastSlideshowCenterClick; }
    const QElapsedTimer &lastCenterClick() const { return m_lastSlideshowCenterClick; }

private:
    ImageView *m_view = nullptr; // not owned
    SlideshowProgressHud m_ssHud;
    SlideshowSettings m_ssSettings;
    SlideshowPhaseState m_ss;
    SlideshowDwellState m_ssDwell;
    mutable SlideshowZoomBlurState m_ssZoomBlur;
    MotionScrollChrome m_motionScroll;
    QTimer *m_motionTimer = nullptr;
    QTimer *m_slideshowProgressTimer = nullptr;
    QElapsedTimer m_lastSlideshowCenterClick;
};

#endif // SLIDESHOWCONTROLLER_H
