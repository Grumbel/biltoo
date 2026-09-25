// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Gallery focus / reveal (selection + ensureVisible + hover HUD path).
// ImageView keeps thin public routers for MainWindow and host callers.

#include "gallery/gallerycontroller.h"
#include "imageview.h"
#include "session/sessiondocument.h"
#include "imageitem.h"
#include "view/viewtransform.h"

#include <QGraphicsScene>
#include <QWidget>

void GalleryController::focusItem(ImageItem *item)
{
    if (!item || !m_view->canvasScene()) {
        return;
    }
    m_view->canvasScene()->clearSelection();
    item->setSelected(true);
    if (!m_view->isGalleryMode()) {
        return;
    }
    // Open/TTFP: first index is often already in view after pack — ensureVisible
    // on a large scene was hundreds of ms for no visual change.
    bool needScroll = true;
    if (QWidget *vp = m_view->viewport()) {
        const QRectF vis = m_view->mapToScene(vp->rect()).boundingRect();
        if (vis.isValid() && item->sceneBoundingRect().intersects(vis)) {
            needScroll = false;
        }
    }
    if (needScroll) {
        m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                              ViewTransform::kEnsureVisibleMargin);
    }
    // Keyboard focus: show filename in the HUD like mouse hover.
    const QString path = item->path();
    if (setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::focusSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    focusItem(m_view->findItemBySessionId(sessionId));
}

void GalleryController::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Before clearSelection so a selected duplicate wins over first-match.
    focusItem(m_view->findItemForPath(path));
}

void GalleryController::revealPath(const QString &path)
{
    if (path.isEmpty() || !m_view->isGalleryMode()) {
        return;
    }
    // Prefer the selected instance of this path when duplicates exist (LoadAdd).
    ImageItem *item = m_view->findItemForPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                          ViewTransform::kEnsureVisibleMargin);
    if (setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::revealSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId || !m_view->isGalleryMode()) {
        return;
    }
    ImageItem *item = m_view->findItemBySessionId(sessionId);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    m_view->ensureVisible(item, ViewTransform::kEnsureVisibleMargin,
                          ViewTransform::kEnsureVisibleMargin);
    const QString path = item->path();
    if (!path.isEmpty() && setHoverPath(path)) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::enterGallery(LayoutMode packagedLayout)
{
    // Sticky Fit/Fill/1:1 *applies* only in Image mode (applyImageModeFraming).
    // Do not release the preference here — left/right sticky must survive
    // Gallery and still frame the next Image open (see setViewMode comment).
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
    }
    if (m_view->isGalleryMode()) {
        // Layout-only switch inside Gallery (no leave/enter of other modes).
        enter(static_cast<int>(packagedLayout),
              static_cast<int>(ImageView::ViewMode::Gallery));
        return;
    }
    // Full mode switch through the central path so Workspace/Image leave runs
    // and setActiveMode happens before GalleryController::enter.
    // Preserve requested layout for setViewMode's Gallery branch.
    m_view->hostLayout().setMode(packagedLayout);
    m_view->setViewMode(ImageView::ViewMode::Gallery);
}


void GalleryController::emitItemFocus(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Same path↔id guard as emitItemOpenInImageMode: a scrubbed/stale SessionImageId
    // must not move the session cursor to another document row.
    SessionDocument *doc = m_view->sessionDocument();
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        bool idMatchesPath = true;
        if (doc) {
            const int docIdx = doc->indexOfId(sid);
            if (docIdx < 0) {
                idMatchesPath = false;
            } else if (!item->path().isEmpty()) {
                idMatchesPath = (doc->paths().at(docIdx) == item->path());
            }
        }
        if (idMatchesPath) {
            emit m_view->sessionImageFocused(sid);
            return;
        }
    }
    // Unbound or id/path conflict: list-order cache, then Gallery live index.
    int listIdx = m_view->sessionListIndex(item);
    if (listIdx < 0 && m_view->isGalleryMode()) {
        const int live = m_view->liveItems().indexOf(item);
        if (live >= 0 && (!doc || live < doc->size())) {
            listIdx = live;
        }
    }
    if (listIdx < 0 && doc && !item->path().isEmpty()) {
        listIdx = doc->indexOfPathPreferId(item->path());
    }
    if (listIdx >= 0) {
        emit m_view->sessionSlotFocused(listIdx);
        return;
    }
    if (!item->path().isEmpty()) {
        emit m_view->galleryItemFocused(item->path());
    }
}

void GalleryController::emitItemOpenInImageMode(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Prefer SessionImageId only when it still matches this tile's path in the
    // session document. After setItemSessionId conflict scrub, a tile can keep
    // a stale id that belongs to another path — opening by that id would load
    // the wrong file while classicPath may still be set from a prior pin.
    SessionDocument *doc = m_view->sessionDocument();
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        bool idMatchesPath = true;
        if (doc) {
            const int docIdx = doc->indexOfId(sid);
            if (docIdx < 0) {
                idMatchesPath = false;
            } else if (!item->path().isEmpty()) {
                idMatchesPath = (doc->paths().at(docIdx) == item->path());
            }
        }
        if (idMatchesPath) {
            emit m_view->sessionImageOpenRequested(sid);
            return;
        }
    }
    int listIdx = m_view->sessionListIndex(item);
    // Gallery pack order aligns live canvas with session rows after reorder.
    if (listIdx < 0 && m_view->isGalleryMode()) {
        const int live = m_view->liveItems().indexOf(item);
        if (live >= 0 && (!doc || live < doc->size())) {
            listIdx = live;
        }
    }
    // Unbound or id/path conflict: prefer document row by list cache, then path.
    if (listIdx < 0 && doc && !item->path().isEmpty()) {
        listIdx = doc->indexOfPathPreferId(item->path());
    }
    if (listIdx >= 0) {
        emit m_view->sessionSlotOpenRequested(listIdx);
        return;
    }
    if (!item->path().isEmpty()) {
        emit m_view->galleryItemOpenRequested(item->path());
    }
}
