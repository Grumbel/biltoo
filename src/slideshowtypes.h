// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWTYPES_H
#define SLIDESHOWTYPES_H

#include "displaysurface.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QSet>

#include <memory>

namespace tilelod {
class TileLodController;
}

/**
 * Slideshow framing and transition kinds (settings + pure-phase presenter).
 * Kept out of ImageView so MainWindow / prefs can share the vocabulary.
 */
enum class SlideshowTransition {
    None = 0,
    Crossfade = 1,
    FadeBlack = 2,
    /** Old frame exits left; new frame enters from the right (slide projector). */
    Slide = 3
};

enum class SlideshowMotion {
    Off = 0,
    PanZoom = 1, /**< Cover frame, slowly zoom in while panning */
    PanScan = 2  /**< Cover frame, pan across full width/height (no zoom) */
};

enum class SlideshowZoom {
    Fit = 0,    /**< Letterbox — whole image visible */
    Fill = 1,   /**< Cover — may crop */
    Actual = 2  /**< 1:1 pixels, centred */
};

enum class SlideshowLetterboxFill {
    AppBackground = 0, /**< Preferences canvas colour / checker */
    Solid = 1,         /**< Dedicated slideshow pad colour */
    ZoomBlur = 2       /**< Cover-scale + blur of current slide under sharp image */
};

enum class SlideshowAtlasKind { From, To };

/**
 * Dual-phase composite buffers for pure-clock slideshow paint.
 *
 * From/To paths, rasters, tile sessions, fade/motion clocks. Timers and
 * settings remain on ImageView; this is the paint-side phase state only.
 * Session replace / leave Image mode should call clear().
 */
struct SlideshowPhaseState {
    QString fromPath;
    QString toPath;
    DisplaySurface::SurfaceId fromSurface = DisplaySurface::kInvalidSurfaceId;
    DisplaySurface::SurfaceId toSurface = DisplaySurface::kInvalidSurfaceId;
    QImage fromImage;
    QImage toImage;
    mutable std::unique_ptr<tilelod::TileLodController> fromTiles;
    mutable std::unique_ptr<tilelod::TileLodController> toTiles;
    bool fromContentApplied = false;
    bool toContentApplied = false;
    /** <0 = dwell; [0,1] = transition blend. */
    qreal fadeT = -1.0;
    qreal fromMotionT = 0.0;
    qreal toMotionT = 0.0;
    QPointF toBiasA{-1.0, -1.0};
    QPointF toBiasB{1.0, 1.0};
    QElapsedTimer fromMotionClock;
    QElapsedTimer toMotionClock;
    bool fromMotionClockRunning = false;
    bool toMotionClockRunning = false;

    QSet<QString> rasterInflight;
    QStringList rasterPending;
    quint64 phaseUpgradeGeneration = 0;

    // To-phase atlas (dwell / transition target)
    QPixmap toAtlas;
    quint64 toAtlasRebuildGeneration = 0;
    qreal toAtlasScale = 0.0;
    int toAtlasVw = 0;
    int toAtlasVh = 0;

    bool inTransition() const { return fadeT >= 0.0; }
    bool inDwell() const { return fadeT < 0.0; }

    void clearTiles()
    {
        fromTiles.reset();
        toTiles.reset();
    }

    void clear()
    {
        fromPath.clear();
        toPath.clear();
        fromSurface = DisplaySurface::kInvalidSurfaceId;
        toSurface = DisplaySurface::kInvalidSurfaceId;
        fromImage = {};
        toImage = {};
        clearTiles();
        fromContentApplied = false;
        toContentApplied = false;
        fadeT = -1.0;
        fromMotionT = 0.0;
        toMotionT = 0.0;
        toBiasA = {-1.0, -1.0};
        toBiasB = {1.0, 1.0};
        fromMotionClockRunning = false;
        toMotionClockRunning = false;
        rasterInflight.clear();
        rasterPending.clear();
        phaseUpgradeGeneration = 0;
        toAtlas = {};
        toAtlasRebuildGeneration = 0;
        toAtlasScale = 0.0;
        toAtlasVw = 0;
        toAtlasVh = 0;
    }
};

#endif // SLIDESHOWTYPES_H
