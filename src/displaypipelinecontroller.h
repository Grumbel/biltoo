// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINECONTROLLER_H
#define DISPLAYPIPELINECONTROLLER_H

#include "sessionloadgate.h"
#include "displaysurface.h"

#include <QTimer>

#include <memory>

class ImageView;
class TileLoadCoordinator;

/**
 * Display / load pipeline collaborator for ImageView (Phase 6 Tier 5a).
 *
 * Owns load generation gate, display surfaces, focus surface id, tile LOD
 * timers and coordinator. PreferCache climb methods remain on ImageView until
 * Tier 5b; SIZE.md soft-vs-logical rules stay with ImageSizeBook.
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
