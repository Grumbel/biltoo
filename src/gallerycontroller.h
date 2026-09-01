// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYCONTROLLER_H
#define GALLERYCONTROLLER_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

class ImageView;
class ImageItem;

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

    void leaveForImageMode();
    void returnFromImage(int layoutMode, const QString &focusPath = QString());
    void enter(int packagedLayout);

    bool hasStash() const { return !m_stashedItems.isEmpty(); }
    QList<ImageItem *> &stashedItems() { return m_stashedItems; }
    const QList<ImageItem *> &stashedItems() const { return m_stashedItems; }

    bool pendingRestore() const { return m_pendingRestore; }
    void setPendingRestore(bool v) { m_pendingRestore = v; }

    bool haveViewCenter() const { return m_haveViewCenter; }
    void clearViewCenter() { m_haveViewCenter = false; }
    QPointF viewCenter() const { return m_viewCenter; }

    bool haveScroll() const { return m_haveScroll; }
    void clearScroll() { m_haveScroll = false; }
    int scrollH() const { return m_scrollH; }
    int scrollV() const { return m_scrollV; }

    QString focusPath() const { return m_focusPath; }
    void setFocusPath(const QString &path) { m_focusPath = path; }

    ImageItem *selectionAnchor() const { return m_selectionAnchor; }
    void setSelectionAnchor(ImageItem *item) { m_selectionAnchor = item; }

    QString hoverPath() const { return m_hoverPath; }
    void setHoverPath(const QString &path) { m_hoverPath = path; }
    void clearHoverPath() { m_hoverPath.clear(); }


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
};

#endif // GALLERYCONTROLLER_H
