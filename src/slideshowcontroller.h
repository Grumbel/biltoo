// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWCONTROLLER_H
#define SLIDESHOWCONTROLLER_H

#include "slideshowtypes.h"
#include "motionscrollchrome.h"
#include "displaysurface.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QObject>
#include <QTimer>

class ImageView;
class ImageItem;
class QPainter;

/**
 * Slideshow collaborator for ImageView (Phase 6 Tier 1).
 *
 * Owns pure-phase / dwell / HUD / settings / ZoomBlur / motion-scroll state and
 * the orchestration methods that drive them. ImageView remains the QGraphicsView
 * shell and forwards MainWindow-facing slideshow API to this controller.
 *
 * Friend of ImageView for Tier 1b pure method moves (private host access).
 * A narrow SlideshowHost surface will replace friend once the call surface stabilises.
 */
class SlideshowController : public QObject
{
    Q_OBJECT
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

    /** Mutable cache: non-const even on const controller (like former ImageView::m_ssZoomBlur). */
    SlideshowZoomBlurState &zoomBlur() const { return m_ssZoomBlur; }

    MotionScrollChrome &motionScroll() { return m_motionScroll; }
    const MotionScrollChrome &motionScroll() const { return m_motionScroll; }

    QTimer *&motionTimer() { return m_motionTimer; }
    QTimer *motionTimer() const { return m_motionTimer; }

    QTimer *&progressTimer() { return m_slideshowProgressTimer; }
    QTimer *progressTimer() const { return m_slideshowProgressTimer; }

    QElapsedTimer &lastCenterClick() { return m_lastSlideshowCenterClick; }
    const QElapsedTimer &lastCenterClick() const { return m_lastSlideshowCenterClick; }

    // --- Orchestration (moved from ImageView, Phase 6 Tier 1b) ---
    void setSlideshowPadColor(const QColor &color);
    void setSlideshowLetterboxFill(SlideshowLetterboxFill mode);
    void setSessionPosition(int index, int total, bool pulseIdentity);
    void setSlideshowProgress(bool active, int intervalMs);
    void setSlideshowProgressPaused(bool paused);
    void setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs);
    void setSlideshowCycleProgress(qreal phase01);
    void setSlideshowTransition(SlideshowTransition kind);
    void setSlideshowTransitionDurationMs(int ms);
    QPixmap captureSlideshowFrame() const;
    void cancelSlideshowTransition();
    void setSlideshowMotion(SlideshowMotion mode);
    void setPanZoomFactor(qreal factor);
    void setSlideshowZoom(SlideshowZoom mode);
    void applySlideshowZoomFraming(ImageItem *item);
    void reapplySlideshowFraming();
    void setSlideshowMotionPaused(bool paused);
    void setSlideshowPausedHud(bool on);
    void cancelSlideshowMotion();
    void restoreImageFramingAfterSlideshow();
    bool tryApplyAttentionMotionBiases(uint seed, const QImage &source);
    void applyGeometricMotionBiases(uint seed);
    void pickInterestingMotionBiases(uint seed, const QImage &source);
    void putSlideshowRaster(const QString &path, const QImage &image);
    bool phaseBufferWantsSample(const QString &path, int sampleEdge) const;
    bool snapshotSlideshowContentAppearance(const QString &path, WorkspaceItemState *out) const;
    void finishSlideshowPhaseBufferUpgrade(const QString &path, const QImage &oriented, quint64 generation);
    void scheduleSlideshowPhaseBufferUpgrade(const QString &path, const QImage &image);
    void finishSlideshowAtlas(SlideshowAtlasKind kind, quint64 generation, const QImage &scaled, qreal atlasScale, int atlasVw, int atlasVh);
    void finishDwellAtlasRebuild(quint64 generation, const QImage &scaled, qreal atlasScale, int atlasVw, int atlasVh);
    void requestSlideshowAtlas(SlideshowAtlasKind kind);
    void requestDwellAtlasRebuild();
    void requestToPhaseAtlasRebuild();
    void onSlideshowRasterReady(const QString &path, const QImage &image);
    void bindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id, const QString &path);
    void unbindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id);
    void slideshowPhaseSurfaceTick();
    QString slideshowPrefetchHudLine() const;
    QImage slideshowRaster(const QString &path) const;
    QImage slideshowFullIfReady(const QString &path) const;
    QImage slideshowSoftPlaceholder(const QString &path);
    QImage orientSlideshowImage(const QImage &raw, const QString &path) const;
    QImage slideshowSampleUnoriented(const QString &path) const;
    QImage slideshowPixelsForPath(const QString &path);
    void pruneZoomBlurOutsidePhasePair(const QString &fromPath, const QString &toPath);
    void schedulePhaseZoomBlur(const QString &path, const QImage &image);
    void captureMotionBiasesForPath(const QString &path, const QImage &image, QPointF *outA, QPointF *outB);
    void ensureSlideshowMotionTimer();
    bool shouldPromoteSlideshowToAsFrom(const QString &fromPath) const;
    void promoteSlideshowFromToPhase(const QString &fromPath);
    void startSlideshowFromPhase(const QString &fromPath);
    void prepareSlideshowFromDwell(const QString &fromPath);
    void armSlideshowMotionClock(int pathMs);
    void armSlideshowFromPhase(const QString &fromPath, int pathMs);
    void armSlideshowToPhase(const QString &toPath);
    int slideshowPathDurationMs() const;
    void warmZoomBlurForCurrentPhase();
    bool applySlideshowFadeProgressOnly(qreal fadeT);
    void updateSlideshowPhaseMotionProgress(int pathMs);
    void setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT);
    qreal slideshowMotionHeadroom() const;
    int slideshowTargetEdge() const;
    qreal slideshowZoomBaseScale(const QSize &logical, int vw, int vh) const;
    void setSlideshowNavHot(bool hot);
    void pumpSlideshowPreloadQueue();
    void finishSlideshowPreload(const QString &path, const QImage &image);
    void preloadSlideshowImage(const QString &path);
    void invalidateDwellAtlasRebuilds();
    void ensureMotionAtlas(const QImage &image, QPixmap *atlas, qreal *atlasScale, int *atlasVw, int *atlasVh) const;
    void setSlideshowUnderlayVisible(bool visible);
    void hideSlideshowUnderlay();
    void clearSlideshowZoomBlurSlots();
    void invalidateZoomBlurQueue() const;
    bool zoomBlurKeyCached(qint64 key) const;
    bool zoomBlurKeyInFlight(qint64 key) const;
    int claimZoomBlurFlightSlot(qint64 key) const;
    void installZoomBlurResult(const QImage &blurred, qint64 key, quint64 gen);
    void scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const;
    void paintZoomBlurUnderlay(QPainter *painter, const QImage &image, const QRect &viewportRect, qint64 stableKey) const;
    QSize resolveMotionLogicalSize(const QString &path) const;
    QRectF computeMotionCoverDestRect(qreal iw, qreal ih, int vw, int vh, qreal motionT, QPointF biasA, QPointF biasB, const QString &path) const;
    bool paintSlideshowTiles(QPainter *painter, const QString &path, const QRectF &dest, const QImage &underlay) const;
    void paintMotionCover(QPainter *painter, const QImage &image, qreal motionT, QPointF biasA, QPointF biasB, const QString &path) const;
    QPixmap renderMotionCoverPixmap(const QImage &image, qreal motionT, uint pathHash) const;
    void maybeStartSlideshowMotion();
    bool prepareSlideshowMotionDwell(ImageItem *item);
    void freezeScrollbarsForMotion();
    void resetItemPlacementForMotion(ImageItem *item);
    void armMotionBiasForPath(ImageItem *item, const QString &path);
    void retargetSlideshowMotionDuration(int durationMs);
    void startSlideshowMotion(int durationMs, qreal initialProgress);
    void tickSlideshowPhaseMotionClocks();
    void tickSlideshowDwellMotionClock();
    void tickSlideshowMotion();
    QString sessionBadgeText() const;

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
