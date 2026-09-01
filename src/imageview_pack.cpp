// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QTimer>

void ImageView::updateGalleryDecodeWindow()
{
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }

    const bool virtualize = m_items.size() >= kGalleryVirtualThreshold
                            || m_pathOrder.size() >= kGalleryVirtualThreshold;

    // Visible-first priority, then the rest of the session. Never unload decoded
    // tiles — clearing pixels while a job was in flight left permanent placeholders
    // and size/scale mismatches broke pack geometry.
    QStringList visible;
    QStringList rest;
    if (virtualize) {
        const QRect viewRect = viewport()->rect().adjusted(
            -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
            kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
        const QRectF sceneVisible = mapToScene(viewRect).boundingRect();
        for (ImageItem *item : m_items) {
            if (!item || item->hasDecodedPixels()
                || m_galleryDecodeFailed.contains(item->path())) {
                continue;
            }
            const QRectF tile = item->contentSceneRect();
            if (tile.isNull() || tile.intersects(sceneVisible)) {
                visible.append(item->path());
            } else {
                rest.append(item->path());
            }
        }
    } else {
        for (ImageItem *item : m_items) {
            if (!item || item->hasDecodedPixels()
                || m_galleryDecodeFailed.contains(item->path())) {
                continue;
            }
            visible.append(item->path());
        }
    }

    for (const QString &path : visible) {
        scheduleGalleryDecode(path);
    }
    for (const QString &path : rest) {
        scheduleGalleryDecode(path);
    }
    emit statusChanged();
}

void ImageView::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts belong only to Gallery; FreeForm only to Workspace.
    if (mode == LayoutMode::FreeForm) {
        if (!isWorkspaceMode()) {
            return;
        }
        if (m_layoutMode != LayoutMode::FreeForm) {
            // Should not happen in Workspace (always FreeForm).
        }
        m_layoutMode = LayoutMode::FreeForm;
        for (ImageItem *item : m_items) {
            applyItemModeFlags(item);
        }
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-64, -64, 64, 64));
        }
        m_fitMode = false;
        emit statusChanged();
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!isGalleryMode()) {
        enterGallery(mode);
        return;
    }

    if (m_layoutMode == LayoutMode::FreeForm && mode != LayoutMode::FreeForm) {
        snapshotFreeFormStates();
    }

    m_layoutMode = mode;
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
    }
    applyLayout(GalleryPackReason::EnterGallery);
}

void ImageView::setGridColumns(int columns)
{
    const int clamped = qMax(0, columns); // 0 = automatic
    if (clamped == m_gridColumns) {
        return;
    }
    m_gridColumns = clamped;
    if (isGalleryMode()
        && (m_layoutMode == LayoutMode::Grid || m_layoutMode == LayoutMode::GridCrop)) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryColumns(int columns)
{
    const int clamped = qBound(1, columns, 32);
    if (clamped == m_masonryColumns) {
        return;
    }
    m_masonryColumns = clamped;
    if (m_layoutMode == LayoutMode::Masonry && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryRows(int rows)
{
    const int clamped = qBound(1, rows, 32);
    if (clamped == m_masonryRows) {
        return;
    }
    m_masonryRows = clamped;
    if (m_layoutMode == LayoutMode::MasonryRows && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setGalleryRelayoutSuppressed(bool on)
{
    if (on) {
        ++m_galleryRelayoutSuppressCount;
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
    } else if (m_galleryRelayoutSuppressCount > 0) {
        --m_galleryRelayoutSuppressCount;
    }
}

void ImageView::reloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        if (!hasClassicPath()) {
            return;
        }
        // Force a fresh decode of the focused session image only.
        scheduleImageLoad(classicPath(), LoadReplace);
        flashHud(tr("Reload"), QFileInfo(classicPath()).fileName());
        return;
    }

    // Gallery / Workspace: re-decode every on-canvas item in place.
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        m_galleryDecodeFailed.remove(path);
        m_galleryDecodeScheduled.remove(path);
        m_pendingWorkspacePaths.remove(path);
        item->clearDecodedPixels();
        PendingSessionBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        m_pendingSessionBinds.append(b);
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

void ImageView::applyLayout(GalleryPackReason reason)
{
    Q_UNUSED(reason);
    if (m_applyingLayout) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    if (!isGalleryMode() || m_items.isEmpty() || m_layoutMode == LayoutMode::FreeForm) {
        return;
    }

    if (!m_pathOrder.isEmpty()) {
        reorderItemsByPaths(m_pathOrder);
    }

    // Gallery overview is axis-aligned. Strip any leftover Workspace placement
    // tilt/flips before packing (content 90°/flip remain in baked pixels).
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        item->setItemRotation(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
    }

    m_applyingLayout = true;

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    if (!m_gallery.pendingRestore()) {
        centerOn(0, 0);
    }

    const qreal margin = 16.0;
    const qreal gap = 12.0;
    const qreal availW = qMax(32.0, static_cast<qreal>(viewport()->width()) - 2.0 * margin);
    const qreal availH = qMax(32.0, static_cast<qreal>(viewport()->height()) - 2.0 * margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_masonryColumns;
    params.gridColumns = m_gridColumns;
    params.masonryRows = m_masonryRows;
    switch (m_layoutMode) {
    case LayoutMode::SideBySide:
        params.mode = GalleryLayout::Mode::SideBySide;
        break;
    case LayoutMode::Vertical:
        params.mode = GalleryLayout::Mode::Vertical;
        break;
    case LayoutMode::Grid:
        params.mode = GalleryLayout::Mode::Grid;
        break;
    case LayoutMode::GridCrop:
        params.mode = GalleryLayout::Mode::GridCrop;
        break;
    case LayoutMode::Masonry:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    case LayoutMode::MasonryRows:
        params.mode = GalleryLayout::Mode::MasonryRows;
        break;
    default:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    }

    GalleryLayout::pack(m_items, params, [this](ImageItem *item) {
        m_itemStates.insert(item->path(), captureState(item));
    });

    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    // Never call setHorizontalScrollBarPolicy here — it resizes the viewport and
    // re-enters via resizeEvent (stack overflow). Policy is owned by MainWindow.
    if (m_scene->sceneRect() != bounds) {
        m_scene->setSceneRect(bounds);
    }
    m_fitMode = true;
    // Keep the guard until after statusChanged so slots cannot re-enter layout.
    emit statusChanged();
    m_applyingLayout = false;
    // Re-apply scroll after centerOn(0,0) above when returning from Image.
    applyPendingGalleryRestore();
    updateGalleryDecodeWindow();
}
