// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYCONTROLLER_H
#define GALLERYCONTROLLER_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include "imageview_types.h"

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
    void restoreViewport(const QString &focusPath = QString());
    void applyPendingRestore();
    void reassertViewport();

    /** Gallery → other mode: clear hover/anchor, stop pack; stash tiles when next is Image. */
    void onLeave(int nextMode);
    void leaveForImageMode();
    void returnFromImage(int layoutMode, const QString &focusPath = QString());
    void enter(int packagedLayout);

    bool hasStash() const { return !m_stashedItems.isEmpty(); }
    QList<ImageItem *> &stashedItems() { return m_stashedItems; }
    const QList<ImageItem *> &stashedItems() const { return m_stashedItems; }

    bool pendingRestore() const { return m_pendingRestore; }

    /** @return true when the pending-restore flag changed. */
    bool setPendingRestore(bool v)
    {
        if (m_pendingRestore == v) {
            return false;
        }
        m_pendingRestore = v;
        return true;
    }

    bool haveViewCenter() const { return m_haveViewCenter; }
    void clearViewCenter() { m_haveViewCenter = false; }
    QPointF viewCenter() const { return m_viewCenter; }

    bool haveScroll() const { return m_haveScroll; }
    void clearScroll() { m_haveScroll = false; }
    int scrollH() const { return m_scrollH; }
    int scrollV() const { return m_scrollV; }

    QString focusPath() const { return m_focusPath; }

    /** @return true when focus path changed. */
    bool setFocusPath(const QString &path)
    {
        if (m_focusPath == path) {
            return false;
        }
        m_focusPath = path;
        return true;
    }

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
    bool tryKeyPressGallery(QKeyEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);

    /** Debounced HUD/status refresh while soft installs progress. */
    void scheduleStatusRefresh(int delayMs = 100);
    /** Debounced viewport soft/LQIP decode window (scroll/climb). */
    void scheduleDecodeWindowRefresh(int delayMs = 48);
    int galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending = nullptr);
    void updateDecodeWindow();
    void applyLayout(GalleryPackReason reason);
    void ensurePlaceholders();

    void setViewportSnapshot(const QPointF &center, int scrollH, int scrollV)
    {
        m_viewCenter = center;
        m_haveViewCenter = !center.isNull();
        m_scrollH = scrollH;
        m_scrollV = scrollV;
        m_haveScroll = true;
    }

private:
    ImageView *m_view = nullptr;

    QList<ImageItem *> m_stashedItems;
    QStringList m_stashedPathOrder;

    int m_scrollH = 0;
    int m_scrollV = 0;
    bool m_haveScroll = false;
    QPointF m_viewCenter;
    bool m_haveViewCenter = false;
    QString m_focusPath;
    bool m_pendingRestore = false;


    ImageItem *m_selectionAnchor = nullptr;
    QString m_hoverPath;

    QTimer *m_statusRefreshTimer = nullptr;
    QTimer *m_decodeScrollTimer = nullptr;
};

#endif // GALLERYCONTROLLER_H
