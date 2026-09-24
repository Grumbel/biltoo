// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGECONTROLLER_H
#define IMAGECONTROLLER_H

#include <QString>
#include "image/edgenavpolicy.h"
#include "slideshow/zoomregiongesture.h"
#include <QSize>

class ImageView;
class ImageItem;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPoint;

/**
 * Image-mode collaborator for ImageView.
 *
 * Owns the classic (single-image) path and the Image-mode enter transition
 * (canvas prepare, clear live items, reload classic path).
 * Gallery/Workspace leave policy (stash) runs in those controllers' onLeave
 * before enter is called.
 * ImageView remains the QGraphicsView shell and public API surface.
 */
class ImageController
{
public:
    explicit ImageController(ImageView *view);

    /** Enter Image mode from any previous mode (stash already handled by onLeave). */
    void enter();

    QString classicPath() const { return m_classicPath; }
    bool hasClassicPath() const { return !m_classicPath.isEmpty(); }
    void setClassicPath(const QString &path) { m_classicPath = path; }
    void clearClassicPath() { m_classicPath.clear(); }

    /** Left/Right/PageUp/PageDown session navigation (Image mode). */
    bool tryKeyPressNavigate(QKeyEvent *event);
    bool tryMousePressEdges(QMouseEvent *event);

    // Edge hover chrome (owns zone; EdgeNavPolicy is pure geometry).
    EdgeNavPolicy::Zone hoverEdge() const;
    EdgeNavPolicy::Zone edgeZoneAt(const QPoint &viewPos) const;
    int edgeZoneWidth() const;
    int edgeZoneHeight() const;
    bool setHoverEdge(EdgeNavPolicy::Zone zone);
    void clearHoverEdge();
    void updateHoverEdge(const QPoint &viewPos);
    void drawEdgeAffordances(QPainter &painter) const;

    /** Soft reload focused classic path (Image mode). */
    void reloadFromDisk();
    /** Hard reload focused classic path — purge Store tiles then re-decode. */
    void hardReloadFromDisk();

    // Image-mode framing / sticky pan (ViewFraming state remains on the host).
    void captureStickyPanAnchor(ImageItem *item);
    void restoreStickyPanAnchor(ImageItem *item);
    void applyImageModeFraming(ImageItem *item);
    void preserveImageViewOnLogicalSizeChange(ImageItem *item,
                                              const QSize &before,
                                              const QSize &after);
    void syncImageModeSceneRect(ImageItem *item);

    // Z-key / Workspace Zoom-tool rubber-band (owns ZoomRegionGesture).
    ZoomRegionGesture &zoomRegion() { return m_zoomRegion; }
    const ZoomRegionGesture &zoomRegion() const { return m_zoomRegion; }
    void armZoomRegion();
    void cancelZoomRegion();
    bool tryMousePressZoomRegion(QMouseEvent *event);
    bool tryMouseMoveZoomRegion(QMouseEvent *event);
    bool tryMouseReleaseZoomRegion(QMouseEvent *event);
    bool tryKeyPressZoomRegion(QKeyEvent *event);

private:
    ImageView *m_view = nullptr;
    QString m_classicPath;
    EdgeNavPolicy::Zone m_hoverEdge = EdgeNavPolicy::Zone::None;
    ZoomRegionGesture m_zoomRegion;
};

#endif // IMAGECONTROLLER_H
