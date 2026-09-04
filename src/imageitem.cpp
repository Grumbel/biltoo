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
    if (!m_source.isNull()) {
        return m_source.size();
    }
    return m_intrinsicSize.isValid() ? m_intrinsicSize : QSize(1, 1);
}

void ImageItem::setSourceImage(const QImage &image)
{
    prepareGeometryChange();
    m_source = image;
    if (!m_source.isNull()) {
        m_intrinsicSize = m_source.size();
        setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
        updateDisplayedPixmap();
    } else {
        setPixmap(QPixmap());
        const QSize s = imageSize();
        setOffset(-s.width() / 2.0, -s.height() / 2.0);
    }
    applyLocalTransform();
    update();
}

void ImageItem::clearDecodedPixels()
{
    if (m_source.isNull()) {
        return;
    }
    prepareGeometryChange();
    if (!m_intrinsicSize.isValid() || m_intrinsicSize.isEmpty()) {
        m_intrinsicSize = m_source.size();
    }
    m_source = QImage();
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
    if (m_source.isNull() || quarterTurns == 0) {
        return;
    }
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    if (quarterTurns == 0) {
        return;
    }
    prepareGeometryChange();
    QTransform xform;
    xform.rotate(90.0 * quarterTurns);
    m_source = m_source.transformed(xform, Qt::SmoothTransformation);
    m_intrinsicSize = m_source.size();
    setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
    // Flips stay as flags or already baked; keep placement angle.
    updateDisplayedPixmap();
    applyLocalTransform();
    update();
}

void ImageItem::bakeFlip(bool horizontal, bool vertical)
{
    if (m_source.isNull() || (!horizontal && !vertical)) {
        return;
    }
    prepareGeometryChange();
    {
        Qt::Orientations axes;
        if (horizontal) {
            axes |= Qt::Horizontal;
        }
        if (vertical) {
            axes |= Qt::Vertical;
        }
        if (axes) {
            m_source = m_source.flipped(axes);
        }
    }
    // Bake any pending display flips into the same op.
    if (m_hFlip) {
        m_source = m_source.flipped(Qt::Horizontal);
    }
    if (m_vFlip) {
        m_source = m_source.flipped(Qt::Vertical);
    }
    m_hFlip = false;
    m_vFlip = false;
    m_intrinsicSize = m_source.size();
    setOffset(-m_source.width() / 2.0, -m_source.height() / 2.0);
    updateDisplayedPixmap();
    applyLocalTransform();
    update();
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
        // Gallery packs many tiles: FastTransformation + device cache so
        // selection only invalidates the two tiles that change, not a
        // full re-scale of every pixmap (OpenGL still redraws the view,
        // but cached tiles are cheap blits).
        setTransformationMode(Qt::FastTransformation);
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
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
        if (m_hFlip) axes |= Qt::Horizontal;
        if (m_vFlip) axes |= Qt::Vertical;
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

