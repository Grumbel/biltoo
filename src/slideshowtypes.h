// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWTYPES_H
#define SLIDESHOWTYPES_H

#include "displaysurface.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QColor>
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


/**
 * User-facing slideshow preferences (transition, framing, Ken Burns mode).
 * Owned by ImageView; MainWindow prefs dialog reads/writes via accessors.
 */
struct SlideshowSettings {
    SlideshowTransition transition = SlideshowTransition::Crossfade;
    int transitionDurationMs = 400;
    SlideshowMotion motion = SlideshowMotion::Off;
    qreal panZoomFactor = 1.12;
    SlideshowZoom zoom = SlideshowZoom::Fit;
    SlideshowLetterboxFill letterboxFill = SlideshowLetterboxFill::AppBackground;
    QColor padColor{42, 42, 42};
};

/**
 * Dwell atlas and Ken Burns camera path (single-slide leg).
 * Motion QTimer stays on ImageView (QObject parent).
 */
struct SlideshowDwellState {
    QImage sourceImage;
    QPixmap atlas;
    quint64 atlasRebuildGeneration = 0;
    qreal atlasScale = 0.0;
    int atlasVw = 0;
    int atlasVh = 0;
    qreal motionT = 0.0;

    bool motionActive = false;
    bool motionPaused = false;

    QPointF biasA{-1.0, -1.0};
    QPointF biasB{1.0, 1.0};
    bool biasValid = false;
    QString biasPath;
    QPointF travelDir{0.0, 1.0};
    qreal motionSign = 1.0;
    int durationMs = 0;
    qint64 elapsedOffsetMs = 0;
    QElapsedTimer clock;

    void clearAtlas()
    {
        atlas = {};
        atlasRebuildGeneration = 0;
        atlasScale = 0.0;
        atlasVw = 0;
        atlasVh = 0;
        sourceImage = {};
    }

    void clearMotionPath()
    {
        motionActive = false;
        motionPaused = false;
        biasValid = false;
        biasPath.clear();
        biasA = {-1.0, -1.0};
        biasB = {1.0, 1.0};
        travelDir = {0.0, 1.0};
        motionSign = 1.0;
        durationMs = 0;
        elapsedOffsetMs = 0;
        motionT = 0.0;
    }

    void clear()
    {
        clearAtlas();
        clearMotionPath();
    }
};


/**
 * ZoomBlur letterbox underlay cache (two slots for from/to during transitions).
 * Async build generation is bumped on every slide change so stale jobs no-op.
 */
struct SlideshowZoomBlurState {
    mutable QPixmap underlay[2];
    mutable qint64 sourceKey[2] = {0, 0};
    mutable int vw = 0;
    mutable int vh = 0;
    /** Last good underlay while a new blur is in flight. */
    mutable QPixmap lastGood;
    mutable qint64 lastGoodKey = 0;
    mutable quint64 generation = 0;
    mutable quint64 inFlightGen[2] = {0, 0};
    mutable qint64 inFlightKey[2] = {0, 0};

    void clear()
    {
        underlay[0] = {};
        underlay[1] = {};
        sourceKey[0] = sourceKey[1] = 0;
        vw = vh = 0;
        lastGood = {};
        lastGoodKey = 0;
        ++generation;
        inFlightGen[0] = inFlightGen[1] = 0;
        inFlightKey[0] = inFlightKey[1] = 0;
    }
};

/**
 * Slideshow progress / seek HUD clocks (pinned HUD + timeline).
 * Progress QTimer stays on ImageView (QObject parent).
 */
struct SlideshowProgressHud {
    bool pausedHud = false;
    bool seekbarVisible = false;
    bool seekDragging = false;
    bool progressActive = false;
    bool progressClockPaused = false;
    qint64 progressBaseMs = 0;
    int progressIntervalMs = 0;
    QElapsedTimer progressElapsed;
    qint64 timelineElapsedMs = 0;
    qreal cycleProgress01 = 0.0;
    bool cycleProgressValid = false;
    qint64 timelineTotalMs = 0;
    bool navHot = false;

    void clearProgress()
    {
        progressActive = false;
        progressClockPaused = false;
        progressBaseMs = 0;
        progressIntervalMs = 0;
        timelineElapsedMs = 0;
        cycleProgress01 = 0.0;
        cycleProgressValid = false;
        timelineTotalMs = 0;
    }
};

#endif // SLIDESHOWTYPES_H
