// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/biltoo_thread.h"
#include "image/imagecontroller.h"
#include "display/displaypipelinecontroller.h"
#include "image/edgenavpolicy.h"
#include "imageview.h"
#include "session/sessionbindbook.h"
#include "imageview_types.h"
#include <memory>
#include <QSet>
#include <QFileInfo>
#include "host/thumtoocache.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "session/sessionappearance.h"
#include "util/biltoo_logging.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QUndoStack>
#include <QScrollBar>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include "workspace/workspacecontroller.h"
#include "gallery/gallerycontroller.h"

ImageController::ImageController(ImageView *view)
    : m_view(view)
{
}

void ImageController::enter()
{
    GUI_BUDGET("ImageController::enter");
    // Gallery/Workspace → Image: matching onLeave already stashed live tiles.
    // MODE_OWNERSHIP.md: never take ImageItem* out of a mode stash.
    m_view->setActiveMode(ImageView::ViewMode::Image, LayoutMode::FreeForm);
    m_view->hostGallery().stopLayoutDebounceTimer();
    m_view->prepareImageModeCanvas();
    m_view->hostGalleryDecodeBook().setDeferPopulate(false);

    const QString path = classicPath();
    const SessionImageId wantId = m_view->hostSessionId().hasCurrentId()
        ? m_view->hostSessionId().currentIdValue()
        : kInvalidSessionImageId;
    biltooModeDbg("Image::enter path=%s id=%lld live=%d wstash=%d gstash=%d defer=%d",
                  qPrintable(QFileInfo(path).fileName()),
                  static_cast<long long>(wantId),
                  m_view->itemCount(),
                  static_cast<int>(m_view->hostWorkspace().stashedItems().size()),
                  static_cast<int>(m_view->hostGallery().stashedItems().size()),
                  m_view->hostGalleryDecodeBook().isDeferPopulate() ? 1 : 0);

    // Do NOT seed ImageCache from Gallery/Workspace stash display samples.
    // Mode-stash soft may be content-baked; ImageCache is host-raw only.
    // Workspace→Image and Gallery→Image share one path: loadImage → host-raw
    // (cache/LQIP/filmstrip) + installDisplayPixels materialize from ItemWorld.

    m_view->clearLiveCanvas();
    m_view->hostDisplayPipeline().loadGate().clearPending();
    clearSceneKeepingStashes();

    // No XDG-seed on Image enter (Workspace placement-only must stay unoriented).
    if (!path.isEmpty()) {
        // LQIP / host sample + tiles (IMAGE_MODE_NAV_SOFT.md). No soft ladder.
        m_view->hostDisplayPipeline().loadImage(path);
    } else {
        biltooModeDbg("Image::enter EMPTY classicPath — no loadImage");
    }
    // Structural guarantee: Image mode always has an underlay for classicPath.
    // loadImage / pendingTile must not leave live empty (prior: sceneRect wipe
    // or createPlaceholder refused).
    if (!path.isEmpty() && m_view->itemCount() == 0) {
        biltooModeDbg("Image::enter FORCE underlay path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        const QSize sz = m_view->contentLayoutSize(path, wantId);
        ImageItem *ph = m_view->hostDisplayPipeline().createPlaceholderItem(
            path, isPositiveSize(sz) ? sz : QSize(1, 1));
        if (ph) {
            m_view->hostDisplayPipeline().bindImageModeSessionCursor(ph);
            m_view->syncImageModeSceneRect(ph);
            m_view->applyImageModeFraming(ph);
            const QImage cached = ImageCache::get(path);
            if (!cached.isNull()) {
                m_view->hostDisplayPipeline().installDisplayPixels(
                    ph, cached, SessionAppearance::PixelKind::SoftPreview,
                    wantId);
            }
        }
    }
    biltooModeDbg("Image::enter done live=%d hasPixels=%d path=%s",
                  m_view->itemCount(),
                  (m_view->itemCount() > 0 && m_view->liveItems().first()
                   && m_view->liveItems().first()->hasDisplayPixels())
                      ? 1
                      : 0,
                  qPrintable(QFileInfo(path).fileName()));
    emit m_view->statusChanged();
}


// --- Image mode session keys (Tier 6h) ---

bool ImageController::tryKeyPressNavigate(QKeyEvent *event)
{
    // Image mode: Left/Right (and friends) navigate the session. QGraphicsView
    // would otherwise scroll the viewport when the image is zoomed or the view
    // has focus (typical in fullscreen), swallowing the QAction shortcuts.
    if (!m_view->isImageMode()
        || (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit m_view->navigatePreviousRequested();
        event->accept();
        return true;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
        emit m_view->navigateNextRequested();
        event->accept();
        return true;
    default:
        return false;
    }
}


// --- Image mode edge chrome (Tier 6i) ---

bool ImageController::tryMousePressEdges(QMouseEvent *event)
{
    if (!m_view->isImageMode() || event->button() != Qt::LeftButton
        || (event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        return false;
    }
    const EdgeNavPolicy::Zone zone = edgeZoneAt(event->pos());
    if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
        emit m_view->galleryReturnRequested();
        event->accept();
        return true;
    }
    if (zone == EdgeNavPolicy::Zone::Previous) {
        emit m_view->navigatePreviousRequested();
        event->accept();
        return true;
    }
    if (zone == EdgeNavPolicy::Zone::Next) {
        emit m_view->navigateNextRequested();
        event->accept();
        return true;
    }
    // Slideshow: centre click pauses / resumes. Edges stay navigation above.
    // Ignore the second press of a double-click so we do not toggle twice.
    if ((m_view->hostSlideshow().hud().isProgressActive() || m_view->hostSlideshow().hud().isPausedHud())
        && zone == EdgeNavPolicy::Zone::None) {
        if (m_view->hostSlideshow().lastCenterClick().isValid()
            && m_view->hostSlideshow().lastCenterClick().elapsed()
                < QApplication::doubleClickInterval()) {
            event->accept();
            return true;
        }
        m_view->hostSlideshow().lastCenterClick().start();
        emit m_view->slideshowTogglePauseRequested();
        event->accept();
        return true;
    }
    return false;
}


void ImageController::reloadFromDisk()
{
    if (!hasClassicPath()) {
        return;
    }
    const QString path = classicPath();
    // Drop retained path tiles so Reload cannot paint pre-reload grid cells.
    m_view->hostDisplayPipeline().purgeTilePathRam(path);
    // Force a fresh decode of the focused session image only.
    m_view->scheduleReplaceLoad(path);
    m_view->flashHud(ImageView::tr("Reload"), QFileInfo(path).fileName());
}

void ImageController::hardReloadFromDisk()
{
    if (!hasClassicPath()) {
        return;
    }
    QList<ImageItem *> targets;
    if (ImageItem *primary = m_view->primaryItem()) {
        targets.append(primary);
    } else {
        for (ImageItem *item : m_view->liveItems()) {
            if (item && item->path() == classicPath()) {
                targets.append(item);
                break;
            }
        }
    }
    const QString path = classicPath();
    if (targets.isEmpty()) {
        // No item yet — still purge Store + process caches, then LoadReplace.
        ImageCache::remove(path);
        m_view->hostDisplayPipeline().purgeTilePathRam(path);
        for (int edge : ThumtooCache::kLadderEdges) {
            ThumtooCache::forgetPixelsSettled(path, edge);
        }
        m_view->flashHud(ImageView::tr("Hard reload"), QFileInfo(path).fileName());
        ThumtooCache::purgePathDurable(path, [this, path](qint64 /*tiles*/) {
            ThumtooCache::scheduleProbe(path);
            m_view->scheduleReplaceLoad(path);
            emit m_view->statusChanged();
        });
        return;
    }

    // Image mode with live item: same multi-item hard path restricted to targets.
    struct ReloadBind {
        QString path;
        SessionImageId id = kInvalidSessionImageId;
        int index = -1;
    };
    QList<ReloadBind> binds;
    QSet<QString> paths;
    for (ImageItem *item : targets) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const QString p = item->path();
        paths.insert(p);
        ReloadBind b;
        b.path = p;
        b.id = item->sessionId();
        b.index = m_view->sessionListIndex(item);
        binds.append(b);
        m_view->hostDisplayPipeline().galleryDecodeResetPath(p);
        m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        m_view->takePendingWorkspacePath(p);
        m_view->hostDisplayPipeline().hostClearDecodedPixels(item);
        ImageCache::remove(p);
        for (int edge : ThumtooCache::kLadderEdges) {
            ThumtooCache::forgetPixelsSettled(p, edge);
        }
    }
    if (paths.isEmpty()) {
        return;
    }
    m_view->flashHud(ImageView::tr("Hard reload"), QFileInfo(path).fileName());

    auto remaining = std::make_shared<int>(paths.size());
    auto tileTotal = std::make_shared<qint64>(0);
    auto finish = [this, binds, tileTotal]() {
        QSet<QString> probed;
        for (const ReloadBind &b : binds) {
            PendingSessionBind pending;
            pending.path = b.path;
            pending.id = b.id;
            pending.index = b.index;
            m_view->hostBindBook().append(pending);
            if (!probed.contains(b.path)) {
                ThumtooCache::scheduleProbe(b.path);
                probed.insert(b.path);
            }
            m_view->scheduleReplaceLoad(b.path);
        }
        if (*tileTotal > 0) {
            m_view->flashHud(ImageView::tr("Hard reload"),
                             ImageView::tr("%1 Store tiles removed").arg(*tileTotal));
        }
        emit m_view->statusChanged();
    };

    for (const QString &p : paths) {
        ThumtooCache::purgePathDurable(p, [remaining, tileTotal, finish](qint64 tiles) {
            *tileTotal += tiles;
            if (--(*remaining) == 0) {
                finish();
            }
        });
    }
}

void ImageController::prepareModeCanvas()
{
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        stack->clear();
    }
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->clearSelection();
        // Drop large Gallery/Workspace scene rects so fitInView centres cleanly.
        scene->setSceneRect(QRectF());
    }
    m_view->resetTransform();
    if (QScrollBar *h = m_view->horizontalScrollBar()) {
        h->setValue(0);
    }
    if (QScrollBar *v = m_view->verticalScrollBar()) {
        v->setValue(0);
    }
    m_framing.setFitOnly();
}

void ImageController::clearSceneKeepingStashes()
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    // Do not QGraphicsScene::clear() — that deletes every item still parented to
    // the scene. Workspace/Gallery stashes are supposed to be off-scene, but if a
    // tile is still parented, clear() would free it and leave a dangling stash
    // pointer (Workspace re-enter → empty canvas).
    QSet<ImageItem *> keep;
    for (ImageItem *item : m_view->hostWorkspace().stashedItems()) {
        if (item) {
            keep.insert(item);
        }
    }
    for (ImageItem *item : m_view->hostGallery().stashedItems()) {
        if (item) {
            keep.insert(item);
        }
    }
    scene->blockSignals(true);
    const QList<QGraphicsItem *> all = scene->items();
    for (QGraphicsItem *gi : all) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (keep.contains(ii)) {
                scene->removeItem(ii);
                continue;
            }
        }
        scene->removeItem(gi);
        delete gi;
    }
    scene->blockSignals(false);
}

void ImageController::onViewResized()
{
    m_view->hostDisplayPipeline().maybeClimbImageModePixelsForView();
    if (m_framing.isFitMode() && m_view->liveItems().size() == 1) {
        fitItem(m_view->liveItems().first(), m_view->currentFitAspectMode());
    }
}

void ImageController::onViewportLeave()
{
    if (m_hoverEdge != EdgeNavPolicy::Zone::None) {
        clearHoverEdge();
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void ImageController::onContentAppearancePropagated(ImageItem *item)
{
    if (!item || !m_view->isImageMode()) {
        return;
    }
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene || item->scene() != scene) {
        return;
    }
    scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}
