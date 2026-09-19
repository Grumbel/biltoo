// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWTYPES_H
#define SLIDESHOWTYPES_H

#include "displaysurface.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QtGlobal>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QSet>

#include <memory>

#include "tilelod/tile_lod_controller.hpp"

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

    /**
     * Transition blend in [0,1], or negative for dwell.
     * Negative values become dwell (fadeT < 0); non-negative are clamped to [0,1].
     */
    void setFadeBlend(qreal t)
    {
        fadeT = (t < 0.0) ? -1.0 : qBound(0.0, t, 1.0);
    }

    /** End transition; dwell until next fade starts. */
    void beginDwell() { fadeT = -1.0; }

    qreal fromMotionT = 0.0;
    qreal toMotionT = 0.0;

    void setFromMotionT(qreal t) { fromMotionT = qBound(0.0, t, 1.0); }

    void setToMotionT(qreal t) { toMotionT = qBound(0.0, t, 1.0); }

    void setFromPath(const QString &path) { fromPath = path; }

    void setToPath(const QString &path) { toPath = path; }

    void clearFromPath() { fromPath.clear(); }

    void clearToPath() { toPath.clear(); }

    void setFromMotionClockRunning(bool on) { fromMotionClockRunning = on; }

    void setToMotionClockRunning(bool on) { toMotionClockRunning = on; }

    /** Start from-phase motion clock at T=0 (or keep T if already set). */
    void startFromMotionClock()
    {
        fromMotionClock.start();
        fromMotionClockRunning = true;
    }

    void startToMotionClock()
    {
        toMotionClock.start();
        toMotionClockRunning = true;
    }

    /** Promote to-phase motion clock/T into from (after fade lands on B). */
    void promoteFromMotionFromTo()
    {
        fromMotionClock = toMotionClock;
        fromMotionClockRunning = true;
        setFromMotionT(toMotionT);
    }

    tilelod::TileLodController *ensureFromTiles() const
    {
        if (!fromTiles) {
            fromTiles = std::make_unique<tilelod::TileLodController>();
        }
        return fromTiles.get();
    }

    tilelod::TileLodController *ensureToTiles() const
    {
        if (!toTiles) {
            toTiles = std::make_unique<tilelod::TileLodController>();
        }
        return toTiles.get();
    }

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

    bool hasToAtlas() const { return !toAtlas.isNull(); }

    qreal clampedFadeT() const
    {
        return fadeT < 0.0 ? 0.0 : qBound(0.0, fadeT, 1.0);
    }

    void setFromImage(const QImage &img, bool contentApplied)
    {
        fromImage = img;
        fromContentApplied = contentApplied;
    }

    void setToImage(const QImage &img, bool contentApplied)
    {
        toImage = img;
        toContentApplied = contentApplied;
    }

    void clearFromImage()
    {
        fromImage = {};
        fromContentApplied = false;
    }

    bool hasFromImage() const { return !fromImage.isNull(); }

    bool hasToImage() const { return !toImage.isNull(); }

    bool isFromContentApplied() const { return fromContentApplied; }

    bool isToContentApplied() const { return toContentApplied; }

    bool isFromMotionClockRunning() const { return fromMotionClockRunning; }

    bool isToMotionClockRunning() const { return toMotionClockRunning; }

    bool isFromPath(const QString &path) const
    {
        return !path.isEmpty() && path == fromPath;
    }

    bool isToPath(const QString &path) const
    {
        return !path.isEmpty() && path == toPath;
    }

    bool isPhasePath(const QString &path) const
    {
        return isFromPath(path) || isToPath(path);
    }

    bool hasFromPath() const { return !fromPath.isEmpty(); }

    bool hasToPath() const { return !toPath.isEmpty(); }

    const QPointF &toBiasAPoint() const { return toBiasA; }

    const QPointF &toBiasBPoint() const { return toBiasB; }

    QPointF &toBiasARef() { return toBiasA; }

    QPointF &toBiasBRef() { return toBiasB; }

    void clearToImage()
    {
        toImage = {};
        toContentApplied = false;
    }

    void setToAtlas(const QPixmap &pm, qreal scale, int vw, int vh, quint64 gen = 0)
    {
        toAtlas = pm;
        toAtlasScale = scale;
        toAtlasVw = vw;
        toAtlasVh = vh;
        if (gen != 0) {
            toAtlasRebuildGeneration = gen;
        }
    }

    void clearToAtlas()
    {
        toAtlas = {};
        toAtlasRebuildGeneration = 0;
        toAtlasScale = 0.0;
        toAtlasVw = 0;
        toAtlasVh = 0;
    }

    /** Bump to-atlas rebuild generation. @return new generation. */
    quint64 bumpToAtlasRebuildGeneration() { return ++toAtlasRebuildGeneration; }

    void stopMotionClocks()
    {
        fromMotionClockRunning = false;
        toMotionClockRunning = false;
        setFromMotionT(0.0);
        setToMotionT(0.0);
    }

    void clearRasterQueues()
    {
        rasterInflight.clear();
        rasterPending.clear();
    }

    bool rasterInflightContains(const QString &path) const
    {
        return rasterInflight.contains(path);
    }

    bool rasterPendingContains(const QString &path) const
    {
        return rasterPending.contains(path);
    }

    void removeRasterInflight(const QString &path) { rasterInflight.remove(path); }

    int rasterQueueCount() const
    {
        return rasterInflight.size() + rasterPending.size();
    }

    bool takeNextRasterPending(QString *out)
    {
        if (!out || rasterPending.isEmpty()) {
            return false;
        }
        *out = rasterPending.takeFirst();
        return true;
    }

    /** Bump generation so in-flight phase upgrades no-op. @return new generation. */
    quint64 bumpPhaseUpgradeGeneration() { return ++phaseUpgradeGeneration; }

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
        clearFromImage();
        clearToImage();
        clearTiles();
        beginDwell();
        stopMotionClocks();
        toBiasA = {-1.0, -1.0};
        toBiasB = {1.0, 1.0};
        clearRasterQueues();
        phaseUpgradeGeneration = 0;
        clearToAtlas();
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

    static qreal clampPanZoomFactor(qreal factor)
    {
        return qBound(1.02, factor, 1.5);
    }

    /** @return true when the clamped pan-zoom factor changed. */
    bool setPanZoomFactor(qreal factor)
    {
        const qreal next = clampPanZoomFactor(factor);
        if (qFuzzyCompare(panZoomFactor, next)) {
            return false;
        }
        panZoomFactor = next;
        return true;
    }

    /** @return true when transition duration changed. */
    bool setTransitionDurationMs(int ms)
    {
        const int next = qMax(0, ms);
        if (transitionDurationMs == next) {
            return false;
        }
        transitionDurationMs = next;
        return true;
    }

    bool setPadColor(const QColor &c)
    {
        if (!c.isValid() || c == padColor) {
            return false;
        }
        padColor = c;
        return true;
    }

    bool setLetterboxFill(SlideshowLetterboxFill mode)
    {
        if (letterboxFill == mode) {
            return false;
        }
        letterboxFill = mode;
        return true;
    }

    bool setTransition(SlideshowTransition kind)
    {
        if (transition == kind) {
            return false;
        }
        transition = kind;
        return true;
    }

    bool setMotion(SlideshowMotion mode)
    {
        if (motion == mode) {
            return false;
        }
        motion = mode;
        return true;
    }

    bool isMotionOff() const { return motion == SlideshowMotion::Off; }

    bool isZoomBlurLetterbox() const
    {
        return letterboxFill == SlideshowLetterboxFill::ZoomBlur;
    }

    bool isZoomFill() const { return zoom == SlideshowZoom::Fill; }

    bool setZoom(SlideshowZoom mode)
    {
        if (zoom == mode) {
            return false;
        }
        zoom = mode;
        return true;
    }
};

/**
 * Viewport-derived budget for slideshow motion atlases.
 * ImageView fills this from viewport size × motion headroom; pure policy
 * (SlideshowAtlasPolicy) only reads the fields.
 */
struct DwellAtlasParams {
    int vw = 0;
    int vh = 0;
    int longCap = 0;
    qreal headroom = 0.0;
    qreal keyScale = 0.0;
    bool valid = false;
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

    void setMotionT(qreal t) { motionT = qBound(0.0, t, 1.0); }

    bool motionActive = false;
    bool motionPaused = false;

    /** @return true when motion-active flag changed. */
    bool setMotionActive(bool on)
    {
        if (motionActive == on) {
            return false;
        }
        motionActive = on;
        return true;
    }

    bool isMotionActive() const { return motionActive; }

    bool isMotionPaused() const { return motionPaused; }

    bool hasBias() const { return biasValid; }

    bool hasDuration() const { return durationMs > 0; }

    const QPointF &biasAPoint() const { return biasA; }

    const QPointF &biasBPoint() const { return biasB; }

    const QPointF &travelDirPoint() const { return travelDir; }

    qreal motionSignValue() const { return motionSign; }

    bool hasSourceImage() const { return !sourceImage.isNull(); }

    bool hasAtlas() const { return !atlas.isNull(); }

    /** @return true when motion-paused flag changed. */
    bool setMotionPaused(bool on)
    {
        if (motionPaused == on) {
            return false;
        }
        motionPaused = on;
        return true;
    }

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

    /** Drop atlas pixmap only; keep sourceImage for rebuild. */
    void clearAtlasPixmap()
    {
        atlas = {};
        atlasRebuildGeneration = 0;
        atlasScale = 0.0;
        atlasVw = 0;
        atlasVh = 0;
    }

    /** Force next tick to rebuild atlas at current viewport size. */
    void invalidateAtlasViewport() { atlasVw = 0; }

    void setAtlas(const QPixmap &pm, qreal scale, int vw, int vh, quint64 gen = 0)
    {
        atlas = pm;
        atlasScale = scale;
        atlasVw = vw;
        atlasVh = vh;
        if (gen != 0) {
            atlasRebuildGeneration = gen;
        }
    }

    /** Bump dwell atlas rebuild generation. @return new generation. */
    quint64 bumpAtlasRebuildGeneration() { return ++atlasRebuildGeneration; }

    void setSourceImage(const QImage &img) { sourceImage = img; }

    void setElapsedOffsetMs(qint64 ms) { elapsedOffsetMs = ms; }

    void setDurationMs(int ms) { durationMs = qMax(0, ms); }

    void applyBias(const QPointF &a, const QPointF &b, const QPointF &dir, qreal sign)
    {
        biasA = a;
        biasB = b;
        travelDir = dir;
        motionSign = sign;
        biasValid = true;
    }

    void clearBias()
    {
        biasValid = false;
        biasPath.clear();
        biasA = {-1.0, -1.0};
        biasB = {1.0, 1.0};
        travelDir = {0.0, 1.0};
        motionSign = 1.0;
    }

    void setBiasPath(const QString &path) { biasPath = path; }

    void clearMotionPath()
    {
        motionActive = false;
        motionPaused = false;
        clearBias();
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
        bumpGeneration();
        inFlightGen[0] = inFlightGen[1] = 0;
        inFlightKey[0] = inFlightKey[1] = 0;
    }

    void clearUnderlays()
    {
        underlay[0] = {};
        underlay[1] = {};
        sourceKey[0] = sourceKey[1] = 0;
    }

    void setViewportSize(int w, int h)
    {
        vw = w;
        vh = h;
    }

    void bumpGeneration() { ++generation; }

    /** Slot with cached underlay for @p key, or -1. */
    int findCachedSlot(qint64 key) const
    {
        for (int i = 0; i < 2; ++i) {
            if (sourceKey[i] == key && !underlay[i].isNull()) {
                return i;
            }
        }
        return -1;
    }

    const QPixmap &underlayAt(int slot) const { return underlay[slot]; }

    bool hasLastGood() const { return !lastGood.isNull(); }

    const QPixmap &lastGoodPixmap() const { return lastGood; }

    bool viewportMatches(int w, int h) const { return vw == w && vh == h; }
};

/**
 * Slideshow progress / seek HUD clocks (pinned HUD + timeline).
 * Progress QTimer stays on ImageView (QObject parent).
 */
struct SlideshowProgressHud {
    /** Progress bar paint tick while slideshow is running (~30 Hz). */
    static constexpr int kProgressTickMs = 33;
    /** Ken Burns / phase motion QTimer interval (~60 Hz). */
    static constexpr int kMotionTickMs = 16;

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
    /** Last [slideshow-paint] fingerprint; skip duplicate debug logs. */
    QString lastPaintFp;

    void setCycleProgress01(qreal phase01)
    {
        cycleProgress01 = qBound(0.0, phase01, 1.0);
        cycleProgressValid = true;
    }

    void setProgressIntervalMs(int ms)
    {
        progressIntervalMs = qMax(0, ms);
    }

    void setTimelineProgress(qint64 elapsedMs, qint64 totalMs)
    {
        timelineTotalMs = totalMs;
        timelineElapsedMs = qBound(qint64(0), elapsedMs, totalMs);
    }

    static qint64 clampElapsedMs(qint64 elapsedMs, qint64 totalMs)
    {
        return qBound(qint64(0), elapsedMs, totalMs);
    }

    bool isSeekHit(int y, int viewportHeight, int edgePx = 48) const
    {
        return progressActive && viewportHeight > 0
            && y >= viewportHeight - edgePx;
    }

    bool setPausedHud(bool on)
    {
        if (pausedHud == on) {
            return false;
        }
        pausedHud = on;
        return true;
    }

    bool setNavHot(bool on)
    {
        if (navHot == on) {
            return false;
        }
        navHot = on;
        return true;
    }

    bool setProgressActive(bool on)
    {
        if (progressActive == on) {
            return false;
        }
        progressActive = on;
        return true;
    }

    bool isProgressActive() const { return progressActive; }

    bool isNavHot() const { return navHot; }

    bool isPausedHud() const { return pausedHud; }

    bool isSeekDragging() const { return seekDragging; }

    bool isSeekbarVisible() const { return seekbarVisible; }

    bool setSeekbarVisible(bool on)
    {
        if (seekbarVisible == on) {
            return false;
        }
        seekbarVisible = on;
        return true;
    }

    bool setSeekDragging(bool on)
    {
        if (seekDragging == on) {
            return false;
        }
        seekDragging = on;
        return true;
    }

    bool setProgressClockPaused(bool on)
    {
        if (progressClockPaused == on) {
            return false;
        }
        progressClockPaused = on;
        return true;
    }

    void resetProgressClock()
    {
        progressBaseMs = 0;
        progressClockPaused = false;
        progressElapsed.start();
    }

    /** Fold progressElapsed into progressBaseMs (pause / stop). */
    void accumulateProgressBaseFromElapsed()
    {
        if (progressElapsed.isValid()) {
            progressBaseMs += progressElapsed.elapsed();
        }
    }

    void clearTimeline()
    {
        timelineElapsedMs = 0;
        timelineTotalMs = 0;
    }

    void clearPaintFingerprint() { lastPaintFp.clear(); }

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
        lastPaintFp.clear();
        seekDragging = false;
        seekbarVisible = false;
    }
};

#endif // SLIDESHOWTYPES_H
