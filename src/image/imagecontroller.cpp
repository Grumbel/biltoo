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
#include <QPointer>
#include <QTimer>
#include <QFileInfo>
#include "host/thumtoocache.h"
#include "session/spreadstate.h"
#include "item/itemcomponents.h"
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
        ImageItem *ph = (isPositiveSize(sz) && sz.width() > 1 && sz.height() > 1)
            ? m_view->hostDisplayPipeline().createPlaceholderItem(path, sz)
            : nullptr;
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


namespace {

/** Process-level caches for one path (no durable Store purge). */
void clearProcessCachesForPath(ImageView *view, const QString &path)
{
    if (!view || path.isEmpty()) {
        return;
    }
    ImageCache::remove(path);
    {
        QSize discarded;
        view->hostSizeBook().take(path, &discarded);
    }
    ThumtooCache::forgetCachedSize(path);
    view->hostDisplayPipeline().purgeTilePathRam(path);
    view->hostDisplayPipeline().galleryDecodeResetPath(path);
    for (int edge : ThumtooCache::kLadderEdges) {
        ThumtooCache::forgetPixelsSettled(path, edge);
    }
}

void clearDecodedOnPath(ImageView *view, const QString &path)
{
    if (!view || path.isEmpty()) {
        return;
    }
    for (ImageItem *item : view->liveItems()) {
        if (item && item->path() == path) {
            view->hostDisplayPipeline().dropItemTileLodSession(item);
            view->hostDisplayPipeline().hostClearDecodedPixels(item);
        }
    }
}

} // namespace

void ImageController::reloadFromDisk()
{
    if (!hasClassicPath()) {
        return;
    }
    const QString path = classicPath();
    // Soft F5: regenerate only when the source fingerprint changed.
    ThumtooCache::checkSourceChanged(path, [this, path](bool changed) {
        if (!m_view) {
            return;
        }
        if (!changed) {
            m_view->hostHud().showFlash(ImageView::tr("Reload"),
                ImageView::tr("%1 (unchanged)").arg(QFileInfo(path).fileName()),
                [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
            return;
        }
        // Source changed: drop process caches and re-decode the focused image only.
        // Do not purge durable Store (Shift+F5) and do not touch session binds.
        clearProcessCachesForPath(m_view, path);
        clearDecodedOnPath(m_view, path);
        ThumtooCache::scheduleProbe(path);
        m_view->hostDisplayPipeline().scheduleImageLoad(
            path, static_cast<int>(ImageView::LoadReplace));
        m_view->hostHud().showFlash(ImageView::tr("Reload"), QFileInfo(path).fileName(),
            [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
        emit m_view->statusChanged();
    });
}

void ImageController::hardReloadFromDisk()
{
    if (!hasClassicPath()) {
        return;
    }
    const QString path = classicPath();
    // Image mode: current image only — never fan out to other live items.
    QList<ImageItem *> targets;
    if (ImageItem *primary = m_view->primaryItem()) {
        if (primary->path() == path) {
            targets.append(primary);
        }
    }
    if (targets.isEmpty()) {
        for (ImageItem *item : m_view->liveItems()) {
            if (item && item->path() == path) {
                targets.append(item);
                break;
            }
        }
    }

    // Process-level clear on the focused path; durable purge below.
    clearProcessCachesForPath(m_view, path);
    for (ImageItem *item : targets) {
        m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        m_view->hostDisplayPipeline().hostClearDecodedPixels(item);
    }

    m_view->hostHud().showFlash(ImageView::tr("Hard reload"), QFileInfo(path).fileName(),
        [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });

    // Keep the existing ImageItem; do not queue PendingSessionBind (LoadAdd
    // binds create / rebind slots and have been observed to drop session rows).
    ThumtooCache::purgePathDurable(path, [this, path](qint64 tiles) {
        if (!m_view) {
            return;
        }
        // Text layers were dropped with the Store purge; reinstall if the overlay
        // is active so boxes track a fresh extract (not the old page_y_up blob).
        m_view->hostText().refresh();
        ThumtooCache::scheduleProbe(path);
        m_view->hostDisplayPipeline().scheduleImageLoad(
            path, static_cast<int>(ImageView::LoadReplace));
        if (tiles > 0) {
            m_view->hostHud().showFlash(ImageView::tr("Hard reload"),
                ImageView::tr("%1 Store tiles removed").arg(tiles),
                [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
        }
        emit m_view->statusChanged();
    });
}

void ImageController::prepareModeCanvas()
{
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        stack->clear();
    }
    // Clear view-level override first (scene-only clear leaves it sticky).
    m_view->setSceneRect(QRectF());
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
    m_spreadLayoutPaths.clear();
    m_spreadLastUnion = QRectF();
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
    if (m_view->liveItems().size() != 1 || !m_view->isImageMode()) {
        return;
    }
    ImageItem *item = m_view->liveItems().first();
    // Sticky Fit/Fill/1:1 is a *policy*, not only the current fitMode flag.
    // Fullscreen / chrome hide changes the viewport; re-apply the pinned kind
    // so framing matches the new size (fitMode alone misses sticky Actual and
    // any path that cleared fit flags while sticky stayed on).
    if (m_framing.isStickyZoomEnabled()) {
        applyImageModeFraming(item);
        return;
    }
    if (m_framing.isFitMode()) {
        fitItem(item, framing().aspectMode());
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
    m_view->setSceneRect(QRectF());
    scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}


void ImageController::applySpreadLayout(const QStringList &paths,
                                        const QVector<SessionImageId> &ids,
                                        SpreadDirection direction,
                                        bool forceFit)
{
    if (!m_view || !m_view->isImageMode() || paths.size() < 2) {
        return;
    }

    // Drop underlays that are no longer members. Must use destroyCanvasItem so
    // tile LOD bags unregister — plain delete leaves tickTileLod asserting.
    {
        QSet<QString> keep;
        for (const QString &p : paths) {
            if (!p.isEmpty()) {
                keep.insert(p);
            }
        }
        QList<ImageItem *> doomed;
        for (ImageItem *item : m_view->liveItems()) {
            if (!item) {
                continue;
            }
            if (!keep.contains(item->path())) {
                doomed.append(item);
            }
        }
        for (ImageItem *item : doomed) {
            m_view->destroyCanvasItem(item, /*persistState=*/false);
        }
    }

    QVector<QSizeF> sizes;
    QList<ImageItem *> items;
    sizes.reserve(paths.size());
    items.reserve(paths.size());

    for (int i = 0; i < paths.size(); ++i) {
        const QString &path = paths.at(i);
        if (path.isEmpty()) {
            continue;
        }
        SessionImageId sid = (i < ids.size()) ? ids.at(i) : kInvalidSessionImageId;
        ImageItem *item = m_view->findItemForPath(path);
        if (!item) {
            const QSize sz = m_view->contentLayoutSize(path, sid);
            if (sz.width() > 1 && sz.height() > 1) {
                item = m_view->hostDisplayPipeline().createPlaceholderItem(path, sz);
            }
        }
        if (!item) {
            continue;
        }
        if (sid != kInvalidSessionImageId) {
            item->setSessionId(sid);
        }
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull() && !item->hasDisplayPixels()) {
            m_view->hostDisplayPipeline().hostSetPreviewImage(item, cached);
        } else if (!item->hasDisplayPixels()) {
            ThumtooCache::scheduleProbe(path);
            ThumtooCache::scheduleStoreUnderlaySeed(path);
            m_view->hostDisplayPipeline().requestEscalateClimb(
                path, ThumtooCache::kBatchOverviewEdge);
        }
        QSizeF sz = item->displayContentRect().size();
        if (sz.width() < 1 || sz.height() < 1) {
            const QSize isz = m_view->contentLayoutSize(path, sid);
            sz = QSizeF(isz);
        }
        sizes.append(sz);
        items.append(item);
    }
    if (items.size() < 2) {
        return;
    }

    if (!paths.isEmpty() && !paths.first().isEmpty()) {
        m_classicPath = paths.first();
    }

    const SpreadLayoutResult layout =
        layoutSpread(sizes, /*gutter=*/12.0, /*heightMatch=*/true, direction);
    for (int i = 0; i < items.size() && i < layout.memberSlots.size(); ++i) {
        ImageItem *item = items.at(i);
        ItemComponents::Placement pl = item->placement();
        pl.pos = layout.memberSlots.at(i).rect.topLeft();
        pl.scale = 1.0;
        pl.scaleY = 1.0;
        pl.rotation = 0.0;
        const QRectF local = item->displayContentRect();
        if (local.height() > 1.0 && layout.memberSlots.at(i).rect.height() > 1.0) {
            const qreal s = layout.memberSlots.at(i).rect.height() / local.height();
            if (s > 0.01 && s < 100.0 && qAbs(s - 1.0) > 0.001) {
                pl.scale = s;
                pl.scaleY = s;
            }
        }
        item->applyPlacement(pl);
    }

    QRectF contentUnion;
    for (ImageItem *item : items) {
        if (item) {
            contentUnion = contentUnion.united(item->sceneBoundingRect());
        }
    }
    if (!contentUnion.isValid() || contentUnion.isEmpty()) {
        contentUnion = layout.unionRect;
    }
    if (!contentUnion.isValid() || contentUnion.isEmpty()) {
        return;
    }
    const QRectF padded = contentUnion.adjusted(-8, -8, 8, 8);

    // Image-mode rule: scene owns sceneRect; never install a view-level
    // setSceneRect override (that clamps scroll and leaves content top-stuck).
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        if (!m_view->sceneRect().isNull()) {
            m_view->setSceneRect(QRectF());
        }
        scene->setSceneRect(padded);
    }

    const bool membershipChanged = (paths != m_spreadLayoutPaths);
    const bool unionGrew =
        m_spreadLastUnion.isEmpty()
        || contentUnion.width() > m_spreadLastUnion.width() * 1.15
        || contentUnion.height() > m_spreadLastUnion.height() * 1.15;
    m_spreadLayoutPaths = paths;
    m_spreadLastUnion = contentUnion;

    if (forceFit || membershipChanged || unionGrew) {
        m_view->setAlignment(Qt::AlignCenter);
        m_view->resetTransform();
        m_view->fitInView(contentUnion, Qt::KeepAspectRatio);
        // Scroll ranges settle after sceneRect/fit — re-center like single-page.
        m_view->centerOn(contentUnion.center());
        m_view->refreshScrollBarGeometry();
        const QPointer<ImageView> guard(m_view);
        const QRectF centerTarget = contentUnion;
        QTimer::singleShot(0, m_view, [guard, centerTarget]() {
            ImageView *const view = guard.data();
            if (!view || !view->isImageMode() || !view->viewport()) {
                return;
            }
            view->centerOn(centerTarget.center());
            view->refreshScrollBarGeometry();
        });
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}
