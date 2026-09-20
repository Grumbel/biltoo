// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayquality.h"
#include "imageview.h"
#include "tilelod/tile_lod_registry.hpp"
#include "layoutapplyguard.h"
#include "gallerypackfit.h"
#include "viewtransform.h"
#include <cstdio>
#include <cstdlib>
#include "biltoo_thread.h"
#include "thumtoocache.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "gallerysoftsm.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <cstdio>
#include <QUndoStack>



void ImageView::scheduleGalleryStatusRefresh(int delayMs)
{
    m_gallery.scheduleStatusRefresh(delayMs);
}


void ImageView::scheduleGalleryDecodeWindowRefresh(int delayMs)
{
    m_gallery.scheduleDecodeWindowRefresh(delayMs);
}


int ImageView::galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending)
{
    return m_gallery.galleryInstallHostSoftOntoBlanks(maxInstalls, morePending);
}


void ImageView::publishGalleryInterest(const QStringList &interestNear,
                                       const QStringList &interestRest)
{
    // Gallery drives tiles via TileLoadCoordinator + scheduleTilePyramid only
    // when durable coverage is missing. setInterest Primary used to enqueue
    // FocusFull pyramids (and soft PreferCache) every decode window — that
    // reintroduced multi-second worker load after we stopped unconditional
    // scheduleTilePyramid. Interest is unused for Gallery.
    Q_UNUSED(interestNear);
    Q_UNUSED(interestRest);
}

void ImageView::scheduleIdleGalleryDecodes(const QStringList &rest)
{
    // Soft PreferCache is removed from Gallery. Off-screen work is tiles via
    // the coordinator when cells enter the viewport — no idle soft climb.
    Q_UNUSED(rest);
}


GalleryLayout::Mode ImageView::galleryLayoutModeFromViewMode() const
{
    return GalleryPackFit::modeFromLayoutMode(m_layout.currentMode());
}

void ImageView::updateGalleryDecodeWindow()
{
    m_gallery.updateDecodeWindow();
}


void ImageView::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts belong only to Gallery; FreeForm only to Workspace.
    if (mode == LayoutMode::FreeForm) {
        if (!isWorkspaceMode()) {
            return;
        }
        if (!m_layout.isFreeForm()) {
            // Should not happen in Workspace (always FreeForm).
        }
        m_layout.setMode(LayoutMode::FreeForm);
        for (ImageItem *item : m_items) {
            applyItemModeFlags(item);
        }
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(ViewTransform::padded(m_scene->itemsBoundingRect(),
                                               ViewTransform::kFreeformScenePad));
        }
        m_framing.releaseFit();
        emit statusChanged();
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!isGalleryMode()) {
        enterGallery(mode);
        return;
    }

    if (m_layout.isFreeForm() && mode != LayoutMode::FreeForm) {
        snapshotFreeFormStates();
    }

    m_layout.setMode(mode);
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
    }
    applyLayout(GalleryPackReason::EnterGallery);
}

void ImageView::setGridColumns(int columns)
{
    m_gallery.setGridColumns(columns);
}


void ImageView::setMasonryColumns(int columns)
{
    m_gallery.setMasonryColumns(columns);
}


void ImageView::setMasonryRows(int rows)
{
    m_gallery.setMasonryRows(rows);
}


void ImageView::setGalleryRelayoutSuppressed(bool on)
{
    if (on) {
        m_galleryRelayoutSuppress.push(true);
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
    } else if (m_galleryRelayoutSuppress.active()) {
        m_galleryRelayoutSuppress.push(false);
    }
}

void ImageView::reloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        if (!hasClassicPath()) {
            return;
        }
        const QString path = classicPath();
        // Drop retained path tiles so Reload cannot paint pre-reload grid cells.
        purgeTilePathRam(path);
        // Force a fresh decode of the focused session image only.
        scheduleImageLoad(path, LoadReplace);
        flashHud(tr("Reload"), QFileInfo(path).fileName());
        return;
    }

    // Gallery / Workspace: re-decode every on-canvas item in place.
    QSet<QString> purgedPaths;
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        gallerySoftResetPath(path);
        if (!purgedPaths.contains(path)) {
            purgeTilePathRam(path);
            purgedPaths.insert(path);
        } else {
            item->dropTileLodSession();
        }
        takePendingWorkspacePath(path);
        item->clearDecodedPixels();
        PendingSessionBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        m_bindBook.append(b);
        if (isGalleryMode()) {
            scheduleGalleryDecode(path);
        } else {
            scheduleImageLoad(path, LoadAdd);
        }
    }
    if (isGalleryMode() && relayoutGallery) {
        applyLayout(GalleryPackReason::Reload);
    }
    flashHud(tr("Reload"),
             isGalleryMode() ? tr("Gallery") : tr("Workspace"));
    emit statusChanged();
}

void ImageView::hardReloadFromDisk(bool relayoutGallery)
{
    // Build the path set: Image = focused path; multi-mode = selection, else all.
    QList<ImageItem *> targets;
    if (isImageMode()) {
        if (!hasClassicPath()) {
            return;
        }
        for (ImageItem *item : m_items) {
            if (item && item->path() == classicPath()) {
                targets.append(item);
                break;
            }
        }
        if (targets.isEmpty()) {
            // No item yet — still purge Store + process caches, then LoadReplace.
            const QString path = classicPath();
            ImageCache::remove(path);
            purgeTilePathRam(path);
            for (int edge : ThumtooCache::kLadderEdges) {
                ThumtooCache::forgetPixelsSettled(path, edge);
            }
            flashHud(tr("Hard reload"), QFileInfo(path).fileName());
            ThumtooCache::purgePathDurable(path, [this, path](qint64 /*tiles*/) {
                ThumtooCache::scheduleProbe(path);
                scheduleImageLoad(path, LoadReplace);
                emit statusChanged();
            });
            return;
        }
    } else {
        targets = transformTargets();
        if (targets.isEmpty()) {
            targets = m_items;
        }
    }
    if (targets.isEmpty()) {
        return;
    }

    QSet<QString> pathSet;
    struct ReloadBind {
        QString path;
        SessionImageId id = kInvalidSessionImageId;
        int index = -1;
    };
    QList<ReloadBind> binds;
    int itemCount = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        ++itemCount;
        gallerySoftResetPath(path);
        takePendingWorkspacePath(path);
        item->clearDecodedPixels();
        if (!pathSet.contains(path)) {
            ImageCache::remove(path);
            purgeTilePathRam(path);
            for (int edge : ThumtooCache::kLadderEdges) {
                ThumtooCache::forgetPixelsSettled(path, edge);
            }
            pathSet.insert(path);
        } else {
            item->dropTileLodSession();
        }
        ReloadBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        binds.append(b);
    }
    if (pathSet.isEmpty()) {
        return;
    }

    // Prefer QStringList over QSet::constBegin() — avoids GCC -Wnull-dereference
    // on QHash span access when inlining QSet iterators.
    const QStringList paths = pathSet.values();
    const QString detail = (paths.size() == 1)
        ? QFileInfo(paths.constFirst()).fileName()
        : tr("%1 paths · %2 items").arg(paths.size()).arg(itemCount);
    flashHud(tr("Hard reload"), detail);

    // Purge durable Store tiles off the GUI, then re-decode only after forget
    // so PreferCache / tile LOD cannot re-hit the old pyramid.
    auto remaining = std::make_shared<int>(paths.size());
    auto tileTotal = std::make_shared<qint64>(0);
    const bool doRelayout = relayoutGallery;
    const bool imageMode = isImageMode();
    const bool galleryMode = isGalleryMode();

    auto finish = [this, binds, doRelayout, imageMode, galleryMode, tileTotal]() {
        QSet<QString> probed;
        for (const ReloadBind &b : binds) {
            PendingSessionBind pending;
            pending.path = b.path;
            pending.id = b.id;
            pending.index = b.index;
            m_bindBook.append(pending);
            // Size memo was cleared with the Store purge — force a fresh probe
            // so layout / intrinsic size do not keep a pre-purge value.
            if (!probed.contains(b.path)) {
                ThumtooCache::scheduleProbe(b.path);
                probed.insert(b.path);
            }
            if (imageMode) {
                scheduleImageLoad(b.path, LoadReplace);
            } else if (galleryMode) {
                scheduleGalleryDecode(b.path);
            } else {
                scheduleImageLoad(b.path, LoadAdd);
            }
        }
        if (galleryMode && doRelayout) {
            applyLayout(GalleryPackReason::Reload);
        }
        if (*tileTotal > 0) {
            flashHud(tr("Hard reload"),
                     tr("%1 Store tiles removed").arg(*tileTotal));
        }
        emit statusChanged();
    };

    for (const QString &path : paths) {
        ThumtooCache::purgePathDurable(path, [remaining, tileTotal, finish](qint64 tiles) {
            *tileTotal += tiles;
            if (--(*remaining) == 0) {
                finish();
            }
        });
    }
}

void ImageView::applyLayout(GalleryPackReason reason)
{
    m_gallery.applyLayout(reason);
}


bool ImageView::layoutWorkspaceItems(const GalleryLayout::Params &userParams,
                                     const QList<ImageItem *> &itemsIn)
{
    return m_workspace.layoutItems(userParams, itemsIn);
}



void ImageView::updateGallerySoftProgressHud()
{
    m_gallery.updateSoftProgressHud();
}


void ImageView::gallerySoftWatchdogTick()
{
    m_gallery.softWatchdogTick();
}

