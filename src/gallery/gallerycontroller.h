// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYCONTROLLER_H
#define GALLERYCONTROLLER_H

#include <QList>
#include <QVector>
#include <QRectF>
#include <QSizeF>
#include <QPointF>
#include <QString>
#include <QPoint>
#include <QStringList>
#include "imageview_types.h"
#include "session/packorderview.h"
#include "gallery/gallerysizeresolve.h"
#include "gallery/gallerydecodebook.h"
#include "gallery/layoutprefs.h"
#include "gallery/layoutdebounce.h"
#include "gallery/galleryrelayoutsuppress.h"
#include "gallery/layoutapplyguard.h"

class ImageView;
class ImageItem;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QTimer;
class QPainter;
class QPoint;

/**
 * Gallery-mode collaborator for ImageView.
 *
 * Owns Gallery-private state (tile stash, viewport snapshot, selection anchor,
 * hover path), GallerySizeResolve, GalleryDecodeBook, LayoutPrefs + pack
 * debounce/suppress/apply guards, and enter/leave/return transition helpers.
 * ImageView remains the QGraphicsView shell and public API surface.
 */
class GalleryController : public GallerySizeResolveHost
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
    /**
     * Return from Image mode into Gallery. Restores the Gallery tile stash when
     * present (same ImageItem* cells — no size/membership rebuild).
     * @return true when the stash was restored (warm path; caller should not
     *         run populateGalleryCanvas / setWorkspacePaths).
     */
    bool returnFromImage(int layoutMode, const QString &focusPath = QString(),
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

    /** Focus/reveal live gallery tiles (selection + ensureVisible + hover path). */
    void focusItem(ImageItem *item);
    void focusSessionId(SessionImageId sessionId);
    void focusSessionPath(const QString &path);
    /**
     * Emit session/gallery focus signals for @p item (id-safe path guard).
     * Prefer SessionImageId when it still matches the tile path in the document.
     */
    void emitItemFocus(ImageItem *item);
    /**
     * Request open of @p item in Image mode (id-safe path guard).
     * Prefer SessionImageId when it still matches the tile path in the document.
     */
    void emitItemOpenInImageMode(ImageItem *item);
    void revealPath(const QString &path);
    void revealSessionId(SessionImageId sessionId);
    /** Enter Gallery mode (or switch packaged layout if already Gallery). */
    void enterGallery(LayoutMode packagedLayout);

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
    /** Gallery double-click: open tile in Image mode. */
    bool tryMouseDoubleClick(QMouseEvent *event);
    /** Start session-row QDrag when armed and past drag distance. */
    bool tryMouseMoveGalleryDrag(QMouseEvent *event);
    void clearGalleryDragArm();
    bool tryKeyPressGallery(QKeyEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);

    /** Debounced HUD/status refresh while LQIP installs progress. */
    void scheduleStatusRefresh(int delayMs = 100);
    /** Debounced viewport LQIP/tile decode window (scroll). */
    void scheduleDecodeWindowRefresh(int delayMs = 48);
    /** Viewport resized in Gallery: re-arm decode window (no pack). */
    void onViewResized();
    void updateDecodeWindow();
    /** During size gate: coalesced rebuildVirtualPlan + syncVirtualWindow. */
    void scheduleSizeGatePlanRefresh();
    void applyLayout(GalleryPackReason reason);
    /**
     * Ensure live ImageItems only for the viewport window (virtualized).
     * Full session lives in path order + size book + layout plan — not on the scene.
     * @return always false (no create-chunk re-arm; scroll triggers sync).
     */
    bool ensurePlaceholders();
    /** Recompute pack poses for the whole session without creating items. */
    void rebuildVirtualPlan();
    /** Create/destroy ImageItems so only near-viewport slots are live. */
    void syncVirtualWindow();
    /** Scene-space cell frames for slots without/under live items. */
    void paintVirtualPlaceholders(QPainter *painter, const QRectF &exposed) const;
    void decodeWatchdogTick();
    /** Continuous Gallery soft-pixel repaint recovery (parented to ImageView). */
    void startDecodeWatchdog();
    void stopDecodeWatchdog();
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


    GallerySizeResolve &sizeResolve() { return m_sizeResolve; }
    const GallerySizeResolve &sizeResolve() const { return m_sizeResolve; }

    /** Per-path Gallery decode-window state (pipeline host surface). */
    GalleryDecodeBook &decodeBook() { return m_decodeBook; }
    const GalleryDecodeBook &decodeBook() const { return m_decodeBook; }

    /** Packaged layout mode + grid/masonry prefs (also FreeForm for Workspace). */
    LayoutPrefs &layout() { return m_layout; }
    const LayoutPrefs &layout() const { return m_layout; }

    LayoutDebounce &layoutDebounce() { return m_layoutDebounce; }
    const LayoutDebounce &layoutDebounce() const { return m_layoutDebounce; }

    /** Debounce QTimer (parented to ImageView shell). */
    QTimer *layoutDebounceTimer() const { return m_layoutDebounceTimer; }
    void requestDebouncedPack(GalleryPackReason reason);
    void stopLayoutDebounceTimer();

    GalleryRelayoutSuppress &relayoutSuppress() { return m_relayoutSuppress; }
    const GalleryRelayoutSuppress &relayoutSuppress() const { return m_relayoutSuppress; }

    LayoutApplyGuard &layoutApply() { return m_layoutApply; }
    const LayoutApplyGuard &layoutApply() const { return m_layoutApply; }

    // GallerySizeResolveHost
    bool hasDefinitiveHostSize(const QString &path) const override;
    void adoptResolvedSize(const QString &path, const QSize &size) override;
    void adoptSizeProbeFailed(const QString &path) override;
    void scheduleSizeProbeBatch(const QStringList &paths) override;
    QStringList sizeResolvePathOrder() const override;
    bool sizeResolveLayoutDefersPopulate() const override;
    void setSizeResolveProgress(const QString &title, const QString &detail) override;
    void clearSizeResolveProgress() override;
    void onSizeResolveGateComplete() override;
    void onSizeResolveGateCancelled() override;
    void onSizeResolvePathSettled(const QString &path) override;

    static bool layoutDefersPopulateUntilSizes(LayoutMode mode);

private:

    // Soft install / HUD / canvas helpers (no external callers)
    int galleryInstallLqipOntoBlanks(int maxInstalls, bool *morePending = nullptr);
    void updateSoftProgressHud();
    void prepareCanvas();

    /** Rebuild view path-order book from live tiles (layout switch). */
    void setPathOrderFromLiveItems();

    ImageView *m_view = nullptr;
    GallerySizeResolve m_sizeResolve;
    GalleryDecodeBook m_decodeBook;
    LayoutPrefs m_layout;
    LayoutDebounce m_layoutDebounce;
    QTimer *m_layoutDebounceTimer = nullptr;
    QTimer *m_decodeWatchdogTimer = nullptr;
    GalleryRelayoutSuppress m_relayoutSuppress;
    LayoutApplyGuard m_layoutApply;

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
    QTimer *m_sizeGatePlanTimer = nullptr;

    /** Offline Gallery layout: one slot per session row (not a QGraphicsItem). */
    struct VirtualSlot {
        QString path;
        SessionImageId id = kInvalidSessionImageId;
        QSizeF layoutSize;
        QPointF center;
        qreal scale = 1.0;
        QSizeF cellSize; // GridCrop clip; empty otherwise
        QRectF bounds; // scene bounds of packed cell
    };
    QVector<VirtualSlot> m_virtualSlots;
    QRectF m_virtualSceneBounds;
    int m_virtualPlanGeneration = 0;
};

#endif // GALLERYCONTROLLER_H
