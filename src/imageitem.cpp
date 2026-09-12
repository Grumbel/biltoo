// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
#include "coloradjust.h"
#include "placementlinear.h"

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

ImageItem::ImageItem(const QString &path, const QImage &image, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent)
    , m_path(path)
    , m_source(image)
    , m_intrinsicSize(image.size())
{
    setTransformationMode(Qt::SmoothTransformation);
    // Classic viewer by default: not selectable/movable until workspace mode
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    if (!m_source.isNull()) {
        setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
        updateDisplayedPixmap();
    } else {
        m_intrinsicSize = QSize(1, 1);
        setOffset(-0.5, -0.5);
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
    // Prefer known native/layout size over decoded ladder/preview pixels so HUD
    // and packs report the full page size, not a 512–2048 ladder step.
    const bool intrinsicKnown =
        m_intrinsicSize.isValid() && m_intrinsicSize.width() > 1
        && m_intrinsicSize.height() > 1 && m_intrinsicSize != QSize(1000, 1000)
        && m_intrinsicSize != QSize(1024, 1024);
    if (intrinsicKnown) {
        return m_intrinsicSize;
    }
    if (!m_source.isNull() && !m_previewPixels) {
        return m_source.size();
    }
    return m_intrinsicSize.isValid() ? m_intrinsicSize : QSize(1, 1);
}

void ImageItem::setIntrinsicSize(const QSize &size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return;
    }
    // Allow growth when a size probe reports true native dimensions larger than
    // a ladder decode already installed as source (HUD / layout geometry).
    if (!m_source.isNull() && !m_previewPixels) {
        const bool larger =
            qint64(size.width()) * size.height()
            > qint64(m_intrinsicSize.width()) * m_intrinsicSize.height();
        if (!larger && m_intrinsicSize.isValid()) {
            return;
        }
    }
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
    prepareGeometryChange();
    m_source = image;
    m_preview = QImage();
    m_previewPixels = false;
    if (!m_source.isNull()) {
        const QSize src = m_source.size();
        const bool intrinsicKnown =
            m_intrinsicSize.isValid() && m_intrinsicSize.width() > 1
            && m_intrinsicSize.height() > 1
            && m_intrinsicSize != QSize(1000, 1000)
            && m_intrinsicSize != QSize(1024, 1024);
        // Full / post-crop pixels always define layout geometry. Grow when a
        // larger decode arrives; also shrink when session crop replaces a larger
        // intrinsic (never leave pre-crop native size with cropped pixels — that
        // skews offset and text-region mapping). Soft ladder uses setPreviewImage
        // and does not touch intrinsic.
        if (!intrinsicKnown
            || (qint64(src.width()) * src.height()
                != qint64(m_intrinsicSize.width()) * m_intrinsicSize.height())
            || src != m_intrinsicSize) {
            m_intrinsicSize = src;
        }
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

int ImageItem::displayPixelLongEdge() const
{
    if (!m_source.isNull() && !m_previewPixels) {
        return qMax(m_source.width(), m_source.height());
    }
    if (!m_preview.isNull()) {
        return qMax(m_preview.width(), m_preview.height());
    }
    return 0;
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
    prepareGeometryChange();
    m_preview = preview;
    m_previewPixels = true;
    m_source = QImage();
    setPixmap(QPixmap());
    // Soft tiles: NoCache. Toggle cache mode so any prior DeviceCoordinate
    // snapshot is discarded (otherwise OpenGL can keep showing the old soft).
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    setCacheMode(QGraphicsItem::NoCache);
    // Intrinsic size is layout geometry (probe / full native size). Never adopt
    // soft-preview pixel dimensions — that shrinks Gallery cells to 512 and
    // makes zoom/pack jump when a full decode later restores native size.
    // Only seed the neutral 1000² / 1024² stand-in when size is still unknown.
    const bool neutral =
        !m_intrinsicSize.isValid()
        || m_intrinsicSize.width() <= 1 || m_intrinsicSize.height() <= 1
        || m_intrinsicSize == QSize(1000, 1000)
        || m_intrinsicSize == QSize(1024, 1024);
    // Prefer keeping neutral stand-in until size probe / full decode; do not
    // promote soft thumbs into layout geometry.
    Q_UNUSED(neutral);
    Q_UNUSED(preview);
    const QSize s = imageSize();
    setOffset(-s.width() / 2.0, -s.height() / 2.0);
    applyLocalTransform();
    update();
    // Ensure the view actually schedules a paint for this item's scene rect
    // (selection used to be the only path that forced a visible upgrade).
    if (QGraphicsScene *sc = scene()) {
        sc->invalidate(mapToScene(boundingRect()).boundingRect(),
                       QGraphicsScene::AllLayers);
    }
}

void ImageItem::clearDecodedPixels()
{
    if (m_source.isNull() && m_preview.isNull()) {
        return;
    }
    prepareGeometryChange();
    if (!m_intrinsicSize.isValid() || m_intrinsicSize.isEmpty()) {
        if (!m_source.isNull()) {
            m_intrinsicSize = m_source.size();
        }
    }
    m_source = QImage();
    m_preview = QImage();
    m_previewPixels = false;
    setPixmap(QPixmap());
    const QSize s = imageSize();
    setOffset(-s.width() / 2.0, -s.height() / 2.0);
    update();
}

qreal ImageItem::itemScale() const
{
    // Geometric mean keeps a single % meaningful when axes differ slightly.
    return qSqrt(qMax(0.01, m_scaleX) * qMax(0.01, m_scaleY));
}

void ImageItem::setItemScale(qreal scale)
{
    setItemScale(scale, scale);
}

void ImageItem::setItemScale(qreal scaleX, qreal scaleY)
{
    m_scaleX = qMax(0.01, scaleX);
    m_scaleY = qMax(0.01, scaleY);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemShear(qreal shear)
{
    // Keep parallelograms editable; extreme k collapses chrome.
    m_shear = qBound(-5.0, shear, 5.0);
    applyLocalTransform();
    prepareGeometryChange();
}

static qreal normalizeDegrees(qreal degrees)
{
    while (degrees >= 360.0) {
        degrees -= 360.0;
    }
    while (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

void ImageItem::setItemRotation(qreal degrees)
{
    // Placement only — never content. Content 90° turns use bakeRotate90().
    m_rotation = normalizeDegrees(degrees);
    m_orientation = 0.0;
    m_fineRotation = m_rotation;
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::bakeRotate90(int quarterTurns)
{
    if (quarterTurns == 0) {
        return;
    }
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    if (quarterTurns == 0) {
        return;
    }
    // Gallery soft tiles often have only m_preview (m_source null). Bake must
    // still transform displayed pixels so content rotate is visible before a
    // full decode arrives; applyContentBakes will re-bake the full source later.
    if (m_source.isNull() && m_preview.isNull()) {
        return;
    }
    prepareGeometryChange();
    QTransform xform;
    xform.rotate(90.0 * quarterTurns);
    if (!m_source.isNull()) {
        m_source = m_source.transformed(xform, Qt::SmoothTransformation);
    }
    if (!m_preview.isNull()) {
        m_preview = m_preview.transformed(xform, Qt::SmoothTransformation);
    }
    // Layout / imageSize / contentRect must follow content orientation. Odd
    // 90° steps swap axes — including soft-only tiles (probe intrinsic).
    // Leaving intrinsic in the pre-rotate aspect made Gallery pack and the
    // selection AABB keep the old bounding box while pixels looked rotated.
    const bool swapAxes = (quarterTurns % 2) != 0;
    if (!m_source.isNull() && !m_previewPixels) {
        m_intrinsicSize = m_source.size();
        setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
    } else {
        if (swapAxes && m_intrinsicSize.isValid()
            && m_intrinsicSize.width() > 0 && m_intrinsicSize.height() > 0) {
            m_intrinsicSize.transpose();
        }
        const QSize s = imageSize();
        setOffset(-s.width() / 2.0, -s.height() / 2.0);
    }
    // Flips stay as flags or already baked; keep placement angle.
    updateDisplayedPixmap();
    applyLocalTransform();
    invalidateDeviceCache();
}

void ImageItem::bakeFlip(bool horizontal, bool vertical)
{
    if (!horizontal && !vertical) {
        return;
    }
    // Soft Gallery tiles: m_source is empty, paint draws m_preview. Transform
    // both so the user sees the flip immediately; full decode re-applies from
    // appearance content flags via applyContentBakes.
    if (m_source.isNull() && m_preview.isNull()) {
        return;
    }
    prepareGeometryChange();
    auto axesFrom = [](bool h, bool v) {
        Qt::Orientations axes;
        if (h) {
            axes |= Qt::Horizontal;
        }
        if (v) {
            axes |= Qt::Vertical;
        }
        return axes;
    };
    const Qt::Orientations axes = axesFrom(horizontal, vertical);
    if (!m_source.isNull() && axes) {
        m_source = m_source.flipped(axes);
    }
    if (!m_preview.isNull() && axes) {
        m_preview = m_preview.flipped(axes);
    }
    // Bake any pending display flips into the same op.
    if (m_hFlip || m_vFlip) {
        const Qt::Orientations axes2 = axesFrom(m_hFlip, m_vFlip);
        if (!m_source.isNull() && axes2) {
            m_source = m_source.flipped(axes2);
        }
        if (!m_preview.isNull() && axes2) {
            m_preview = m_preview.flipped(axes2);
        }
    }
    m_hFlip = false;
    m_vFlip = false;
    // Flip keeps width/height; still re-sync offset to current display size so
    // QGraphicsPixmapItem AABB and contentRect stay aligned after the bake.
    if (!m_source.isNull() && !m_previewPixels) {
        m_intrinsicSize = m_source.size();
        setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
    } else {
        const QSize s = imageSize();
        setOffset(-s.width() / 2.0, -s.height() / 2.0);
    }
    updateDisplayedPixmap();
    applyLocalTransform();
    invalidateDeviceCache();
}

void ImageItem::rotateOrientationBy(qreal degrees)
{
    // Legacy: treat as content bake (±90 only).
    const int steps = qRound(degrees / 90.0);
    if (steps != 0) {
        bakeRotate90(steps);
    }
}

void ImageItem::setOrientation(qreal degrees)
{
    Q_UNUSED(degrees);
    // Content orientation is pixel data; no separate transform.
    m_orientation = 0.0;
}

void ImageItem::setFineRotation(qreal degrees)
{
    // Alias: free-rotate is placement rotation.
    setItemRotation(degrees);
}

void ImageItem::zoomBy(qreal factor)
{
    setItemScale(m_scaleX * factor, m_scaleY * factor);
}

void ImageItem::rotateBy(qreal degrees)
{
    // Generic spin (shortcuts): treat as total-angle change and re-decompose.
    setItemRotation(m_rotation + degrees);
}

void ImageItem::setItemOpacity(qreal opacity)
{
    m_opacity = qBound(0.05, opacity, 1.0);
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

void ImageItem::toggleHFlip()
{
    bakeFlip(true, false);
}

void ImageItem::toggleVFlip()
{
    bakeFlip(false, true);
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
    m_galleryHovered = false;
    m_hoverHandle = Handle::None;
    m_activeHandle = Handle::None;
    // Crop is owned by the layout; clear when leaving gallery selectable.
    if (!on) {
        m_galleryCellSize = QSizeF();
    }
    if (on) {
        setAcceptHoverEvents(true);
        // Gallery: smooth scale (bilinear) so soft thumbs look less blocky when
        // the view zoom is not 1:1. Soft tiles stay NoCache — DeviceCoordinate
        // after soft install used to freeze the empty "⋯" placeholder until hover.
        setTransformationMode(Qt::SmoothTransformation);
        if (m_previewPixels && !m_preview.isNull()) {
            setCacheMode(QGraphicsItem::NoCache);
        } else {
            setCacheMode(QGraphicsItem::DeviceCoordinateCache);
        }
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

void ImageItem::invalidateDeviceCache()
{
    // DeviceCoordinateCache freezes paint() output (including the Gallery
    // selection frame). Toggle cache mode so the next paint sees current
    // QStyle::State_Selected.
    if (cacheMode() == QGraphicsItem::DeviceCoordinateCache) {
        setCacheMode(QGraphicsItem::NoCache);
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
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
    const qreal s = qMax(0.001, qMax(m_scaleX, m_scaleY));
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
    m_colorAdjust = adj;
    updateDisplayedPixmap();
    update();
}

void ImageItem::updateDisplayedPixmap()
{
    if (m_source.isNull()) {
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

bool ImageItem::cropToLocalRect(const QRectF &localRect, const QColor &padColor,
                                qreal rotationDegrees)
{
    if (m_source.isNull()) {
        return false;
    }
    const QRectF local = localRect.normalized();
    if (local.width() < 1.0 || local.height() < 1.0) {
        return false;
    }
    const QPointF off = offset();
    const int dw = qMax(1, qRound(local.width()));
    const int dh = qMax(1, qRound(local.height()));
    // Centre of the crop in source pixel coordinates.
    const QPointF srcCenter(local.center().x() - off.x(),
                            local.center().y() - off.y());

    QImage::Format fmt = m_source.format();
    if (fmt == QImage::Format_Invalid) {
        fmt = QImage::Format_ARGB32_Premultiplied;
    }
    if (padColor.alpha() < 255 && fmt != QImage::Format_ARGB32
        && fmt != QImage::Format_ARGB32_Premultiplied) {
        fmt = QImage::Format_ARGB32_Premultiplied;
    }

    QImage cropped;
    const bool rotated = std::abs(rotationDegrees) > 0.05;
    if (!rotated) {
        const int dx = qRound(local.left() - off.x());
        const int dy = qRound(local.top() - off.y());
        const QRect bounds(0, 0, m_source.width(), m_source.height());
        const QRect destBounds(dx, dy, dw, dh);
        const QRect srcRect = destBounds.intersected(bounds);
        if (srcRect == destBounds && srcRect.width() > 0 && srcRect.height() > 0) {
            cropped = m_source.copy(srcRect);
        } else {
            cropped = QImage(dw, dh, fmt);
            if (cropped.isNull()) {
                return false;
            }
            cropped.fill(padColor);
            if (srcRect.width() > 0 && srcRect.height() > 0) {
                QPainter painter(&cropped);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawImage(QPoint(srcRect.x() - dx, srcRect.y() - dy),
                                  m_source, srcRect);
                painter.end();
            }
        }
    } else {
        // Sample a rotated window into an axis-aligned output (straightened).
        cropped = QImage(dw, dh, fmt);
        if (cropped.isNull()) {
            return false;
        }
        cropped.fill(padColor);
        QPainter painter(&cropped);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // out pixel (x,y) ← source at R*(out - centre) + srcCentre
        painter.translate(dw / 2.0, dh / 2.0);
        painter.rotate(-rotationDegrees);
        painter.translate(-srcCenter.x(), -srcCenter.y());
        painter.drawImage(0, 0, m_source);
        painter.end();
    }
    if (cropped.isNull()) {
        return false;
    }
    m_hFlip = false;
    m_vFlip = false;
    setSourceImage(cropped);
    return true;
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
    if (!m_source.isNull() && !pixmap().isNull()) {
        return QGraphicsPixmapItem::boundingRect();
    }
    const QSize s = imageSize();
    return QRectF(offset(), QSizeF(s));
}

