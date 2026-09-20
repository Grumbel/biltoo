// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "tile_load_coordinator.h"
#include "gallerylayout.h"
#include "toolpolicy.h"
#include "gallerysoftsm.h"
#include "viewtransform.h"
#include "displayedgepolicy.h"
#include "slideshowatlaspolicy.h"
#include "slideshowmotiongeometry.h"
#include "slideshowclocks.h"
#include "zoomblurhelpers.h"
#include "slideshowphasepolicy.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include "archivepath.h"
#include "biltoo_logging.h"
#include "contentxform.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "thumtoocache.h"
#include "tilelod/tile_lod_controller.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRubberBand>
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QtMath>

#include <cmath>
#include <cstdio>
#include <cstdlib>

void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    setCursor(ToolPolicy::cursorFor(m_tool));
    // Workspace Select: rubber-band multi-select on empty drag (same as Gallery).
    // Pan / Zoom keep NoDrag (view gestures are handled in mouse handlers).
    if (isWorkspaceMode()) {
        setDragMode(ToolPolicy::workspaceRubberBand(m_tool)
                        ? QGraphicsView::RubberBandDrag
                        : QGraphicsView::NoDrag);
    }
    emit toolChanged(m_tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (!m_sessionNav.setImageModeNav(on)) {
        return;
    }
    if (!on && m_hoverEdge != EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    if (!m_sessionNav.setGalleryReturnAvailable(on)) {
        return;
    }
    if (!on && m_hoverEdge == EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

QColor ImageView::slideshowPadColor() const
{
    if (m_slideshow.settings().isSolidLetterbox()
        && m_slideshow.settings().padColorRef().isValid()) {
        return m_slideshow.settings().padColorRef();
    }
    if (m_canvasBg.primaryColor().isValid()) {
        return m_canvasBg.primaryColor();
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return m_slideshow.settings().padColorRef().isValid() ? m_slideshow.settings().padColorRef() : QColor(42, 42, 42);
}



void ImageView::setContentEditMarksVisible(bool on)
{
    ImageItem::setContentEditMarksVisible(on);
    if (m_scene) {
        for (ImageItem *it : m_items) {
            if (it) {
                it->update();
            }
        }
        m_scene->update();
    }
    if (viewport()) {
        viewport()->update();
    }
}


QSize ImageView::logicalSizeForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = m_sizeBook.known(path);
    if (!known.isEmpty()) {
        return known;
    }
    const QSize cached = ThumtooCache::cachedSize(path);
    if (isPositiveSize(cached)) {
        return cached;
    }
    return {};
}


QSize ImageView::ensureLogicalSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !m_sizeBook.isProvisional(path)) {
        return known;
    }
    // imageSizeForPath may schedule a probe and/or install thumtoo cache.
    return imageSizeForPath(path);
}

QString ImageView::currentPath() const
{
    if (ImageItem *item = targetItem()) {
        return item->path();
    }
    if (ImageItem *item = primaryItem()) {
        return item->path();
    }
    return {};
}

