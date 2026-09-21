// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecontroller.h"
#include "imageview.h"
#include "sessionbindbook.h"
#include "imageview_types.h"
#include <memory>
#include <QSet>
#include <QFileInfo>
#include "thumtoocache.h"
#include "imagecache.h"
#include "imageitem.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>

ImageController::ImageController(ImageView *view)
    : m_view(view)
{
}

QString ImageController::takeClassicPath()
{
    const QString path = m_classicPath;
    m_classicPath.clear();
    return path;
}

void ImageController::enter()
{
    // Gallery/Workspace → Image: stash already done in the matching onLeave.
    m_view->setActiveMode(ImageView::ViewMode::Image, LayoutMode::FreeForm);
    m_view->stopDeferredPacking();
    m_view->prepareImageModeCanvas();
    // Prefer explicit classic path (MainWindow pins the open target before
    // leaveForImageMode). Do not pick live Workspace/Gallery items — that
    // re-decodes a random tile before setCurrentIndex runs.
    const QString path = takeClassicPath();
    // Clear live canvas only — do not discard stashes.
    m_view->clearLiveCanvas();
    m_view->hostDisplayPipeline().loadGate().clearPending();
    m_view->clearSceneKeepingStashes();
    m_view->scheduleReplaceLoad(path);
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
    const ImageView::EdgeZone zone = m_view->edgeZoneAt(event->pos());
    if (zone == ImageView::EdgeZone::GalleryReturn) {
        emit m_view->galleryReturnRequested();
        event->accept();
        return true;
    }
    if (zone == ImageView::EdgeZone::Previous) {
        emit m_view->navigatePreviousRequested();
        event->accept();
        return true;
    }
    if (zone == ImageView::EdgeZone::Next) {
        emit m_view->navigateNextRequested();
        event->accept();
        return true;
    }
    // Slideshow: centre click pauses / resumes. Edges stay navigation above.
    // Ignore the second press of a double-click so we do not toggle twice.
    if ((m_view->hostSlideshow().hud().isProgressActive() || m_view->hostSlideshow().hud().isPausedHud())
        && zone == ImageView::EdgeZone::None) {
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
        m_view->hostDisplayPipeline().gallerySoftResetPath(p);
        m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        m_view->takePendingWorkspacePath(p);
        m_view->clearItemDecodedPixels(item);
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
