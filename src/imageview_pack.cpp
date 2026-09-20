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
        m_workspace.applyFreeFormLayout();
        return;
    }
    m_gallery.setLayoutMode(mode);
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
        m_image.reloadFromDisk();
        return;
    }
    if (isGalleryMode()) {
        m_gallery.reloadFromDisk(relayoutGallery);
        return;
    }
    m_workspace.reloadFromDisk();
}


void ImageView::hardReloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        m_image.hardReloadFromDisk();
        return;
    }
    if (isGalleryMode()) {
        m_gallery.hardReloadFromDisk(relayoutGallery);
        return;
    }
    m_workspace.hardReloadFromDisk();
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

