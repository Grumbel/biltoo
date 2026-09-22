// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
#include "imageview.h"
#include "cropgeometry.h"
#include "tilelod/tile_lod_controller.hpp"
#include "tilelod/tile_lod_registry.hpp"
#include "displayquality.h"
#include "imagecache.h"
#include <QFileInfo>

#include "coloradjust.h"
#include "placementlinear.h"
#include "contentxform.h"

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <cmath>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

bool ImageItem::s_contentEditMarksVisible = true;

void ImageItem::setContentEditMarksVisible(bool on)
{
    if (s_contentEditMarksVisible == on) {
        return;
    }
    s_contentEditMarksVisible = on;
}

bool ImageItem::contentEditMarksVisible()
{
    return s_contentEditMarksVisible;
}

ImageItem::~ImageItem()
{
    // Invalidate pending QTimer::singleShot from tickTileLod (queued on the
    // scene/app, not tied to this QGraphicsItem lifetime). Pipeline still owns
    // the bag unique_ptr until releaseTileBag / releaseAllTileBags.
    if (m_tileLodAttached) {
        if (m_tileLodAttached->alive) {
            *m_tileLodAttached->alive = false;
        }
        m_tileLodAttached->repaintQueued = false;
        m_tileLodAttached = nullptr;
    }
}


void ImageItem::setPath(const QString &path)
{
    if (m_path == path) {
        return;
    }
    m_path = path;
    // Kill pending tickTileLod singleShot so it cannot update() after this
    // item now represents a different file (stale path identity). Only when a
    // pipeline bag is already attached (no ensure via ImageView).
    if (m_tileLodAttached) {
        tilelod::ItemBag &bag = *m_tileLodAttached;
        if (bag.alive) {
            *bag.alive = false;
        }
        bag.alive = std::make_shared<bool>(true);
        // Destroy session only — SharedPathTiles stay in TileLodRegistry (1212).
        bag.controller.reset();
        bag.lastUpdateGen = 0;
        // Do not clear bag.suppressed: crop-draft owns it across path binds.
        bag.repaintQueued = false;
        bag.lastDpc = -1.0;
        bag.lastVisSource = QRectF();
        clearTileGradedCache();
    }
    // Prefer this path under global LRU when the user navigates back soon.
    tilelod::TileLodRegistry::instance().touch(m_path);
}


ImageItem::ImageItem(const QString &path, const QImage &image, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent)
    , m_path(path)
    , m_source(image)
    // Sample pixels are not logical size. Caller must setIntrinsicSize
    // (layoutSizeForPath / probe) before fit/pack.
    , m_intrinsicSize(1, 1)
{
    setTransformationMode(Qt::SmoothTransformation);
    // Classic viewer by default: not selectable/movable until workspace mode
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setOffset(-0.5, -0.5);
    if (!m_source.isNull()) {
        updateDisplayedPixmap();
    }
    applyLocalTransform();
}

ImageItem::ImageItem(const QString &path, const QSize &intrinsicSize, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent)
    , m_path(path)
    , m_intrinsicSize(intrinsicSize.isValid() && intrinsicSize.width() > 0
                           && intrinsicSize.height() > 0
                       ? intrinsicSize
                       : QSize(1, 1))
{
    setTransformationMode(Qt::SmoothTransformation);
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setOffset(-m_intrinsicSize.width() / 2.0, -m_intrinsicSize.height() / 2.0);
    applyLocalTransform();
}

QSize ImageItem::imageSize() const
{
    // Logical size only. Never return sample pixel dimensions (soft or
    // full-source ladder) — those are sampling, not identity.
    if (m_intrinsicSize.isValid() && m_intrinsicSize.width() > 0
        && m_intrinsicSize.height() > 0) {
        return m_intrinsicSize;
    }
    return QSize(1, 1);
}

void ImageItem::setIntrinsicSize(const QSize &size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return;
    }
    // Explicit logical-size write (probe, layout, crop). Always honored —
    // samples must not block crop shrink or probe upgrades. Soft never calls this.
    if (m_intrinsicSize == size) {
        return;
    }
    prepareGeometryChange();
    m_intrinsicSize = size;
    setOffset(-m_intrinsicSize.width() / 2.0, -m_intrinsicSize.height() / 2.0);
    applyLocalTransform();
    update();
}

void ImageItem::setSourceImage(const QImage &image)
{
    // Legacy path may still carry item-level flip/grade; ready path is preferred
    // for LoadReplace installs (appearance baked in the sample).
    prepareGeometryChange();
    m_source = image;
    m_preview = QImage();
    m_previewPixels = false;
    if (!m_source.isNull()) {
        setOffset(-m_intrinsicSize.width() / 2.0, -m_intrinsicSize.height() / 2.0);
        updateDisplayedPixmap();
    } else {
        setPixmap(QPixmap());
        const QSize s = imageSize();
        setOffset(-s.width() / 2.0, -s.height() / 2.0);
    }
    applyLocalTransform();
    update();
}

void ImageItem::setSourceImageReady(const QImage &image)
{
    // Display-ready sample: assign pixels + repaint only.
    // Do NOT prepareGeometryChange / applyLocalTransform — intrinsic size is
    // independent of sample resolution; those calls re-enter the scene and
    // were the remaining ←/→ GUI cost after decode moved off-thread.
    QImage src = image;
    ImageCache::stampDebugOverlayIfEnabled(&src, QFileInfo(path()).fileName());
    m_source = src;
    m_preview = QImage();
    m_previewPixels = false;
    m_hFlip = false;
    m_vFlip = false;
    // Gallery: bake pixmap + ItemCoordinateCache (scroll-stable under GL).
    // Image/Workspace: keep NoCache; paint reads m_source (tiles / soft underlay).
    if (!m_interactive) {
        syncGalleryScrollCache();
    } else {
        setCacheMode(QGraphicsItem::NoCache);
        setPixmap(QPixmap()); // paint path uses m_source via drawImage
    }
    update();
}

int ImageItem::displayPixelLongEdge() const
{
    const QImage &img = displayImage();
    return img.isNull() ? 0 : ContentXform::longEdge(img.size());
}

bool ImageItem::shouldUpgradeDisplayTo(int incomingLongEdge) const
{
    // Edge-only (no content crop). Install/accept policy is DisplaySurface::decide.
    return DisplayQuality::isStrictUpgrade(displayPixelLongEdge(), incomingLongEdge);
}

void ImageItem::setPreviewImage(const QImage &preview)
{
    if (preview.isNull()) {
        return;
    }
    // Full decode already present — ignore late previews.
    if (!m_source.isNull() && !m_previewPixels) {
        return;
    }
    // Soft stand-in: assign + repaint only (no prepareGeometryChange).
    QImage prev = preview;
    ImageCache::stampDebugOverlayIfEnabled(&prev, QFileInfo(path()).fileName());
    m_preview = prev;
    m_previewPixels = true;
    m_source = QImage();
    // Gallery soft used to force NoCache so DeviceCoordinateCache did not freeze
    // the empty placeholder. ItemCoordinateCache + baked pixmap survives scroll
    // (DeviceCoordinate is invalidated on every view pan under OpenGL).
    if (!m_interactive) {
        syncGalleryScrollCache();
    } else {
        setPixmap(QPixmap());
        setCacheMode(QGraphicsItem::NoCache);
    }
    update();
}

void ImageItem::clearDecodedPixels()
{
    if (m_source.isNull() && m_preview.isNull()) {
        return;
    }
    // Keep intrinsic — samples must not redefine geometry on clear.
    // No prepareGeometryChange — offset/intrinsic unchanged.
    m_source = QImage();
    m_preview = QImage();
    m_previewPixels = false;
    setPixmap(QPixmap());
    if (!m_interactive) {
        setCacheMode(QGraphicsItem::NoCache);
    }
    // Keep applied ContentXform across the pixel gap: it is the session content
    // fingerprint (tileContentXform / chrome), not a claim that pixels are
    // currently present. attachDisplaySample / syncLive reassert or replace it.
    // Identity path-change uses clearLiveContentMeta to drop applied.
    update();
}

ItemComponents::Placement ImageItem::placement() const
{
    ItemComponents::Placement pl;
    pl.pos = pos();
    pl.scale = m_scaleX;
    pl.scaleY = m_scaleY;
    pl.shear = m_shear;
    pl.rotation = m_rotation;
    pl.opacity = m_opacity;
    pl.z = m_stackZ;
    pl.hFlip = m_hFlip;
    pl.vFlip = m_vFlip;
    return pl;
}

void ImageItem::applyPlacement(const ItemComponents::Placement &pl)
{
    setPos(pl.pos);
    setItemScale(pl.scale, pl.scaleY > 0.0 ? pl.scaleY : pl.scale);
    setItemShear(pl.shear);
    setItemRotation(pl.rotation);
    setItemOpacity(pl.opacity);
    setStackZ(pl.z);
    setItemHFlip(pl.hFlip);
    setItemVFlip(pl.vFlip);
}

void ImageItem::setItemScale(qreal scale)
{
    setItemScale(scale, scale);
}

void ImageItem::setItemScale(qreal scaleX, qreal scaleY)
{
    m_scaleX = scaleX;
    m_scaleY = scaleY;
    PlacementLinear::clampScaleXY(&m_scaleX, &m_scaleY);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemShear(qreal shear)
{
    // Keep parallelograms editable; extreme k collapses chrome.
    m_shear = PlacementLinear::clampShear(shear);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemRotation(qreal degrees)
{
    // Placement only — never content. Content orient is absolute materialize
    // from host-raw via ImageView::bakeItemRotate90 (ItemWorld contentBake).
    m_rotation = PlacementLinear::normalizeDegrees(degrees);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemOpacity(qreal opacity)
{
    m_opacity = PlacementLinear::clampOpacity(opacity);
    // Keep QGraphicsItem opacity at 1 so handles/chrome stay solid; the
    // pixmap is drawn with m_opacity in paint().
    setOpacity(1.0);
    update();
}

void ImageItem::setStackZ(qreal z)
{
    m_stackZ = z;
    refreshStackingOrder();
}

QRectF ImageItem::contentSceneRect() const
{
    return mapToScene(contentRect()).boundingRect();
}

QPolygonF ImageItem::contentScenePolygon() const
{
    return mapToScene(contentRect());
}

void ImageItem::refreshStackingOrder()
{
    // Stacking order is only m_stackZ (Raise/Lower). Selecting an item must not
    // temporarily bring the whole pixmap above others — only the chrome is drawn
    // on that item; covering images keep their true stack position.
    setZValue(m_stackZ);
}

void ImageItem::setItemHFlip(bool on)
{
    if (m_hFlip == on) {
        return;
    }
    m_hFlip = on;
    updateDisplayedPixmap();
    prepareGeometryChange();
    update();
}

void ImageItem::setItemVFlip(bool on)
{
    if (m_vFlip == on) {
        return;
    }
    m_vFlip = on;
    updateDisplayedPixmap();
    prepareGeometryChange();
    update();
}

void ImageItem::setInteractive(bool on)
{
    m_interactive = on;
    if (on) {
        setGalleryCellSize({});
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges
                 | ItemIsFocusable);
    } else {
        setSelected(false);
        setFlags(ItemSendsGeometryChanges);
    }
}

void ImageItem::setGallerySelectable(bool on)
{
    // Selectable for classic multi-select; open on double-click; no transform chrome.
    m_interactive = false;
    m_scaleHandlesEnabled = false;
    m_hoverHandle = Handle::None;
    // Crop is owned by the layout; clear when leaving gallery selectable.
    if (!on) {
        m_galleryCellSize = QSizeF();
    }
    if (on) {
        setAcceptHoverEvents(true);
        // Gallery: smooth scale so soft thumbs look less blocky when the view
        // zoom is not 1:1. ItemCoordinateCache + pixmap bake survives scroll
        // under QOpenGLWidget (DeviceCoordinateCache is invalidated on pan).
        setTransformationMode(Qt::SmoothTransformation);
        syncGalleryScrollCache();
        setFlags(ItemIsSelectable | ItemSendsGeometryChanges | ItemIsFocusable);
    } else {
        setSelected(false);
        setCacheMode(QGraphicsItem::NoCache);
        setTransformationMode(Qt::SmoothTransformation);
        setFlags(ItemSendsGeometryChanges);
    }
    prepareGeometryChange();
    update();
}

void ImageItem::syncGalleryScrollCache()
{
    if (m_interactive) {
        return;
    }
    // Tile grid cells change every completion / pan — do not freeze mid-stream.
    if (tileLodWanted()) {
        setCacheMode(QGraphicsItem::NoCache);
        // Soft underlay must upgrade LQIP → soft while tiles stream in.
        // Only filling when pixmap is null left LQIP stuck under the tile grid.
        const QImage &img = displayImage();
        if (!img.isNull()) {
            const int imgEdge = ContentXform::longEdge(img.size());
            const int pixEdge = pixmap().isNull()
                ? 0
                : ContentXform::longEdge(pixmap().size());
            if (imgEdge > pixEdge) {
                setPixmap(QPixmap::fromImage(img));
            }
        }
        return;
    }
    const QImage &img = displayImage();
    if (img.isNull()) {
        setPixmap(QPixmap());
        setCacheMode(QGraphicsItem::NoCache);
        return;
    }
    // Bake once; paint draws this pixmap. ItemCoordinateCache is in item
    // space so view scrollbar pan does not rebuild it. Toggle mode when
    // already cached so soft 128→256→512 upgrades are not frozen.
    setPixmap(QPixmap::fromImage(img));
    if (cacheMode() == QGraphicsItem::ItemCoordinateCache) {
        setCacheMode(QGraphicsItem::NoCache);
    }
    setCacheMode(QGraphicsItem::ItemCoordinateCache);
}

void ImageItem::invalidateDeviceCache()
{
    // ItemCoordinateCache / DeviceCoordinateCache freeze paint() output.
    // Toggle mode so the next paint is not a stale raster (content change).
    // Gallery selection frame is painted by ImageView overlay — selection
    // alone does not need this; content/pixel installs still do.
    const CacheMode mode = cacheMode();
    if (mode != QGraphicsItem::NoCache) {
        setCacheMode(QGraphicsItem::NoCache);
        setCacheMode(mode);
    }
    update();
}

void ImageItem::setGalleryCellSize(const QSizeF &sceneSize)
{
    if (m_galleryCellSize == sceneSize) {
        return;
    }
    prepareGeometryChange();
    m_galleryCellSize = sceneSize;
    update();
}

QRectF ImageItem::galleryClipLocal() const
{
    if (m_galleryCellSize.isEmpty() || m_galleryCellSize.width() <= 0
        || m_galleryCellSize.height() <= 0) {
        return {};
    }
    const qreal s = PlacementLinear::maxAxisScale(m_scaleX, m_scaleY);
    const qreal lw = m_galleryCellSize.width() / s;
    const qreal lh = m_galleryCellSize.height() / s;
    const QRectF br = contentRect();
    return QRectF(br.center().x() - lw / 2.0,
                  br.center().y() - lh / 2.0,
                  lw, lh);
}

void ImageItem::setScaleHandlesEnabled(bool on)
{
    if (m_scaleHandlesEnabled == on) {
        return;
    }
    m_scaleHandlesEnabled = on;
    prepareGeometryChange();
    update();
}

void ImageItem::applyLocalTransform()
{
    // Linear pose: R(θ)·H(k)·S(sx,sy). Flips are baked into the pixmap so
    // handles stay on the geometric top/left/right of the item frame.
    setTransform(PlacementLinear::make(m_scaleX, m_scaleY, m_shear, m_rotation));
}

void ImageItem::setColorAdjustments(const ColorAdjustments &adj)
{
    if (m_colorAdjust.brightness == adj.brightness
        && m_colorAdjust.contrast == adj.contrast
        && m_colorAdjust.saturation == adj.saturation
        && m_colorAdjust.hue == adj.hue
        && qFuzzyCompare(m_colorAdjust.gamma, adj.gamma)
        && m_colorAdjust.invert == adj.invert) {
        return;
    }
    m_colorAdjust = adj;
    clearTileGradedCache();
    // Live grade only when the sample is still host-raw (no applied ContentXform
    // bake). Once materializeDisplay has graded pixels into m_source, re-running
    // updateDisplayedPixmap would double-apply (Gallery used to hit this via
    // setSourceImage in attachDisplaySample).
    if (!m_source.isNull() && !m_previewPixels && !m_hasAppliedContentXform) {
        updateDisplayedPixmap();
    }
    update();
}

void ImageItem::setColorAdjustmentsRecord(const ColorAdjustments &adj)
{
    m_colorAdjust = adj;
    clearTileGradedCache();
}

void ImageItem::updateDisplayedPixmap()
{
    if (m_source.isNull()) {
        // Gallery soft may only have m_preview; keep pixmap in sync for scroll cache.
        if (!m_interactive && !m_preview.isNull()) {
            setPixmap(QPixmap::fromImage(m_preview));
            return;
        }
        setPixmap(QPixmap());
        return;
    }
    QImage img = m_source;
    if (m_hFlip || m_vFlip) {
        Qt::Orientations axes;
        if (m_hFlip) {
            axes |= Qt::Horizontal;
        }
        if (m_vFlip) {
            axes |= Qt::Vertical;
        }
        img = img.flipped(axes);
    }
    if (!m_colorAdjust.isIdentity()) {
        img = applyColorAdjustments(img, m_colorAdjust);
    }
    setPixmap(QPixmap::fromImage(img));
}

QPoint ImageItem::pixelAtScenePos(const QPointF &scenePos) const
{
    const QPointF local = mapFromScene(scenePos) - offset();
    int x = static_cast<int>(local.x());
    int y = static_cast<int>(local.y());
    if (x < 0 || y < 0 || x >= m_source.width() || y >= m_source.height()) {
        return QPoint(-1, -1);
    }
    // Display is mirrored; map back to source pixel coordinates
    if (m_hFlip) {
        x = m_source.width() - 1 - x;
    }
    if (m_vFlip) {
        y = m_source.height() - 1 - y;
    }
    return QPoint(x, y);
}

QColor ImageItem::colorAtPixel(const QPoint &pixel) const
{
    if (pixel.x() < 0 || pixel.y() < 0
        || pixel.x() >= m_source.width() || pixel.y() >= m_source.height()) {
        return QColor();
    }
    return m_source.pixelColor(pixel);
}

QRectF ImageItem::contentRect() const
{
    // Logical size owns geometry. Display pixmap may be soft/ladder and must
    // not define the item box — paint samples into this rect.
    const QSize s = imageSize();
    return QRectF(offset(), QSizeF(s));
}

QRectF ImageItem::displayContentRect() const
{
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            return clip;
        }
    }
    // Soft and full share the same logical contentRect (SIZE.md). Do not
    // letterbox soft inside the layout box — that desynced scroll/hit geometry.
    return contentRect();
}


