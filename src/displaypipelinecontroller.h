// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINECONTROLLER_H
#define DISPLAYPIPELINECONTROLLER_H

#include "sessionloadgate.h"
#include "displaysurface.h"
#include "sessionappearance.h"
#include "pathrasterservice.h"

#include <QTimer>

#include <memory>

class ImageView;
class ImageItem;
class TileLoadCoordinator;

/**
 * Display / load pipeline collaborator for ImageView (Phase 6 Tier 5a).
 *
 * Owns load generation gate, display surfaces, focus surface id, tile LOD
 * timers, PreferCache climbs, sample install, and surface drive/bind (Tier 5b).
 * SIZE.md soft-vs-logical rules stay with ImageSizeBook.
 */
class DisplayPipelineController
{
public:
    explicit DisplayPipelineController(ImageView *view);

    ImageView *view() const { return m_view; }

    SessionLoadGate &loadGate() { return m_loadGate; }
    const SessionLoadGate &loadGate() const { return m_loadGate; }

    DisplaySurfaceController &displaySurfaces() { return m_displaySurfaces; }
    const DisplaySurfaceController &displaySurfaces() const { return m_displaySurfaces; }

    DisplaySurface::SurfaceId imageFocusSurface() const { return m_imageFocusSurface; }
    void setImageFocusSurface(DisplaySurface::SurfaceId id) { m_imageFocusSurface = id; }
    DisplaySurface::SurfaceId &imageFocusSurfaceRef() { return m_imageFocusSurface; }

    std::unique_ptr<TileLoadCoordinator> &tileCoordinator() { return m_tileCoordinator; }
    const std::unique_ptr<TileLoadCoordinator> &tileCoordinator() const { return m_tileCoordinator; }

    QTimer *&tileLodTimer() { return m_tileLodTimer; }
    QTimer *tileLodTimer() const { return m_tileLodTimer; }

    QTimer *&tileLodZoomDebounce() { return m_tileLodZoomDebounce; }
    QTimer *tileLodZoomDebounce() const { return m_tileLodZoomDebounce; }

    /**
     * PreferCache / PathRaster quality climb for selected (or bounded) Workspace
     * items. Body moved from ImageView (Tier 5b); ImageView thin-forwards.
     */
    void ensureWorkspaceQualityClimb();
    void scheduleImageModeNativeDecodeOnce(const QString &path);
    void scheduleImageModePreferCacheClimb(const QString &path, int wantEdge = 0);
    void requestEscalateClimb(const QString &path, int wantEdge);
    void noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                          const QImage &sample);
    void ensureImageModeQualityClimb(const QString &path, const QImage &sample);
    void installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                       const QImage &image,
                                       SessionAppearance::PixelKind kind);
    int cappedDisplayEdgeForPath(const QString &path, int wantEdge) const;
    bool sampleCoversNativeLogical(const QString &path, const QImage &image) const;
    bool tryInstallImageModeSample(const QString &path, const QImage &image);
    bool tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                        SessionAppearance::PixelKind kind);
    void onLadderReady(const QString &path, int maxEdge, const QImage &image);
    void upgradeImageModeFromLadder(const QString &path, int maxEdge, const QImage &image);
    void applyGalleryLadderReady(const QString &path, int maxEdge, const QImage &image);
    void applyWorkspaceLadderReady(const QString &path, int maxEdge, const QImage &image);
    void maybeClimbImageModePixelsForView();
    DisplaySurface::State displaySurfaceStateForItem(const ImageItem *item,
                                                     int hostLongEdge = -1,
                                                     bool climbPending = false) const;
    bool applyDisplaySurfaceAction(ImageItem *item,
                                   const DisplaySurface::Action &act,
                                   const QImage &hostSample,
                                   int fallbackNeedEdge,
                                   PathRasterService::ClimbPolicy climbPolicy);
    void driveImageFocusSurface();
    void registerItemDisplaySurface(ImageItem *item);
    void unregisterItemDisplaySurface(ImageItem *item);
    void syncItemDisplaySurface(ImageItem *item, int hostLongEdge = -1,
                                bool climbPending = false);
    bool canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                SessionAppearance::PixelKind kind) const;
    void installDisplayPixels(ImageItem *item, const QImage &pixels,
                              SessionAppearance::PixelKind kind,
                              SessionImageId sid);

private:
    ImageView *m_view = nullptr; // not owned
    SessionLoadGate m_loadGate;
    DisplaySurfaceController m_displaySurfaces;
    DisplaySurface::SurfaceId m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;
    std::unique_ptr<TileLoadCoordinator> m_tileCoordinator;
    QTimer *m_tileLodTimer = nullptr;
    QTimer *m_tileLodZoomDebounce = nullptr;
};

#endif // DISPLAYPIPELINECONTROLLER_H
