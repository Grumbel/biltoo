// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYCONTROLLER_H
#define GALLERYCONTROLLER_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QPoint>
#include <QStringList>
#include "imageview_types.h"
#include "packorderview.h"

class ImageView;
class ImageItem;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QTimer;
class QPoint;

/**
 * Gallery-mode collaborator for ImageView.
 *
 * Owns Gallery-private state (tile stash, viewport snapshot, selection anchor,
 * hover path) and the Gallery enter / leave / return transition helpers.
 * ImageView remains the QGraphicsView shell and public API surface.
 */
class GalleryController
{
public:
    explicit GalleryController(ImageView *view);

    void discardStash();
    void stashItems();
    void restoreStashedItems();

    void snapshotViewport();
    void restoreViewport(const QString &focusPath = QString(),
                         SessionImageId focusId = kInvalidSessionImageId);
    void applyPendingRestore();
    void reassertViewport();

    /** Gallery → other mode: clear hover/anchor, stop pack; stash tiles when next is Image. */
    void onLeave(int nextMode);
    void leaveForImageMode();
    void returnFromImage(int layoutMode, const QString &focusPath = QString(),
                         SessionImageId focusId = kInvalidSessionImageId);
    void enter(int packagedLayoutInt, int previousModeInt = -1);

    QList<ImageItem *> &stashedItems() { return m_stashedItems; }
    const QList<ImageItem *> &stashedItems() const { return m_stashedItems; }

    ImageItem *selectionAnchor() const { return m_selectionAnchor; }
    void setSelectionAnchor(ImageItem *item) { m_selectionAnchor = item; }

    QString hoverPath() const { return m_hoverPath; }

    /** @return true when hover path changed. */
    bool setHoverPath(const QString &path)
    {
        if (m_hoverPath == path) {
            return false;
        }
        m_hoverPath = path;
        return true;
    }

    void clearHoverPath() { m_hoverPath.clear(); }

    /** Drop hover path and selection anchor (leave / wipe). */
    void clearChrome()
    {
        clearHoverPath();
        m_selectionAnchor = nullptr;
    }

    // Input (Tier 6c) — ImageView thin-forwards
    void updateGalleryHoverAt(const QPoint &viewPos);
    bool tryWheelGalleryZoom(QWheelEvent *event);
    bool tryWheelGalleryScroll(QWheelEvent *event);
    bool tryMousePressGalleryRight(QMouseEvent *event);
    bool tryMousePressGalleryLeft(QMouseEvent *event);
    /** Start session-row QDrag when armed and past drag distance. */
    bool tryMouseMoveGalleryDrag(QMouseEvent *event);
    void clearGalleryDragArm();
    bool tryKeyPressGallery(QKeyEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);

    /** Debounced HUD/status refresh while LQIP installs progress. */
    void scheduleStatusRefresh(int delayMs = 100);
    /** Debounced viewport LQIP/tile decode window (scroll). */
    void scheduleDecodeWindowRefresh(int delayMs = 48);
    void updateDecodeWindow();
    void applyLayout(GalleryPackReason reason);
    void ensurePlaceholders();
    void decodeWatchdogTick();
    void setGridColumns(int columns);
    void setMasonryColumns(int columns);
    void setMasonryRows(int rows);
    /** Packaged Gallery layout only (FreeForm is Workspace). */
    void setLayoutMode(LayoutMode mode);
    void reloadFromDisk(bool relayout = true);
    void hardReloadFromDisk(bool relayout = true);
    void setRelayoutSuppressed(bool on);
    void invalidateDecodes();

    void setViewportSnapshot(const QPointF &center, int scrollH, int scrollV)
    {
        m_viewCenter = center;
        m_haveViewCenter = !center.isNull();
        m_scrollH = scrollH;
        m_scrollV = scrollV;
        m_haveScroll = true;
    }

private:

    // Soft install / HUD / canvas helpers (no external callers)
    int galleryInstallLqipOntoBlanks(int maxInstalls, bool *morePending = nullptr);
    void updateSoftProgressHud();
    void prepareCanvas();

    /** Rebuild view path-order book from live tiles (layout switch). */
    void setPathOrderFromLiveItems();

    ImageView *m_view = nullptr;

    QList<ImageItem *> m_stashedItems;
    PackOrderView m_stashedPackOrder;

    int m_scrollH = 0;
    int m_scrollV = 0;
    bool m_haveScroll = false;
    QPointF m_viewCenter;
    bool m_haveViewCenter = false;
    QString m_focusPath;
    SessionImageId m_focusSessionId = kInvalidSessionImageId;
    bool m_pendingRestore = false;


    ImageItem *m_selectionAnchor = nullptr;

    /** Gallery canvas session reorder drag (press + threshold → QDrag). */
    bool m_dragArmed = false;
    QPoint m_dragStartViewPos;
    ImageItem *m_dragPressItem = nullptr;  // not QObject — no QPointer
    QString m_hoverPath;

    QTimer *m_statusRefreshTimer = nullptr;
    QTimer *m_decodeScrollTimer = nullptr;
};

#endif // GALLERYCONTROLLER_H
