// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshow/slideshowcontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "gallery/gallerylayout.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "content/contentxform.h"
#include "display/displayquality.h"
#include "display/pathrasterservice.h"
#include "view/viewtransform.h"
#include "slideshow/zoomblurhelpers.h"
#include "slideshow/slideshowclocks.h"
#include "slideshow/slideshowphasepolicy.h"
#include "slideshow/slideshowatlaspolicy.h"
#include "slideshow/slideshowmotiongeometry.h"
#include "attention/attentiongeometry.h"
#include "tilelod/tile_lod_controller.hpp"
#include "tilelod/tile_cover_paint.hpp"

#include <QGraphicsScene>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QPointer>
#include <QThreadPool>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cstdlib>
#include <QFileInfo>
#include <QMouseEvent>
#include "util/biltoo_logging.h"
#include "hud/hudmodel.h"
#include "host/imageloader.h"
#include <QGraphicsItem>
#include "host/thumtoocache.h"


SlideshowController::SlideshowController(ImageView *view)
    : QObject(view)
    , m_view(view)
{
}


void SlideshowController::setSlideshowPadColor(const QColor &color)
{
    if (!settings().setPadColor(color)) {
        return;
    }
    clearSlideshowZoomBlurSlots();
    if (hud().isProgressActive() && m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::setSlideshowLetterboxFill(SlideshowLetterboxFill mode)
{
    if (!settings().setLetterboxFill(mode)) {
        return;
    }
    clearSlideshowZoomBlurSlots();
    if (hud().isProgressActive() && m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::setSessionPosition(int index, int total, bool pulseIdentity)
{
    const bool changed = m_view->hostSessionId().setPosition(index, total);
    // Pulse only when the session cursor actually moves (user Next/Prev, etc.).
    // Do not pulse on every statusChanged while total > 0 (AUDIT H7).
    // Slideshow auto-advance passes pulseIdentity=false.
    if (pulseIdentity && changed) {
        m_view->hostHudFlash().setIdentityPulse(true);
        if (m_view->hostHudFlashTimer()) {
            m_view->hostHudFlashTimer()->start(HudFlash::kIdentityPulseMs);
        }
    }
    if (!(changed || m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isVisible() || m_view->hostHudFlash().isIdentityPulse()
          || hud().isPausedHud())) {
        return;
    }
    // Gallery selection already invalidates the tile; a full m_view->viewport()->update()
    // here forced every image through the GL path and felt like lag on click.
    if (m_view->isGalleryMode() && !m_view->hostHudPrefs().isVisible() && !m_view->hostHudFlash().isIdentityPulse()) {
        return;
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::setSlideshowProgress(bool active, int intervalMs)
{
    // Speed / interval edit while the show is already running: only update the
    // interval. Do not restart the progress clock or clear phase buffers —
    // that produced HUD and paint blips on every [/] or settings change.
    if (active && hud().isProgressActive()) {
        hud().setProgressIntervalMs(intervalMs);
        if (hud().hasProgressInterval() && progressTimer()
            && !hud().isProgressClockPaused()) {
            progressTimer()->start();
        } else if (progressTimer() && !hud().hasProgressInterval()) {
            progressTimer()->stop();
        }
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }

    hud().setProgressActive(active);
    hud().setProgressIntervalMs(active ? intervalMs : 0);
    if (active) {
        // Pure phase owns the viewport — never flash the underlay item.
        hideSlideshowUnderlay();
        hud().resetProgressClock();
        if (hud().hasProgressInterval() && progressTimer()) {
            progressTimer()->start();
        } else if (progressTimer()) {
            progressTimer()->stop();
        }
    } else if (progressTimer()) {
        progressTimer()->stop();
        hud().resetProgressClock();
    }
    if (!active) {
        cancelSlideshowMotion();
        dwell().clearBias();
        hud().clearTimeline();
        hud().clearPaintFingerprint();
        phase().clearFromPath();
        phase().clearToPath();
        unbindSlideshowPhaseSurface(&phase().fromSurfaceRef());
        unbindSlideshowPhaseSurface(&phase().toSurfaceRef());
        phase().clearFromImage();
        phase().clearToImage();
        phase().beginDwell();
        phase().stopMotionClocks();
        // Rasters live in ImageCache — do not clear the host map on stop.
        phase().clearRasterQueues();
    }
    m_view->viewport()->update();
}


void SlideshowController::setSlideshowProgressPaused(bool paused)
{
    if (paused == hud().isProgressClockPaused()) {
        return;
    }
    if (paused) {
        if (hud().isProgressElapsedValid()) {
            hud().accumulateProgressBaseFromElapsed();
        }
        hud().setProgressClockPaused(true);
        if (progressTimer()) {
            progressTimer()->stop();
        }
    } else {
        hud().setProgressClockPaused(false);
        hud().startProgressElapsed();
        if (hud().isProgressActive() && hud().hasProgressInterval()
            && progressTimer()) {
            progressTimer()->start();
        }
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs)
{
    if (totalMs <= 0) {
        if (!hud().hasTimelineTotal()) {
            return;
        }
        hud().clearTimeline();
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    elapsedMs = SlideshowProgressHud::clampElapsedMs(elapsedMs, totalMs);
    if (elapsedMs == hud().timelineElapsed()
        && totalMs == hud().timelineTotal()) {
        return;
    }
    hud().setTimelineProgress(elapsedMs, totalMs);
    // Progress bar needs sub-second updates while the extended HUD is pinned.
    if (m_view->hostHudPrefs().isVisible() && m_view->viewport()) {
        m_view->viewport()->update();
    }
}



void SlideshowController::setSlideshowCycleProgress(qreal phase01)
{
    hud().setCycleProgress01(phase01);
}



void SlideshowController::setSlideshowTransition(SlideshowTransition kind)
{
    if (!settings().setTransition(kind)) {
        return;
    }
    if (kind == SlideshowTransition::None) {
        cancelSlideshowTransition();
    }
}


void SlideshowController::setSlideshowTransitionDurationMs(int ms)
{
    settings().setTransitionDurationMs(ms);
}




void SlideshowController::cancelSlideshowTransition()
{
    // Pure phase has no overlay to cancel; keep the hook for callers that
    // clear transition intent on user nav / stop / pause.
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::setSlideshowMotion(SlideshowMotion mode)
{
    if (!settings().setMotion(mode)) {
        return;
    }
    if (mode == SlideshowMotion::Off) {
        cancelSlideshowMotion();
        if (hud().isProgressActive()) {
            reapplySlideshowFraming();
        }
    } else if (hud().isProgressActive()) {
        reapplySlideshowFraming();
    }
}


void SlideshowController::setPanZoomFactor(qreal factor)
{
    settings().setPanZoomFactor(factor);
}


void SlideshowController::setSlideshowZoom(SlideshowZoom mode)
{
    if (!settings().setZoom(mode)) {
        return;
    }
    // Zoom is the base scale for Ken Burns as well as static framing.
    if (hud().isProgressActive()) {
        reapplySlideshowFraming();
    }
}


void SlideshowController::applySlideshowZoomFraming(ImageItem *item)
{
    if (!item || !m_view->viewport()) {
        return;
    }
    // Explicit uniform scale for static slideshow framing — logical size owns
    // geometry (same model as paintMotionCover), not soft contentRect pixels.
    {
        ItemComponents::Placement pl = item->placement();
        pl.shear = 0.0;
        pl.rotation = 0.0;
        pl.scale = 1.0;
        pl.scaleY = 1.0;
        if (m_view->isImageMode()) {
            pl.pos = QPointF(0, 0);
        }
        GalleryLayout::applyItemPlacement(item, pl);
    }
    const QString path = item->path();
    QSize logical = m_view->ensureLogicalSizeForPath(path);
    if (isPositiveSize(logical) && !m_view->hostSizeBook().isProvisional(path)) {
        // File-native → ContentXform layout when phase orient is applied (same
        // rule as resolveMotionLogicalSize / paintMotionCover).
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(path, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            const QSize lay = ContentXform::layoutSize(logical, app);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                logical = lay;
            }
        }
        m_view->hostDisplayPipeline().hostSetIntrinsicSize(item, logical);
    }
    // Provisional / unknown: leave intrinsic alone — LQIP/soft must not set geometry.
    const QRectF content = item->contentRect();
    if (content.width() < 1.0 || content.height() < 1.0) {
        return;
    }
    // Scale from logical size when known; contentRect only for scene mid-point.
    const qreal vw = qreal(ViewTransform::atLeast1(m_view->viewport()->width()));
    const qreal vh = qreal(ViewTransform::atLeast1(m_view->viewport()->height()));
    const QPointF mid = item->mapToScene(content.center());

    if (!isPositiveSize(logical)) {
        logical = QSize(int(content.width()), int(content.height()));
    }
    qreal scale = slideshowZoomBaseScale(logical, int(vw), int(vh));
    switch (settings().currentZoom()) {
    case SlideshowZoom::Fill:
        m_view->hostFraming().setFitFillFlags(false, true);
        break;
    case SlideshowZoom::Actual:
        m_view->hostFraming().clearFitFill();
        break;
    case SlideshowZoom::Fit:
    default:
        m_view->hostFraming().setFitOnly();
        break;
    }
    if (scale <= 0.0 || !qIsFinite(scale)) {
        return;
    }
    const qreal vx = qreal(m_view->viewport()->width()) * 0.5;
    const qreal vy = qreal(m_view->viewport()->height()) * 0.5;
    m_view->setTransformationAnchor(QGraphicsView::NoAnchor);
    m_view->setResizeAnchor(QGraphicsView::NoAnchor);
    QTransform xform;
    xform.translate(vx, vy);
    xform.scale(scale, scale);
    xform.translate(-mid.x(), -mid.y());
    m_view->setTransform(xform);
    if (m_view->horizontalScrollBar()) {
        m_view->horizontalScrollBar()->setValue(0);
    }
    if (m_view->verticalScrollBar()) {
        m_view->verticalScrollBar()->setValue(0);
    }
}


void SlideshowController::reapplySlideshowFraming()
{
    if (!hud().isProgressActive() || !m_view->isImageMode()) {
        return;
    }
    ImageItem *item = m_view->targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    if (!settings().isMotionOff()) {
        int duration = hud().progressInterval();
        if (duration < 250) {
            duration = 3000;
        }
        // Already in motion (typical interval edit): retarget duration and keep
        // normalized progress + dwell atlas. startSlideshowMotion cancels first
        // and clears the atlas — that is the speed-change flash.
        if (dwell().isMotionActive()) {
            retargetSlideshowMotionDuration(duration);
        } else {
            startSlideshowMotion(duration);
        }
    } else {
        cancelSlideshowMotion();
        applySlideshowZoomFraming(item);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        emit m_view->statusChanged();
    }
}


void SlideshowController::setSlideshowMotionPaused(bool paused)
{
    if (paused == dwell().isMotionPaused()) {
        return;
    }
    if (paused) {
        if (dwell().isMotionActive() && motionTimer() && motionTimer()->isActive()) {
            // Fold wall into unitless dwell progress, then freeze.
            if (dwell().hasDuration() && dwell().isClockValid()) {
                const qint64 d = dwell().clockElapsed();
                if (d > 0) {
                    const qreal t = qreal(dwell().elapsedOffsetMsValue() + d)
                        / qreal(dwell().durationMsValue());
                    dwell().setMotionT(t);
                    dwell().setElapsedOffsetMs(
                        qint64(dwell().motionTValue() * qreal(dwell().durationMsValue())));
                }
            }
            motionTimer()->stop();
        }
        // Fold phase-motion clocks into T ∈ [0,1].
        const int pathMs = slideshowPathDurationMs();
        if (pathMs > 0) {
            SlideshowClocks::integrateMotionProgress01(&phase().fromMotionTRef(), &phase().fromMotionClockMutable(),
                                      phase().isFromMotionClockRunning(), false, pathMs);
            SlideshowClocks::integrateMotionProgress01(&phase().toMotionTRef(), &phase().toMotionClockMutable(),
                                      phase().isToMotionClockRunning(), false, pathMs);
            if (phase().isFromMotionClockRunning()) {
                dwell().setMotionT(phase().fromMotionTValue());
            }
        }
        dwell().setMotionPaused(true);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    dwell().setMotionPaused(false);
    if (phase().isFromMotionClockRunning()) {
        phase().startFromMotionClock();
    }
    if (phase().isToMotionClockRunning()) {
        phase().startToMotionClock();
    }
    if (dwell().isMotionActive() && motionTimer() && dwell().hasDuration()) {
        dwell().restartClock();
        motionTimer()->start();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}



void SlideshowController::setSlideshowPausedHud(bool on)
{
    if (!hud().setPausedHud(on)) {
        return;
    }
    if (on) {
        // Keep a stable action line for the permanent cue; flash timer must
        // not clear it (paint draws paused HUD independently of flash).
        m_view->hostHudFlash().setPausedLabel(tr("❚❚  Paused"));
        if (m_view->hostHudFlashTimer()) {
            m_view->hostHudFlashTimer()->stop();
        }
    } else if (m_view->hostHudFlash().actionText().contains(QStringLiteral("Paused"))) {
        m_view->hostHudFlash().clearAction();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::cancelSlideshowMotion()
{
    const bool wasMotion = dwell().isMotionActive();
    dwell().setMotionActive(false);
    dwell().setMotionPaused(false);
    {
        Qt::ScrollBarPolicy h = Qt::ScrollBarAsNeeded;
        Qt::ScrollBarPolicy v = Qt::ScrollBarAsNeeded;
        if (motionScroll().release(&h, &v)) {
            // freezeScrollbars may have saved Gallery AsNeeded from before the
            // session was marked running. Restoring that mid-show brings bars back.
            if (hud().isProgressActive()) {
                m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            } else {
                m_view->setHorizontalScrollBarPolicy(h);
                m_view->setVerticalScrollBarPolicy(v);
            }
        }
    }
    setSlideshowUnderlayVisible(true);
    dwell().clearAtlasPixmap();
    dwell().setElapsedOffsetMs(0);
    if (motionTimer()) {
        motionTimer()->stop();
    }
    // Ken Burns only moved the overlay blit. Bring the underlay back in line
    // with static slideshow framing while the show is still running.
    if (wasMotion && hud().isProgressActive() && m_view->isImageMode()) {
        if (ImageItem *item = m_view->targetItem()) {
            applySlideshowZoomFraming(item);
        }
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::restoreImageFramingAfterSlideshow()
{
    // Leave slideshow Fill/Actual flags and camera from applySlideshowZoomFraming
    // so Image mode is normal Fit-to-window again.
    if (!m_view->isImageMode()) {
        return;
    }
    ImageItem *item = m_view->targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    m_view->hostFraming().setFitOnly();
    m_view->fitItem(item, Qt::KeepAspectRatio);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
}


bool SlideshowController::tryApplyAttentionMotionBiases(uint seed, const QImage &source)
{
    QPointF att01;
    bool haveAtt = false;
    if (ImageItem *item = m_view->targetItem()) {
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            if (m_view->itemWorld().hasDurableAppearance(sid)) {
                const WorkspaceItemState st = m_view->sessionAppearanceValue(sid);
                if (st.hasAttention) {
                    att01 = st.attentionNorm;
                    haveAtt = true;
                }
            }
        }
    }
    if (!haveAtt && !source.isNull() && ImageLoader::attentionPoint(source, &att01)) {
        haveAtt = true;
    }
    if (!haveAtt) {
        return false;
    }
    SlideshowMotionGeometry::BiasPath path;
    if (!SlideshowMotionGeometry::attentionBiasPath(att01, seed, &path)) {
        return false;
    }
    dwell().applyBias(path.a, path.b, path.travelDir, path.motionSign);
    return true;
}


void SlideshowController::applyGeometricMotionBiases(uint seed)
{
    const SlideshowMotionGeometry::BiasPath path =
        SlideshowMotionGeometry::geometricBiasPath(seed);
    dwell().applyBias(path.a, path.b, path.travelDir, path.motionSign);
}


void SlideshowController::pickInterestingMotionBiases(uint seed, const QImage &source)
{
    // Prefer content-aware centres (libvips attention) when the decoded frame
    // is available; otherwise path-hash corners/edges as before.
    if (seed == 0) {
        seed = 1;
    }
    if (tryApplyAttentionMotionBiases(seed, source)) {
        return;
    }
    applyGeometricMotionBiases(seed);
}




// ---------------------------------------------------------------------------
// Slideshow size model
//
//   logical size  = identity of the image (probe / full decode / thumtoo)
//   sample raster = soft or target-edge pixels used only for sampling
//
// Camera (fit/fill/actual/Ken Burns) always uses logical size.
// putSlideshowRaster / preload never write sample dimensions into the size map.
// ---------------------------------------------------------------------------


void SlideshowController::putSlideshowRaster(const QString &path, const QImage &image)
{
    // Thin wrapper: host ImageCache is the only path→raster store.
    ImageCache::put(path, image);
}


bool SlideshowController::phaseBufferWantsSample(const QString &path, int sampleEdge) const
{
    if (sampleEdge <= 0 || path.isEmpty()) {
        return false;
    }
    // Appearance presence is resolved on the GUI; pure size policy is shared.
    WorkspaceItemState app;
    const bool pendingContent = snapshotSlideshowContentAppearance(path, &app)
        && SessionAppearance::hasContentAppearance(app);
    return SlideshowPhasePolicy::bufferWantsSample(
               phase().fromPathRef(), phase().fromImageRef(), phase().isFromContentApplied(), path,
               sampleEdge, pendingContent)
        || SlideshowPhasePolicy::bufferWantsSample(
               phase().toPathRef(), phase().toImageRef(), phase().isToContentApplied(), path,
               sampleEdge, pendingContent);
}


bool SlideshowController::snapshotSlideshowContentAppearance(const QString &path,
                                                   WorkspaceItemState *out) const
{
    // GUI-only: session map → path map → durable XDG. Worker must not call this.
    // Same resolution order as imageWithSessionAppearance (without transforming).
    if (!out || path.isEmpty()) {
        return false;
    }
    *out = {};
    const SessionImageId sid = sessionIdForPath(path);
    if (sid != kInvalidSessionImageId && m_view->itemWorld().hasDurableAppearance(sid)) {
        const WorkspaceItemState app = m_view->sessionAppearanceValue(sid);
        if (SessionAppearance::hasContentAppearance(app)) {
            *out = app;
            return true;
        }
    }
    // Unbound only: path map + path XDG. Bound = ItemWorld sparse only
    // (matches Image underlay — no path XDG orient for bound ids).
    if (sid == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(path)) {
            if (SessionAppearance::hasContentAppearance(*st)) {
                *out = *st;
                return true;
            }
        }
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && stored.hasOrientContent()) {
            out->path = path;
            out->sessionId = sid;
            SessionAppearance::applyStoredContentAppearance(out, stored, false);
            return SessionAppearance::hasContentAppearance(*out);
        }
    }
    return false;
}


void SlideshowController::finishSlideshowPhaseBufferUpgrade(const QString &path, const QImage &oriented,
                                                  quint64 generation)
{
    // GUI assign after pool orient. Generation discards stale mid-slide work.
    if (generation != phase().phaseUpgradeGenerationValue()) {
        return;
    }
    if (path.isEmpty() || oriented.isNull()) {
        return;
    }
    const int incoming = ImageCache::longEdge(oriented);
    const int need = SlideshowAtlasPolicy::needEdge(slideshowTargetEdge());
    bool changed = false;
    if (phase().isFromPath(path)
        && SlideshowPhasePolicy::acceptOrientedUpgrade(
               incoming, ImageCache::longEdge(phase().fromImageRef()),
               phase().isFromContentApplied())) {
        phase().setFromImage(oriented, true);
        ImageCache::stampDebugOverlayIfEnabled(&phase().fromImageMutable(), path);
        dwell().setSourceImage(phase().fromImageRef());
        // Orient may swap aspect — drop atlas built from the unoriented sample.
        if (dwell().hasAtlas() && dwell().atlasRef().height() > 0
            && oriented.height() > 0) {
            if (SlideshowMotionGeometry::aspectMismatch(
                    qreal(dwell().atlasRef().width()), qreal(dwell().atlasRef().height()),
                    qreal(oriented.width()), qreal(oriented.height()))) {
                invalidateDwellAtlasRebuilds();
                dwell().clearAtlasPixmap();
            }
        }
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            !dwell().hasAtlas()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !SlideshowAtlasPolicy::coversSource(
                   dwell().atlasRef(), dwell().atlasScaleValue(), dwell().atlasVwValue(),
                   dwell().atlasVhValue(), params, oriented);
        if (needAtlas) {
            requestDwellAtlasRebuild();
        }
        changed = true;
    }
    if (phase().isToPath(path)
        && SlideshowPhasePolicy::acceptOrientedUpgrade(
               incoming, ImageCache::longEdge(phase().toImageRef()),
               phase().isToContentApplied())) {
        phase().setToImage(oriented, true);
        ImageCache::stampDebugOverlayIfEnabled(&phase().toImageMutable(), path);
        if (phase().hasToAtlas() && phase().toAtlasRef().height() > 0
            && oriented.height() > 0) {
            if (SlideshowMotionGeometry::aspectMismatch(
                    qreal(phase().toAtlasRef().width()), qreal(phase().toAtlasRef().height()),
                    qreal(oriented.width()), qreal(oriented.height()))) {
                phase().bumpToAtlasRebuildGeneration();
                phase().clearToAtlas();
            }
        }
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            !phase().hasToAtlas()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !SlideshowAtlasPolicy::coversSource(
                   phase().toAtlasRef(), phase().toAtlasScaleValue(), phase().toAtlasVwValue(), phase().toAtlasVhValue(),
                   params, oriented);
        if (needAtlas) {
            requestToPhaseAtlasRebuild();
        }
        changed = true;
    }
    if (changed && m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::scheduleSlideshowPhaseBufferUpgrade(const QString &path, const QImage &image)
{
    // HQ→full mid-slide: clamp + orient off the GUI thread (never Smooth scale here).
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    if (!phase().isPhasePath(path)) {
        return;
    }
    // Cheap long-edge check before pool work; clamp itself runs off-GUI.
    const int incomingRaw = ImageCache::longEdge(image);
    const int targetEdge = slideshowTargetEdge();
    const int incoming = incomingRaw > targetEdge && targetEdge > 0 ? targetEdge : incomingRaw;
    if (!phaseBufferWantsSample(path, incoming)) {
        return;
    }

    WorkspaceItemState appState;
    const bool hasApp = snapshotSlideshowContentAppearance(path, &appState);
    const quint64 gen = phase().bumpPhaseUpgradeGeneration();
    const QPointer<SlideshowController> self(this);
    const QString pathCopy = path;
    const QImage raw = image;
    const int edgeCap = targetEdge;

    QThreadPool::globalInstance()->start(
        [self, pathCopy, raw, appState, hasApp, gen, edgeCap]() {
            QImage capped = ImageCache::clampToMaxEdge(raw, edgeCap);
            if (capped.isNull()) {
                capped = raw;
            }
            QImage out = capped;
            if (hasApp) {
                const QImage oriented = SessionAppearance::materializeDisplay(
                    capped, appState, SessionAppearance::PixelKind::SoftPreview);
                if (!oriented.isNull()) {
                    out = oriented;
                }
            }
            SlideshowController *ctrl = self.data();
            if (!ctrl) {
                return;
            }
            QTimer::singleShot(0, ctrl, [self, pathCopy, out, gen]() {
                if (SlideshowController *c = self.data()) {
                    c->finishSlideshowPhaseBufferUpgrade(pathCopy, out, gen);
                }
            });
        },
        -1);
}


void SlideshowController::finishSlideshowAtlas(SlideshowAtlasKind kind, quint64 generation,
                                     const QImage &scaled, qreal atlasScale,
                                     int atlasVw, int atlasVh)
{
    if (scaled.isNull()) {
        return;
    }
    if (kind == SlideshowAtlasKind::From) {
        if (generation != dwell().atlasRebuildGenerationValue()) {
            return;
        }
        dwell().setAtlas(QPixmap::fromImage(scaled), atlasScale, atlasVw, atlasVh);
    } else {
        if (generation != phase().toAtlasRebuildGenerationValue()) {
            return;
        }
        phase().setToAtlas(QPixmap::fromImage(scaled), atlasScale, atlasVw, atlasVh);
    }
    if (m_view->viewport() && hud().isProgressActive()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::requestSlideshowAtlas(SlideshowAtlasKind kind)
{
    // Scale off the GUI thread; keep the previous atlas until finish assigns.
    if (!m_view->viewport() || hud().isNavHot()) {
        return;
    }
    const QImage *source = (kind == SlideshowAtlasKind::From)
                               ? &dwell().sourceImageRef()
                               : &phase().toImageRef();
    if (!source || source->isNull()) {
        return;
    }
    const DwellAtlasParams params = dwellAtlasParams();
    if (!params.valid) {
        return;
    }
    const QPixmap *atlas = (kind == SlideshowAtlasKind::From) ? &dwell().atlasRef() : &phase().toAtlasRef();
    const qreal aScale = (kind == SlideshowAtlasKind::From) ? dwell().atlasScaleValue() : phase().toAtlasScaleValue();
    const int aVw = (kind == SlideshowAtlasKind::From) ? dwell().atlasVwValue() : phase().toAtlasVwValue();
    const int aVh = (kind == SlideshowAtlasKind::From) ? dwell().atlasVhValue() : phase().toAtlasVhValue();
    if (SlideshowAtlasPolicy::coversSource(*atlas, aScale, aVw, aVh, params,
                                           *source)) {
        return;
    }

    const quint64 gen = (kind == SlideshowAtlasKind::From)
                            ? dwell().bumpAtlasRebuildGeneration()
                            : phase().bumpToAtlasRebuildGeneration();
    const QImage src = *source;
    const int longCap = params.longCap;
    const qreal keyScale = params.keyScale;
    const int vw = params.vw;
    const int vh = params.vh;
    const QPointer<SlideshowController> self(this);
    QThreadPool::globalInstance()->start(
        [self, src, longCap, keyScale, vw, vh, gen, kind]() {
            if (src.isNull()) {
                return;
            }
            // Always Smooth on the pool thread. FastTransformation upscales
            // soft samples to nearest-neighbour garbage that stays on screen
            // for the whole dwell when coverage skips a later rebuild.
            QImage scaled = src.scaled(longCap, longCap, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            SlideshowController *ctrl = self.data();
            if (scaled.isNull() || !ctrl) {
                return;
            }
            QTimer::singleShot(0, ctrl, [self, gen, scaled, keyScale, vw, vh, kind]() {
                if (SlideshowController *c = self.data()) {
                    c->finishSlideshowAtlas(kind, gen, scaled, keyScale, vw, vh);
                }
            });
        },
        -1);
}


void SlideshowController::requestDwellAtlasRebuild()
{
    requestSlideshowAtlas(SlideshowAtlasKind::From);
}


void SlideshowController::requestToPhaseAtlasRebuild()
{
    requestSlideshowAtlas(SlideshowAtlasKind::To);
}


void SlideshowController::onSlideshowRasterReady(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    const int incoming = ImageCache::longEdge(image);
    // Host ImageCache is often already updated by noteDelivery / ladderReady
    // *before* this runs. Comparing incoming to the cache edge skipped every
    // upgrade and left m_ssFrom/To stuck on LQIP/soft until re-enter.
    const int hadPhase =
        (phase().isFromPath(path)) ? ImageCache::longEdge(phase().fromImageRef())
        : (phase().isToPath(path))  ? ImageCache::longEdge(phase().toImageRef())
                                 : 0;
    putSlideshowRaster(path, image);

    const bool isPhasePath = phase().isPhasePath(path);
    if (!isPhasePath) {
        return;
    }

    if (phaseBufferWantsSample(path, incoming)) {
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] raster-ready " << QFileInfo(path).fileName()
            << " " << image.width() << "x" << image.height()
            << " (phase was " << hadPhase << ")";
        // Phase buffer upgrade (clamp + orient + atlas) is deferred off this stack
        // so ladderReady does not block the pure-phase clock.
        scheduleSlideshowPhaseBufferUpgrade(path, image);
    } else if (m_view->viewport() && hud().isProgressActive()) {
        m_view->viewport()->update();
    }

    // Climb while the *phase buffer* is still short of target (not only when
    // the host cache edge increases).
    if (m_view->hostPathRaster()) {
        const int target = m_view->hostDisplayPipeline().cappedDisplayEdgeForPath(path, slideshowTargetEdge());
        const int need = SlideshowAtlasPolicy::needEdge(target);
        const int phaseHave =
            (phase().isFromPath(path)) ? ImageCache::longEdge(phase().fromImageRef())
                                   : ImageCache::longEdge(phase().toImageRef());
        if (phaseHave < need || incoming < need) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_view->hostPathRaster()->ensure(path, target, m_view->logicalSizeForPath(path), policy);
        }
    }
}



void SlideshowController::bindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id,
                                            const QString &path)
{
    if (!id) {
        return;
    }
    unbindSlideshowPhaseSurface(id);
    if (path.isEmpty()) {
        return;
    }
    *id = m_view->hostDisplaySurfaces().bind(
        DisplaySurface::Kind::SlideshowPhase, path, kInvalidSessionImageId);
}


void SlideshowController::unbindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id)
{
    if (!id || *id == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    m_view->hostDisplaySurfaces().unbind(*id);
    *id = DisplaySurface::kInvalidSurfaceId;
}


void SlideshowController::slideshowPhaseSurfaceTick()
{
    // ImageFocus: never. Slideshow phase buffers only, while a transition is live.
    if (!hud().isProgressActive()) {
        return;
    }

    auto drivePhase = [this](DisplaySurface::SurfaceId *sid, const QString &path,
                             int shownEdge) {
        if (path.isEmpty()) {
            return;
        }
        if (!sid || *sid == DisplaySurface::kInvalidSurfaceId) {
            bindSlideshowPhaseSurface(sid, path);
        }
        if (!sid || *sid == DisplaySurface::kInvalidSurfaceId) {
            return;
        }
        const int target = slideshowTargetEdge();
        const int hostEdge = DisplayQuality::hostLongEdge(path);
        const bool pending =
            m_view->hostPathRaster() && m_view->hostPathRaster()->isClimbPending(path);
        m_view->hostDisplaySurfaces().setNeed(*sid, target);
        m_view->hostDisplaySurfaces().setHostLongEdge(*sid, hostEdge);
        m_view->hostDisplaySurfaces().setClimbPending(*sid, pending);
        DisplaySurface::AttachedKind ak = DisplaySurface::AttachedKind::None;
        if (shownEdge > 0) {
            ak = (shownEdge >= target)
                ? DisplaySurface::AttachedKind::FullSource
                : DisplaySurface::AttachedKind::SoftPreview;
        }
        m_view->hostDisplaySurfaces().setAttached(*sid, ak, shownEdge, ContentXform::Value{});
        const DisplaySurface::Action act = m_view->hostDisplaySurfaces().evaluate(*sid);
        using AT = DisplaySurface::ActionType;
        if (act.type == AT::None) {
            return;
        }
        if (act.type == AT::AttachSoft || act.type == AT::AttachFull
            || act.type == AT::ScheduleAsyncMaterialize) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                scheduleSlideshowPhaseBufferUpgrade(path, host);
            }
            return;
        }
        if (act.type == AT::ScheduleClimb && m_view->hostPathRaster()) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_view->hostPathRaster()->ensure(
                path, target, m_view->logicalSizeForPath(path), policy);
        }
    };
    drivePhase(&phase().fromSurfaceRef(), phase().fromPathRef(), ImageCache::longEdge(phase().fromImageRef()));
    drivePhase(&phase().toSurfaceRef(), phase().toPathRef(), ImageCache::longEdge(phase().toImageRef()));
}



QString SlideshowController::slideshowPrefetchHudLine() const
{
    // Ladder edge chips ("Loading 1024→2048") were noisy and not actionable —
    // especially with HUD off. Prefetch still runs; status is not shown here.
    Q_UNUSED(hud().isProgressActive());
    return {};
}


QImage SlideshowController::slideshowRaster(const QString &path) const
{
    // Unoriented host samples only — not item->sourceImage() (appearance baked).
    return path.isEmpty() ? QImage() : ImageCache::get(path);
}




QImage SlideshowController::slideshowSoftPlaceholder(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    QImage have = slideshowRaster(path);
    if (!have.isNull()) {
        return have;
    }
    // Any host sample (incl. filmstrip soft), then durable LQIP. GUI-safe —
    // never loadThumbnail / native decode here. Schedule soft for the next tick.
    const int edge = slideshowTargetEdge();
    QImage soft = ImageCache::get(path);
    if (soft.isNull()) {
        soft = ThumtooCache::cachedLqipImage(path);
    }
    if (soft.isNull()) {
        // LQIP/cache miss: PathRaster SoftDisplay → TileSynth or pyramid.
        // Fallback without PathRaster: same (never PreferCache soft encode).
        if (m_view->hostPathRaster()) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_view->hostPathRaster()->ensure(path, edge, m_view->logicalSizeForPath(path), policy);
        } else if (ThumtooCache::isAvailable()) {
            (void)ThumtooCache::scheduleTileSynthOrPyramid(
                path, ThumtooCache::kGalleryLadderEdge);
        }
        return {};
    }
    soft = ImageCache::clampToMaxEdge(soft, edge);
    putSlideshowRaster(path, soft);
    return soft;
}

SessionImageId SlideshowController::sessionIdForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return kInvalidSessionImageId;
    }
    // Prefer ordered session row (slideshow / gallery path list).
    {
        const SessionImageId ordered = m_view->firstSessionIdForPath(path);
        if (ordered != kInvalidSessionImageId) {
            return ordered;
        }
    }
    // Image-mode slideshow: current session cursor when path matches.
    if (m_view->hostSessionId().hasCurrentId()) {
        if (ImageItem *it = m_view->findItemBySessionId(m_view->hostSessionId().currentIdValue())) {
            if (it->path() == path) {
                return m_view->hostSessionId().currentIdValue();
            }
        }
        if (m_view->hostImage().classicPath() == path || m_view->currentPath() == path) {
            return m_view->hostSessionId().currentIdValue();
        }
    }
    return kInvalidSessionImageId;
}


QImage SlideshowController::orientSlideshowImage(const QImage &raw, const QString &path) const
{
    // ItemWorld sparse tables are sole content truth (CROP_MODE.md): flips, turns, crop.
    if (raw.isNull() || path.isEmpty()) {
        return raw;
    }
    WorkspaceItemState app;
    if (!snapshotSlideshowContentAppearance(path, &app)
        || !SessionAppearance::hasContentAppearance(app)) {
        return raw;
    }
    QImage soft = raw;
    if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
        soft = ImageCache::clampToMaxEdge(
            soft, ContentXform::kGuiMaterializeMaxEdge);
    }
    const QImage oriented = SessionAppearance::materializeDisplay(
        soft, app, SessionAppearance::PixelKind::SoftPreview);
    return oriented.isNull() ? raw : oriented;
}


QImage SlideshowController::slideshowSampleUnoriented(const QString &path) const
{
    // Host sample (any soft) + LQIP — no flip/rotate, no loadThumbnail (GUI-safe).
    const int edge = slideshowTargetEdge();
    QImage img = slideshowRaster(path);
    if (img.isNull()) {
        img = ImageCache::get(path);
    }
    if (img.isNull()) {
        img = ThumtooCache::cachedLqipImage(path);
        // Const method: do not put into ImageCache here; soft placeholder may.
    }
    return ImageCache::clampToMaxEdge(img, edge);
}


QImage SlideshowController::slideshowPixelsForPath(const QString &path)
{
    // Oriented sample for callers that need correct content orientation now.
    // Phase arm uses the unoriented path + async upgrade to avoid GUI hitches.
    const QImage img = slideshowSampleUnoriented(path);
    if (img.isNull()) {
        QImage soft = slideshowSoftPlaceholder(path);
        soft = ImageCache::clampToMaxEdge(soft, slideshowTargetEdge());
        return orientSlideshowImage(soft, path);
    }
    return orientSlideshowImage(img, path);
}


void SlideshowController::pruneZoomBlurOutsidePhasePair(const QString &fromPath, const QString &toPath)
{
    const int vw = zoomBlur().viewportWidth() > 0 ? zoomBlur().viewportWidth()
                                    : (m_view->viewport() ? m_view->viewport()->width() : 0);
    const int vh = zoomBlur().viewportHeight() > 0 ? zoomBlur().viewportHeight()
                                    : (m_view->viewport() ? m_view->viewport()->height() : 0);
    // Keep lastGood across path changes — previous underlay holds until the
    // new key finishes (paintZoomBlurUnderlay draws it).
    ZoomBlur::pruneOutsidePair(&zoomBlur(),
                               ZoomBlur::key(fromPath, vw, vh),
                               ZoomBlur::key(toPath, vw, vh));
}


void SlideshowController::schedulePhaseZoomBlur(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull() || !m_view->viewport()) {
        return;
    }
    const QSize vs = m_view->viewport()->size();
    const qint64 key = ZoomBlur::key(path, vs.width(), vs.height());
    if (key != 0) {
        scheduleZoomBlurBuild(image, vs.width(), vs.height(), key);
    }
}


void SlideshowController::captureMotionBiasesForPath(const QString &path, const QImage &image,
                                           QPointF *outA, QPointF *outB)
{
    if (!outA || !outB) {
        return;
    }
    const QPointF saveA = dwell().biasAPoint();
    const QPointF saveB = dwell().biasBPoint();
    const QPointF saveDir = dwell().travelDirPoint();
    const qreal saveSign = dwell().motionSignValue();
    const bool saveV = dwell().hasBias();
    dwell().clearBias();
    pickInterestingMotionBiases(qHash(path), image);
    *outA = dwell().biasAPoint();
    *outB = dwell().biasBPoint();
    if (saveV) {
        dwell().applyBias(saveA, saveB, saveDir, saveSign);
    } else {
        dwell().clearBias();
    }
}


void SlideshowController::ensureSlideshowMotionTimer()
{
    if (motionTimer()) {
        return;
    }
    motionTimer() = new QTimer(this);
    motionTimer()->setTimerType(Qt::PreciseTimer);
    motionTimer()->setInterval(SlideshowProgressHud::kMotionTickMs);
    connect(motionTimer(), &QTimer::timeout, this, &SlideshowController::tickSlideshowMotion);
}


bool SlideshowController::shouldPromoteSlideshowToAsFrom(const QString &fromPath) const
{
    // Spec: B moves through transition *and* its following interval.
    // When dwell becomes B after A+B fade, promote B's motion — do not restart at 0.
    return !fromPath.isEmpty() && fromPath == phase().toPathRef() && phase().isToMotionClockRunning();
}


void SlideshowController::promoteSlideshowFromToPhase(const QString &fromPath)
{
    if (phase().hasToImage()) {
        phase().setFromImage(phase().toImageRef(), phase().isToContentApplied());
    } else {
        // Oriented path when to-phase missing (slideshowPixelsForPath materializes).
        phase().setFromImage(slideshowPixelsForPath(fromPath), true);
    }
    dwell().applyBias(phase().toBiasAPoint(), phase().toBiasBPoint(), dwell().travelDirPoint(), dwell().motionSignValue());
    dwell().setBiasPath(fromPath);
    phase().promoteFromMotionFromTo();
    dwell().setMotionT(phase().fromMotionTValue());
    // Keep the to-atlas as the from/dwell atlas — clearing it forced multi-MP
    // drawImage every frame until rebuild (visible frame drops on promote).
    if (phase().hasToAtlas()) {
        dwell().setAtlas(phase().toAtlasRef(), phase().toAtlasScaleValue(), phase().toAtlasVwValue(),
                           phase().toAtlasVhValue(), phase().toAtlasRebuildGenerationValue());
    }
}


void SlideshowController::startSlideshowFromPhase(const QString &fromPath)
{
    // Unoriented clamp only — ContentXform orient + atlas run async
    // (prepareSlideshowFromDwell → scheduleSlideshowPhaseBufferUpgrade).
    // Sync orient of multi-MP on every ←/→ dropped frames; same-edge orient must
    // still be accepted (see phaseBufferWantsSample / phase().isFromContentApplied()).
    phase().setFromImage(slideshowSampleUnoriented(fromPath), false);
    if (!phase().hasFromImage() && !fromPath.isEmpty()) {
        phase().setFromImage(ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(fromPath), slideshowTargetEdge()), false);
    }
    // Always orient a ≤512 stand-in on the GUI when appearance is present so
    // the first paint is correct. Larger samples are clamped for this pass;
    // sharper unoriented host climbs via scheduleSlideshowPhaseBufferUpgrade
    // (must pass *host* raw — never re-materialize an oriented phase buffer).
    if (!fromPath.isEmpty() && phase().hasFromImage()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(fromPath, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            QImage soft = phase().fromImageRef();
            if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    soft, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage oriented = SessionAppearance::materializeDisplay(
                soft, app, SessionAppearance::PixelKind::SoftPreview);
            if (!oriented.isNull()) {
                phase().setFromImage(oriented, true);
            }
        }
    }
    if (!fromPath.isEmpty()) {
        (void)m_view->ensureLogicalSizeForPath(fromPath);
        if (!phase().hasFromImage()
            || ImageCache::longEdge(phase().fromImageRef())
                   < SlideshowAtlasPolicy::needEdge(slideshowTargetEdge())) {
            preloadSlideshowImage(fromPath);
        }
        dwell().clearBias();
        pickInterestingMotionBiases(qHash(fromPath), phase().fromImageRef());
        dwell().setBiasPath(fromPath);
    }
    phase().startFromMotionClock();
    phase().setFromMotionT(0.0);
    dwell().setMotionT(0.0);
}


void SlideshowController::prepareSlideshowFromDwell(const QString &fromPath)
{
    dwell().setSourceImage(phase().fromImageRef());
    if (!phase().hasFromImage()) {
        return;
    }
    phase().bumpPhaseUpgradeGeneration(); // drop mid-slide upgrades for previous path

    // Rapid user ←/→ (nav hot): phase buffer already holds soft/best cache.
    // Do not schedule atlas rebuild, zoom-blur, PreferCache, or phase-buffer
    // upgrades per key — those flood the thread pool and PathRaster and make
    // the show progressively worse the longer a key is held. Settle (MainWindow
    // timer) clears nav-hot and re-arms full quality for the current path only.
    if (hud().isNavHot()) {
        invalidateDwellAtlasRebuilds();
        dwell().clearAtlasPixmap();
        return;
    }

    // Keep atlas only when promote carried oriented continuity for the *same*
    // sample. Fresh arm (contentApplied false) or aspect mismatch: drop atlas
    // so paintMotionCover blits the live sample without stretch for a frame.
    const bool keepAtlas = phase().isFromContentApplied()
        && dwell().hasAtlas()
        && SlideshowAtlasPolicy::coversSource(
               dwell().atlasRef(), dwell().atlasScaleValue(), dwell().atlasVwValue(),
               dwell().atlasVhValue(), dwellAtlasParams(), phase().fromImageRef());
    if (!keepAtlas) {
        invalidateDwellAtlasRebuilds();
        dwell().clearAtlasPixmap();
    }
    // Async atlas — never scale multi-MP on the GUI during ←/→ or phase arm.
    // paintMotionCover falls back to drawImage until the atlas is ready.
    requestDwellAtlasRebuild();
    schedulePhaseZoomBlur(fromPath, phase().fromImageRef());
    // Sharper climb from *unoriented* host only. Passing the phase buffer here
    // re-materialized an already-oriented sample (double turns/flips → glitch).
    if (!fromPath.isEmpty()) {
        QImage host = ImageCache::get(fromPath);
        if (host.isNull()) {
            host = slideshowSampleUnoriented(fromPath);
        }
        if (!host.isNull()) {
            scheduleSlideshowPhaseBufferUpgrade(fromPath, host);
        }
    }
}


void SlideshowController::armSlideshowMotionClock(int pathMs)
{
    if (settings().isMotionOff() || pathMs < 250) {
        return;
    }
    ensureSlideshowMotionTimer();
    dwell().setMotionActive(true);
    dwell().setDurationMs(pathMs);
    // While paused, arm motion state but do not run the timer.
    if (!dwell().isMotionPaused()) {
        motionTimer()->start();
    }
}


void SlideshowController::armSlideshowFromPhase(const QString &fromPath, int pathMs)
{
    const bool promote = shouldPromoteSlideshowToAsFrom(fromPath);
    phase().setFromPath(fromPath);
    bindSlideshowPhaseSurface(&phase().fromSurfaceRef(), fromPath);
    if (promote) {
        promoteSlideshowFromToPhase(fromPath);
    } else {
        startSlideshowFromPhase(fromPath);
    }
    prepareSlideshowFromDwell(fromPath);
    armSlideshowMotionClock(pathMs);
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] phase-from "
        << QFileInfo(fromPath).fileName()
        << " " << phase().fromImageRef().width() << "x" << phase().fromImageRef().height()
        << (promote ? " (continue)" : " (start)");
}


void SlideshowController::armSlideshowToPhase(const QString &toPath)
{
    if (toPath.isEmpty()) {
        phase().clearToPath();
        unbindSlideshowPhaseSurface(&phase().toSurfaceRef());
        phase().clearToImage();
        phase().bumpToAtlasRebuildGeneration();
        phase().clearToAtlas();
        phase().setToMotionClockRunning(false);
        phase().setToMotionT(0.0);
        return;
    }
    phase().setToPath(toPath);
    bindSlideshowPhaseSurface(&phase().toSurfaceRef(), toPath);
    (void)m_view->ensureLogicalSizeForPath(toPath);
    phase().setToImage(slideshowSampleUnoriented(toPath), false);
    if (!phase().hasToImage()) {
        phase().setToImage(ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(toPath), slideshowTargetEdge()), false);
    }
    if (phase().hasToImage()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(toPath, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            QImage soft = phase().toImageRef();
            if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    soft, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage oriented = SessionAppearance::materializeDisplay(
                soft, app, SessionAppearance::PixelKind::SoftPreview);
            if (!oriented.isNull()) {
                phase().setToImage(oriented, true);
            }
        }
    }
    // Cold to-path: kick preload immediately (do not wait for neighbour pump).
    if (!phase().hasToImage()
        || ImageCache::longEdge(phase().toImageRef()) < SlideshowAtlasPolicy::needEdge(slideshowTargetEdge())) {
        preloadSlideshowImage(toPath);
    }
    captureMotionBiasesForPath(toPath, phase().toImageRef(), &phase().toBiasARef(), &phase().toBiasBRef());
    phase().startToMotionClock();
    phase().setToMotionT(0.0);
    if (phase().hasToImage()) {
        schedulePhaseZoomBlur(toPath, phase().toImageRef());
    }
    phase().bumpToAtlasRebuildGeneration(); // drop stale to-atlas jobs
    phase().clearToAtlas();
    requestToPhaseAtlasRebuild();
    if (phase().hasToImage()) {
        {
            QImage host = ImageCache::get(toPath);
            if (host.isNull()) {
                host = slideshowSampleUnoriented(toPath);
            }
            if (!host.isNull()) {
                scheduleSlideshowPhaseBufferUpgrade(toPath, host);
            }
        }
    }
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] phase-to "
        << QFileInfo(toPath).fileName()
        << " " << phase().toImageRef().width() << "x" << phase().toImageRef().height();
}


int SlideshowController::slideshowPathDurationMs() const
{
    return SlideshowClocks::pathDurationMs(hud().progressInterval(),
                                           settings().transitionDuration());
}


void SlideshowController::warmZoomBlurForCurrentPhase()
{
    // Skip while user is key-repeating — builds fight soft decode.
    if (!m_view->viewport() || hud().isNavHot()
        || !settings().isZoomBlurLetterbox()) {
        return;
    }
    const QSize vs = m_view->viewport()->size();
    if (vs.width() <= 0 || vs.height() <= 0) {
        return;
    }
    auto warm = [&](const QString &path, const QImage &img) {
        if (path.isEmpty() || img.isNull()) {
            return;
        }
        const qint64 key = qint64(qHash(path))
            ^ (qint64(vs.width()) << 16) ^ qint64(vs.height());
        scheduleZoomBlurBuild(img, vs.width(), vs.height(), key);
    };
    warm(phase().fromPathRef(), !phase().hasFromImage() ? dwell().sourceImageRef() : phase().fromImageRef());
    warm(phase().toPathRef(), phase().toImageRef());
}


bool SlideshowController::applySlideshowFadeProgressOnly(qreal fadeT)
{
    // Pure-phase clock ticks at 16ms with unchanged from/to — only advance fade.
    if (qFuzzyCompare(fadeT, phase().fadeTValue()) || (fadeT < 0.0 && phase().inDwell())) {
        return false;
    }
    phase().setFadeBlend(fadeT);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}


void SlideshowController::updateSlideshowPhaseMotionProgress(int pathMs)
{
    // T ∈ [0,1] is authority; clocks only measure Δt for integration.
    SlideshowClocks::advancePhaseMotion(&phase(), &dwell(), pathMs);
}


void SlideshowController::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    if (!hud().isProgressActive()) {
        return;
    }

    const bool fromChanged = (fromPath != phase().fromPathRef());
    const bool toChanged = (toPath != phase().toPathRef());
    // Unchanged paths: never hide underlay / full scene refresh every 16ms.
    if (!fromChanged && !toChanged) {
        (void)applySlideshowFadeProgressOnly(fadeT);
        return;
    }
    // Do NOT bump ZoomBlur generation here — that cancelled the incoming
    // slide's underlay build every transition and caused letterbox flicker.
    // Evict only slots that are neither from nor to; in-flight jobs for the
    // new pair keep running.
    pruneZoomBlurOutsidePhasePair(fromPath, toPath);
    const int pathMs = slideshowPathDurationMs();

    // Phase buffers lock at from/toChanged; preload fills ImageCache for next.
    // Keep phase().rasterPending look-ahead (do not clear — cancelled +2/+3 warm-up).
    if (fromChanged) {
        armSlideshowFromPhase(fromPath, pathMs);
    }
    if (toPath.isEmpty()) {
        armSlideshowToPhase(QString()); // clear
    } else if (toChanged) {
        armSlideshowToPhase(toPath);
    }
    updateSlideshowPhaseMotionProgress(pathMs);
    phase().setFadeBlend(fadeT);

    if (const char *dbg = std::getenv("BILTOO_DEBUG_SLIDESHOW");
        (dbg && dbg[0] && dbg[0] != '0')
        || (std::getenv("THUMTOO_DEBUG")
            && std::getenv("THUMTOO_DEBUG")[0]
            && std::getenv("THUMTOO_DEBUG")[0] != '0')) {
        fprintf(stderr,
                "biltoo/ss: phase from=%s to=%s fade=%.3f fromImg=%dx%d toImg=%dx%d\n",
                qPrintable(QFileInfo(fromPath).fileName()),
                qPrintable(QFileInfo(toPath).fileName()),
                fadeT,
                phase().fromImageRef().width(), phase().fromImageRef().height(),
                phase().toImageRef().width(), phase().toImageRef().height());
    }

    if (!hud().isNavHot()) {
        warmZoomBlurForCurrentPhase();
    }
    hideSlideshowUnderlay();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


qreal SlideshowController::slideshowMotionHeadroom() const
{
    return SlideshowAtlasPolicy::motionHeadroom(
        settings().currentMotion(), settings().currentPanZoomFactor(), hud().isProgressActive());
}


int SlideshowController::slideshowTargetEdge() const
{
    if (!m_view->viewport()) {
        return SlideshowAtlasPolicy::targetLongEdge(false, 0, 0, 1.0, 1.0);
    }
    const QSize vs = m_view->viewport()->size();
    return SlideshowAtlasPolicy::targetLongEdge(
        true, vs.width(), vs.height(), m_view->devicePixelRatioF(),
        slideshowMotionHeadroom());
}


qreal SlideshowController::slideshowZoomBaseScale(const QSize &logical, int vw, int vh) const
{
    return SlideshowAtlasPolicy::zoomBaseScale(settings().currentZoom(), logical, vw, vh);
}


void SlideshowController::setSlideshowNavHot(bool hot)
{
    if (!hud().setNavHot(hot)) {
        return;
    }
    // Do NOT invalidateZoomBlurQueue here — keep the previous underlay until a
    // new key's blur is ready (solid flash on every ←/→ was the bug).
    if (hot) {
        // Drop off-canvas prefetch sessions: their InFlight tiles compete with
        // settle soft/climb after a long key-repeat burst.
        m_view->hostTileNeighborPrefetch().clear();
    }
}




void SlideshowController::preloadSlideshowImage(const QString &path)
{
    if (path.isEmpty() || !m_view->hostPathRaster()) {
        return;
    }
    // User key-repeat: no PathRaster EscalateToFull per visited path.
    if (hud().isNavHot()) {
        return;
    }
    const int targetEdge = m_view->hostDisplayPipeline().cappedDisplayEdgeForPath(path, slideshowTargetEdge());
    const int need = SlideshowAtlasPolicy::needEdge(targetEdge);
    const QSize native = m_view->logicalSizeForPath(path);
    const QImage cached = ImageCache::get(path);
    const int haveEdge = ImageCache::longEdge(cached);

    // Quiet no-ops: look-ahead is once per toIdx; still guard re-entry.
    if (need > 0 && ImageCache::adequate(cached, need)) {
        if (!cached.isNull()) {
            onSlideshowRasterReady(path, cached);
        }
        return;
    }
    if (m_view->hostPathRaster()->isClimbPending(path)) {
        if (!cached.isNull()) {
            onSlideshowRasterReady(path, cached);
        }
        return;
    }
    // PreferCache plateau with Full already done for this want — stop.
    if (m_view->hostPathRaster()->isGaveUp(path) && !m_view->hostPathRaster()->isClimbPending(path)) {
        // SoftDisplay plateau — do not escalate to Full native.
    }

    qCDebug(lcSlideshow).nospace()
        << "[slideshow] preload-ensure " << QFileInfo(path).fileName()
        << " edge=" << targetEdge
        << " have=" << haveEdge;

    // SoftDisplay only: PreferCache/TileSynth at screen-fit edge. Never Full
    // native whole-frame (that pulled multi-MP samples for every slide).
    m_view->hostPathRaster()->ensure(path, targetEdge, native,
                         PathRasterService::ClimbPolicy::SoftDisplay);
    // Warm durable tiles into the shared path TileMemoryCache (TileLodRegistry)
    // so SoftDisplay TileSynth and later Image/Gallery views reuse them.
    if (!ThumtooCache::hasDurableTilesKnown(path)) {
        (void)ThumtooCache::scheduleTilePyramid(path);
    }
    m_view->hostDisplayPipeline().tickPrimaryTileLod(TileLoadCoordinator::kDefaultTickBudget);

    const QImage have = ImageCache::get(path);
    if (!have.isNull()) {
        onSlideshowRasterReady(path, have);
    }
}

DwellAtlasParams SlideshowController::dwellAtlasParams() const
{
    // Atlas size is a function of the *viewport* and motion headroom only —
    // not of the source raster's pixel dimensions. Camera dest is aspect-based;
    // the atlas is just a sharp enough texture to sample under max zoom.
    if (!m_view->viewport()) {
        return {};
    }
    return SlideshowAtlasPolicy::makeParams(
        m_view->viewport()->width(), m_view->viewport()->height(), slideshowMotionHeadroom());
}


void SlideshowController::invalidateDwellAtlasRebuilds()
{
    dwell().bumpAtlasRebuildGeneration();
}


void SlideshowController::setSlideshowUnderlayVisible(bool visible)
{
    // Invariant: while a slideshow is in progress the underlay is never shown.
    if (hud().isProgressActive()) {
        visible = false;
    }
    if (ImageItem *item = m_view->targetItem()) {
        item->setVisible(visible);
    }
}


void SlideshowController::hideSlideshowUnderlay()
{
    // Hide every canvas item — new LoadReplace items default to visible and
    // were slipping past a single m_view->targetItem() hide (paint: underlayVisible=true).
    if (!m_view->canvasScene()) {
        return;
    }
    for (QGraphicsItem *gi : m_view->canvasScene()->items()) {
        if (qgraphicsitem_cast<ImageItem *>(gi)) {
            gi->setVisible(false);
        }
    }
}

namespace {


} // namespace


void SlideshowController::clearSlideshowZoomBlurSlots()
{
    // Drop cached underlays and cancel in-flight blur jobs (pad/letterbox/viewport).
    ZoomBlur::clearAllSlots(&zoomBlur());
    invalidateZoomBlurQueue();
}


void SlideshowController::invalidateZoomBlurQueue() const
{
    ZoomBlur::invalidateQueue(&zoomBlur());
}


bool SlideshowController::zoomBlurKeyCached(qint64 key) const
{
    return ZoomBlur::keyCached(zoomBlur(), key);
}


bool SlideshowController::zoomBlurKeyInFlight(qint64 key) const
{
    return ZoomBlur::keyInFlight(zoomBlur(), key);
}


int SlideshowController::claimZoomBlurFlightSlot(qint64 key) const
{
    // Allow up to two concurrent builds (outgoing + incoming underlay).
    return ZoomBlur::claimFlightSlot(&zoomBlur(), key);
}


void SlideshowController::installZoomBlurResult(const QImage &blurred, qint64 key, quint64 gen)
{
    if (!ZoomBlur::installResult(&zoomBlur(), QPixmap::fromImage(blurred), key, gen)) {
        return; // page flipped — discard
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


void SlideshowController::scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const
{
    if (hud().isNavHot()) {
        return;
    }
    if (image.isNull() || vw < 1 || vh < 1 || key == 0) {
        return;
    }
    if (zoomBlurKeyCached(key) || zoomBlurKeyInFlight(key)) {
        return;
    }
    if (claimZoomBlurFlightSlot(key) < 0) {
        return;
    }
    const quint64 gen = zoomBlur().generationValue();
    // Snapshot pixels for the worker (avoid touching GUI QImage after return).
    const QImage src = image.copy();
    const QPointer<SlideshowController> self(const_cast<SlideshowController *>(this));
    QThreadPool::globalInstance()->start([self, src, vw, vh, key, gen]() {
        const QImage blurred = ZoomBlur::makeCover(src, vw, vh);
        SlideshowController *ctrl = self.data();
        if (blurred.isNull() || !ctrl) {
            return;
        }
        QTimer::singleShot(0, ctrl, [self, blurred, key, gen]() {
            if (SlideshowController *c = self.data()) {
                c->installZoomBlurResult(blurred, key, gen);
            }
        });
    });
}




void SlideshowController::paintZoomBlurUnderlay(QPainter *painter, const QImage &image,
                                      const QRect &viewportRect, qint64 stableKey) const
{
    if (!painter || image.isNull() || viewportRect.isEmpty()) {
        return;
    }
    const int vw = viewportRect.width();
    const int vh = viewportRect.height();
    // Stable identity: path key + viewport only.
    // Do NOT fold source width/height — soft→full upgrades would miss the
    // cache and re-blur mid-transition (the spike after the first fix).
    const qint64 key = stableKey ^ (qint64(vw) << 16) ^ qint64(vh);
    if (!zoomBlur().viewportMatches(vw, vh)) {
        // Viewport size change: drop sized slots; keep lastGood stretched until
        // async rebuild finishes (still better than solid flash).
        zoomBlur().clearUnderlays();
        zoomBlur().setViewportSize(vw, vh);
        invalidateZoomBlurQueue();
    }
    const int slot = zoomBlur().findCachedSlot(key);
    if (slot >= 0) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, zoomBlur().underlayAt(slot));
        return;
    }
    // Miss: schedule build only when not in a key-repeat burst (pool pressure).
    // Always keep painting the previous underlay until this key is ready —
    // solid pad on every path change was the "discarded blurry background" bug.
    if (!hud().isNavHot()) {
        scheduleZoomBlurBuild(image, vw, vh, key);
    }
    if (zoomBlur().hasLastGood()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, zoomBlur().lastGoodPixmap());
        return;
    }
    // Slideshow letterbox uses the dedicated pad colour; Image View content-blur
    // falls back to the Preferences / canvas primary colour.
    const QColor pad = settings().isZoomBlurLetterbox()
        ? m_view->slideshowPadColor()
        : m_view->hostCanvasBg().primaryColor();
    painter->fillRect(viewportRect, pad.isValid() ? pad : QColor(42, 42, 42));
}


QSize SlideshowController::resolveMotionLogicalSize(const QString &path) const
{
    // File-native owns motion dest size (SIZE.md). Sample is blit-only; using
    // sample aspect here made cover rect jump when LQIP → soft → full arrived.
    const QSize fileNative = m_view->logicalSizeForPath(path);
    if (isPositiveSize(fileNative) && !path.isEmpty()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(path, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            const QSize lay = ContentXform::layoutSize(fileNative, app);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                return lay;
            }
        }
        return fileNative;
    }
    return QSize(kProvisionalLayoutLongEdge, kProvisionalLayoutLongEdge);
}


QRectF SlideshowController::computeMotionCoverDestRect(qreal iw, qreal ih, int vw, int vh,
                                             qreal motionT, QPointF biasA, QPointF biasB,
                                             const QString &path) const
{
    const QSize logical{int(iw), int(ih)};
    const qreal base = slideshowZoomBaseScale(logical, vw, vh);
    return SlideshowMotionGeometry::coverDestRect(
        settings().currentMotion(), base, settings().currentPanZoomFactor(), iw, ih, vw, vh,
        motionT, biasA, biasB, dwell().hasBias(), path);
}


tilelod::TileLodController *SlideshowController::slideshowTilesForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    if (phase().isFromPath(path)) {
        tilelod::TileLodController *ctrl = phase().ensureFromTiles();
        if (ctrl->path() != path) {
            ctrl->setPath(path);
        }
        return ctrl;
    }
    if (phase().isToPath(path)) {
        tilelod::TileLodController *ctrl = phase().ensureToTiles();
        if (ctrl->path() != path) {
            ctrl->setPath(path);
        }
        return ctrl;
    }
    return nullptr;
}


bool SlideshowController::paintSlideshowTiles(QPainter *painter, const QString &path,
                                    const QRectF &dest, const QImage &underlay) const
{
    if (!painter || path.isEmpty() || dest.isEmpty()) {
        return false;
    }
    // Prefer the live ImageItem session (same path registry + climbed scale)
    // so slideshow tracks ImageView up-res instead of a cold phase session.
    tilelod::TileLodController *lod = nullptr;
    for (ImageItem *it : m_view->liveItems()) {
        if (it && it->path() == path) {
            lod = it->tileLodController();
            if (lod) {
                break;
            }
        }
    }
    if (!lod) {
        lod = slideshowTilesForPath(path);
    }
    if (!lod) {
        return false;
    }
    if (lod->path() != path) {
        lod->setPath(path);
    }
    QSize native = m_view->logicalSizeForPath(path);
    if (!isPositiveSize(native)) {
        native = ThumtooCache::cachedSize(path);
    }
    if (!isPositiveSize(native)) {
        return false;
    }
    if (!ThumtooCache::hasDurableTilesKnown(path)) {
        (void)ThumtooCache::scheduleTilePyramid(path);
    }
    // Image-mode floor: min_scale 0 so density can climb to full-res (Gallery
    // durable floor left slideshow stuck on coarse overview tiles).
    tilelod::CoverPaintArgs args;
    args.lod = lod;
    args.native = native;
    args.dest = dest;
    args.underlay = underlay;
    args.tick_budget = 32;
    args.min_scale = 0;
    args.tick = true;
    const bool drew = tilelod::prepare_and_paint_cover(painter, args);
    if (drew && !lod->viewportFullyCovered() && m_view->viewport()) {
        // Keep climbing while the motion/progress timer paints.
        m_view->viewport()->update();
    }
    // Drive shared coordinator so in-flight tiles complete off the paint path.
    m_view->hostDisplayPipeline().tickPrimaryTileLod(32);
    return drew;
}


void SlideshowController::paintMotionCover(QPainter *painter, const QImage &image,
                                 qreal motionT, QPointF biasA, QPointF biasB,
                                 const QString &path) const
{
    if (!painter || image.isNull() || !m_view->viewport()) {
        return;
    }
    const int vw = ViewTransform::atLeast1(m_view->viewport()->width());
    const int vh = ViewTransform::atLeast1(m_view->viewport()->height());

    const QSize logical = resolveMotionLogicalSize(path);
    const qreal iw = qreal(logical.width());
    const qreal ih = qreal(logical.height());
    if (iw < 1.0 || ih < 1.0) {
        return;
    }

    const QRectF dest = computeMotionCoverDestRect(iw, ih, vw, vh, motionT, biasA, biasB, path);
    if (!dest.isValid() || dest.isEmpty()) {
        return;
    }

    // Tiles first (shared TileLodRegistry + ImageItem session) — same cover paint
    // helper as Image/Gallery. Prefer tiles whenever the path has RAM/durable
    // coverage so SoftDisplay atlases cannot pin the slideshow on low-res.
    if (paintSlideshowTiles(painter, path, dest, image)) {
        return;
    }

    // Prefer pre-scaled atlases matched by path (not QImage address — pure-phase
    // paint may pass temporaries). From/dwell → dwell().atlasRef(); to → phase().toAtlasRef().
    const QPixmap *atlas = nullptr;
    if (!path.isEmpty() && phase().isFromPath(path) && dwell().hasAtlas()) {
        atlas = &dwell().atlasRef();
    } else if (!path.isEmpty() && phase().isToPath(path) && phase().hasToAtlas()) {
        atlas = &phase().toAtlasRef();
    } else if (path.isEmpty() && dwell().hasAtlas()
               && (&image == &dwell().sourceImageRef() || &image == &phase().fromImageRef())) {
        atlas = &dwell().atlasRef();
    }

    // Stale atlas after ContentXform orient (aspect swap) stretches into dest.
    if (atlas && image.width() > 1 && image.height() > 1
        && atlas->width() > 1 && atlas->height() > 1) {
        if (SlideshowMotionGeometry::aspectMismatch(
                qreal(atlas->width()), qreal(atlas->height()),
                qreal(image.width()), qreal(image.height()))) {
            atlas = nullptr;
        }
    }
    if (atlas) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(dest, *atlas, atlas->rect());
    } else {
        // No atlas yet: smooth when the sample is in the display budget so soft
        // placeholders are not nearest-neighbour. Only skip Smooth for huge
        // native samples (rare during slideshow — atlas should cover those).
        const int srcLong = ContentXform::longEdge(image.size());
        const int budget = SlideshowMotionGeometry::atlasBudgetPx(vw, vh);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, srcLong <= budget);
        painter->drawImage(dest, image);
    }
}



QPixmap SlideshowController::renderMotionCoverPixmap(const QImage &image, qreal motionT,
                                           uint pathHash) const
{
    // Snapshot helper (rare). Prefer paintMotionCover on the live painter.
    if (image.isNull() || !m_view->viewport()) {
        return {};
    }
    const int vw = ViewTransform::atLeast1(m_view->viewport()->width());
    const int vh = ViewTransform::atLeast1(m_view->viewport()->height());
    QImage out(vw, vh, QImage::Format_ARGB32_Premultiplied);
    out.fill(m_view->slideshowPadColor());
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // pathHash was historical; recover path from phase when possible.
    QString path;
    if (pathHash != 0 && qHash(phase().toPathRef()) == pathHash) {
        path = phase().toPathRef();
    } else if (phase().hasFromPath()) {
        path = phase().fromPathRef();
    }
    paintMotionCover(&painter, image, motionT, dwell().biasAPoint(), dwell().biasBPoint(), path);
    painter.end();
    return QPixmap::fromImage(out);
}


void SlideshowController::maybeStartSlideshowMotion()
{
    if (settings().isMotionOff() || !hud().isProgressActive()
        || !m_view->isImageMode()) {
        return;
    }
    // Already running — do not restart from 0.
    if (dwell().isMotionActive()) {
        return;
    }
    int duration = hud().progressInterval();
    if (duration < 250) {
        return;
    }
    // Continue from the dwell sample if soft-handoff already set one.
    const qreal initial = ViewTransform::clamp01(dwell().motionTValue()); // already clamped on set
    startSlideshowMotion(duration, initial);
}


bool SlideshowController::prepareSlideshowMotionDwell(ImageItem *item)
{
    // Ken Burns moves the *image* via blit, not the QGraphicsView camera.
    // First paint must be ContentXform-oriented; unoriented dwell was the
    // "garbage first frame" when this ran before setSlideshowPhase.
    const QString path = item->path();
    QImage host = slideshowSampleUnoriented(path);
    if (host.isNull()) {
        host = ImageCache::clampToMaxEdge(item->sourceImage(), slideshowTargetEdge());
    }
    if (host.isNull()) {
        return false;
    }
    QImage dwellImg = host;
    WorkspaceItemState app;
    if (!path.isEmpty()
        && snapshotSlideshowContentAppearance(path, &app)
        && SessionAppearance::hasContentAppearance(app)) {
        QImage soft = host;
        if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
            soft = ImageCache::clampToMaxEdge(
                soft, ContentXform::kGuiMaterializeMaxEdge);
        }
        const QImage oriented = SessionAppearance::materializeDisplay(
            soft, app, SessionAppearance::PixelKind::SoftPreview);
        if (!oriented.isNull()) {
            dwellImg = oriented;
        }
    }
    dwell().setSourceImage(dwellImg);
    // Keep pure-phase buffers in sync when progress is already active (start
    // path arms phase first; motion-only path must not leave m_ssFrom empty).
    if (hud().isProgressActive() && !path.isEmpty()) {
        if (phase().fromPathRef() != path || !phase().hasFromImage()) {
            phase().setFromPath(path);
            bindSlideshowPhaseSurface(&phase().fromSurfaceRef(), path);
            WorkspaceItemState app2;
            const bool applied =
                snapshotSlideshowContentAppearance(path, &app2)
                && SessionAppearance::hasContentAppearance(app2)
                && !dwellImg.isNull();
            phase().setFromImage(dwellImg, applied);
        }
    }
    // Align underlay camera to slideshow zoom before hiding it so cancel/stop
    // can restore a known static frame.
    applySlideshowZoomFraming(item);
    invalidateDwellAtlasRebuilds();
    requestDwellAtlasRebuild(); // async — do not scale on this stack
    if (!path.isEmpty()) {
        // Upgrade from unoriented host only (never re-materialize dwell).
        scheduleSlideshowPhaseBufferUpgrade(path, host);
    }
    setSlideshowUnderlayVisible(false);
    return true;
}


void SlideshowController::freezeScrollbarsForMotion()
{
    // Freeze scrollbars so the view cannot re-clamp/centre while the overlay
    // path is the only thing that should move (underlay is hidden).
    motionScroll().capture(m_view->horizontalScrollBarPolicy(), m_view->verticalScrollBarPolicy());
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_view->horizontalScrollBar()) {
        m_view->horizontalScrollBar()->setValue(0);
    }
    if (m_view->verticalScrollBar()) {
        m_view->verticalScrollBar()->setValue(0);
    }
}


void SlideshowController::resetItemPlacementForMotion(ImageItem *item)
{
    m_view->hostFraming().setFitFillFlags(false, settings().isZoomFill());
    {
        ItemComponents::Placement pl = item->placement();
        pl.shear = 0.0;
        pl.rotation = 0.0;
        pl.scale = 1.0;
        pl.scaleY = 1.0;
        if (m_view->isImageMode()) {
            pl.pos = QPointF(0, 0);
        }
        GalleryLayout::applyItemPlacement(item, pl);
    }
}


void SlideshowController::armMotionBiasForPath(ImageItem *item, const QString &path)
{
    // Biases are per-image. Manual next/prev (and any LoadReplace) must not keep
    // the previous slide's A/B — that made the first post-nav transition glitch.
    // Live handoff installs to-path biases + path before calling here.
    if (dwell().hasBias() && dwell().biasPathRef() != path) {
        dwell().clearBias();
    }
    if (!dwell().hasBias()) {
        const QImage src = item ? item->sourceImage() : QImage();
        pickInterestingMotionBiases(qHash(path), src);
        dwell().setBiasPath(path);
    } else if (dwell().biasPathRef().isEmpty()) {
        dwell().setBiasPath(path);
    }
}


void SlideshowController::retargetSlideshowMotionDuration(int durationMs)
{
    // Interval change while Ken Burns is running: keep atlas, biases, and
    // normalized progress; only the path duration changes.
    if (!dwell().isMotionActive() || settings().isMotionOff()) {
        return;
    }
    durationMs = SlideshowClocks::sanitizeDwellDurationMs(durationMs);
    qreal progress = 0.0;
    if (dwell().hasDuration()) {
        qint64 elapsed = dwell().elapsedOffsetMsValue();
        if (dwell().isClockValid() && !dwell().isMotionPaused()) {
            elapsed += dwell().clockElapsed();
        }
        progress = SlideshowClocks::progress01(elapsed, dwell().durationMsValue());
    }
    dwell().setDurationMs(SlideshowClocks::pathDurationMs(
        durationMs, settings().transitionDuration()));
    dwell().setElapsedOffsetMs(qint64(progress * qreal(dwell().durationMsValue())));
    dwell().startClock();
    if (motionTimer() && !dwell().isMotionPaused()) {
        motionTimer()->start();
    }
}


void SlideshowController::startSlideshowMotion(int durationMs, qreal initialProgress)
{
    cancelSlideshowMotion();
    if (settings().isMotionOff() || !m_view->isImageMode()
        || durationMs < 250 || !m_view->viewport()) {
        return;
    }
    ImageItem *item = m_view->targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    if (!prepareSlideshowMotionDwell(item)) {
        return;
    }
    freezeScrollbarsForMotion();
    resetItemPlacementForMotion(item);
    armMotionBiasForPath(item, item->path());
    // Biases + slideshow zoom are read by renderMotionCoverPixmap.

    ensureSlideshowMotionTimer();
    dwell().setMotionActive(true);
    // Path lasts longer than the dwell interval so that when the slideshow
    // advances (and crossfade runs), from-progress is still < 1 and keeps
    // lerping. Previously duration==interval → progress clamped at 1 for the
    // entire transition → motion looked frozen.
    dwell().setDurationMs(SlideshowClocks::pathDurationMs(
        durationMs, settings().transitionDuration()));
    initialProgress = ViewTransform::clamp01(initialProgress); // [0,1]
    dwell().startClock();
    dwell().setMotionT(ViewTransform::clamp01(initialProgress));
    dwell().setElapsedOffsetMs((dwell().hasDuration())
        ? qint64(dwell().motionTValue() * qreal(dwell().durationMsValue()))
        : 0);
    motionTimer()->start();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}




void SlideshowController::tickSlideshowPhaseMotionClocks()
{
    // Pure-phase path: advance A/B motion from their own clocks (spec: both
    // move during transition; B moves through transition + its interval).
    // Atlas / phase buffers are not rebuilt here — only motion progress.
    updateSlideshowPhaseMotionProgress(slideshowPathDurationMs());
}


void SlideshowController::tickSlideshowDwellMotionClock()
{
    SlideshowClocks::advanceDwellMotion(&dwell());
}


void SlideshowController::tickSlideshowMotion()
{
    if (!dwell().isMotionActive()) {
        if (motionTimer()) {
            motionTimer()->stop();
        }
        return;
    }
    if (!m_view->viewport()) {
        return;
    }
    // Climb shared path tiles while Ken Burns runs (same coordinator as ImageView).
    m_view->hostDisplayPipeline().tickPrimaryTileLod(24);

    if (hud().isProgressActive()
        && (phase().isFromMotionClockRunning() || phase().isToMotionClockRunning())) {
        tickSlideshowPhaseMotionClocks();
        m_view->viewport()->update();
        return;
    }

    tickSlideshowDwellMotionClock();
    m_view->viewport()->update();
}


QString SlideshowController::sessionBadgeText() const
{
    return HudModel::sessionBadge(
        m_view->hostSessionId().currentIndex(), m_view->hostSessionId().currentTotal());
}

// --- Tier 6b input ---

bool SlideshowController::tryMousePressSlideshowSeek(QMouseEvent *event)
{
    // mpv-style seekbar: drag along bottom edge during slideshow.
    if (event->button() != Qt::LeftButton || !m_view->viewport()) {
        return false;
    }
    if (!hud().isSeekHit(event->pos().y(), m_view->viewport()->height())) {
        return false;
    }
    hud().setSeekDragging(true);
    hud().setSeekbarVisible(true);
    const qreal f = ViewTransform::unitFraction(event->pos().x(), m_view->viewport()->width());
    emit m_view->slideshowSeekRequested(f);
    event->accept();
    return true;
}

void SlideshowController::updateMouseMoveSlideshowSeek(QMouseEvent *event)
{
    if (!hud().isProgressActive() || !m_view->viewport()) {
        return;
    }
    const int h = m_view->viewport()->height();
    const bool nearBottom = hud().isSeekHit(event->pos().y(), h);
    if (nearBottom != hud().isSeekbarVisible() && !hud().isSeekDragging()) {
        hud().setSeekbarVisible(nearBottom);
        m_view->viewport()->update();
    }
    if (hud().isSeekDragging() && h > 0 && m_view->viewport()->width() > 0) {
        const qreal f = ViewTransform::unitFraction(event->pos().x(), m_view->viewport()->width());
        emit m_view->slideshowSeekRequested(f);
    }
}

bool SlideshowController::tryMouseReleaseSlideshowSeek(QMouseEvent *event)
{
    if (!hud().isSeekDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    hud().setSeekDragging(false);
    event->accept();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}
