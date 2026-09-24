// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGECONTROLLER_H
#define IMAGECONTROLLER_H

#include <QString>
#include "image/edgenavpolicy.h"
#include "slideshow/zoomregiongesture.h"
#include "session/sessionchrome.h"
#include "view/viewframing.h"
#include "color/coloradjustcommit.h"
#include "session/sessionappearance.h"
#include <QSize>

class ImageView;
class ImageItem;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;
class QPoint;

/**
 * Image-mode collaborator for ImageView.
 *
 * Owns the classic (single-image) path, Image-mode enter transition,
 * framing/sticky pan, edge nav, content flip/rotate (all modes; pipeline
 * bake with mode post-steps), and interactive colour-grade + deferred commit.
 * Gallery/Workspace leave (stash) runs in those controllers' onLeave before
 * enter is called. ImageView remains the QGraphicsView shell and public API.
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

    /** Reset undo, view transform, scene rect, framing for Image enter. */
    void prepareModeCanvas();
    /** Remove scene items without deleting mode-stashed tiles. */
    void clearSceneKeepingStashes();

    /** Content flip / quarter-turn rotate (all modes; pipeline bake + mode post). */
    void flipHorizontal();
    void flipVertical();
    void rotateContentByQuarterTurns(ImageItem *item, int quarterTurns);
    void rotateLeft();
    void rotateRight();

    /** Image-mode framing / sticky pan (per-view; dual-safe). */
    ViewFraming &framing() { return m_framing; }
    const ViewFraming &framing() const { return m_framing; }

    void captureStickyPanAnchor(ImageItem *item);
    void restoreStickyPanAnchor(ImageItem *item);
    void applyImageModeFraming(ImageItem *item);
    /** Fit item in view (Image-mode layout + crop-draft rules). */
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode);
    void zoomFit();
    void zoomFill();
    void zoomReset();
    void zoomViewBy(qreal factor);
    void zoomIn();
    void zoomOut();
    /** Workspace overview scale ≈ four zoom-out steps (41%). */
    void setWorkspaceDefaultViewScale();
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

    /** Image-mode edge/session nav flags (prev/next + gallery return). */
    SessionNavFlags &sessionNav() { return m_sessionNav; }
    const SessionNavFlags &sessionNav() const { return m_sessionNav; }

    /** Pending durable colour-grade commit target + debounce timer. */
    ColorAdjustCommit &colorAdjustCommit() { return m_colorAdjustCommit; }
    const ColorAdjustCommit &colorAdjustCommit() const { return m_colorAdjustCommit; }
    /** Arm debounce; timeout calls flushColorAdjustCommit. */
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);
    void stopColorAdjustCommitTimer();
    /** Interactive slider path + deferred durable rematerialize. */
    void setTargetColorAdjustments(const ColorAdjustments &adj);
    void flushColorAdjustCommit();

private:
    void ensureColorAdjustCommitTimer();
    void applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want);

    ImageView *m_view = nullptr;
    ViewFraming m_framing;
    QString m_classicPath;
    EdgeNavPolicy::Zone m_hoverEdge = EdgeNavPolicy::Zone::None;
    ZoomRegionGesture m_zoomRegion;
    SessionNavFlags m_sessionNav;
    ColorAdjustCommit m_colorAdjustCommit;
    QTimer *m_colorAdjustCommitTimer = nullptr;
};

#endif // IMAGECONTROLLER_H
