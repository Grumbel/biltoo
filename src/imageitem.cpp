// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
QString tooltipForHandle(ImageItem::Handle h)
{
    switch (h) {
    case ImageItem::Handle::FlipH:
        return QObject::tr("Flip horizontal");
    case ImageItem::Handle::FlipV:
        return QObject::tr("Flip vertical");
    case ImageItem::Handle::Raise:
        return QObject::tr("Raise (bring forward)");
    case ImageItem::Handle::Lower:
        return QObject::tr("Lower (send backward)");
    case ImageItem::Handle::OpacitySlider:
        return QObject::tr("Opacity");
    default:
        return QString();
    }
}
} // namespace

namespace {
constexpr qreal kHandleScreenPx = 13.0;
constexpr qreal kRotateOffsetPx = 32.0;
constexpr qreal kChromeBtnScreenPx = 28.0;   // raise / lower button diameter
constexpr qreal kChromeOffsetPx = 36.0;      // gap from image bottom to chrome row
constexpr qreal kChromeBtnGapPx = 10.0;      // gap between raise and lower
constexpr qreal kSliderWidthPx = 120.0;
constexpr qreal kSliderHeightPx = 14.0;
constexpr qreal kSliderGapPx = 12.0;         // gap from lower button to slider
} // namespace

ImageItem::ImageItem(const QString &path, const QImage &image, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent)
    , m_path(path)
    , m_source(image)
{
    setTransformationMode(Qt::SmoothTransformation);
    // Classic viewer by default: not selectable/movable until workspace mode
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setOffset(-image.width() / 2.0, -image.height() / 2.0); // origin at centre
    updateDisplayedPixmap();
    applyLocalTransform();
}

void ImageItem::setItemScale(qreal scale)
{
    m_scale = qMax(0.01, scale);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemRotation(qreal degrees)
{
    m_rotation = degrees;
    while (m_rotation >= 360.0) {
        m_rotation -= 360.0;
    }
    while (m_rotation < 0.0) {
        m_rotation += 360.0;
    }
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::zoomBy(qreal factor)
{
    setItemScale(m_scale * factor);
}

void ImageItem::rotateBy(qreal degrees)
{
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

void ImageItem::refreshStackingOrder()
{
    if (!m_interactive || !isSelected() || !scene()) {
        setZValue(m_stackZ);
        return;
    }
    qreal maxZ = m_stackZ;
    for (QGraphicsItem *gi : scene()->items()) {
        if (gi == this) {
            continue;
        }
        if (auto *other = qgraphicsitem_cast<ImageItem *>(gi)) {
            maxZ = qMax(maxZ, other->stackZ());
        } else {
            maxZ = qMax(maxZ, gi->zValue());
        }
    }
    setZValue(maxZ + 1.0);
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
    setItemHFlip(!m_hFlip);
}

void ImageItem::toggleVFlip()
{
    setItemVFlip(!m_vFlip);
}

void ImageItem::setInteractive(bool on)
{
    m_interactive = on;
    if (on) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges
                 | ItemIsFocusable);
    } else {
        setSelected(false);
        setFlags(ItemSendsGeometryChanges);
    }
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
    // Scale and rotate only — flips are applied to the pixmap so handles
    // stay on the geometric top/left/right of the item frame.
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    setTransform(t);
}

void ImageItem::updateDisplayedPixmap()
{
    if (!m_hFlip && !m_vFlip) {
        setPixmap(QPixmap::fromImage(m_source));
        return;
    }
    Qt::Orientations axes;
    if (m_hFlip) {
        axes |= Qt::Horizontal;
    }
    if (m_vFlip) {
        axes |= Qt::Vertical;
    }
    setPixmap(QPixmap::fromImage(m_source.flipped(axes)));
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
    return boundingRect(); // overridden below; use pixmap rect
}

QRectF ImageItem::boundingRect() const
{
    // Expand for handles when selected so they are not clipped
    QRectF r = QGraphicsPixmapItem::boundingRect();
    if (isSelected() && m_interactive) {
        const qreal ss = screenScale();
        const qreal rotPad = handleHitRadius() + kRotateOffsetPx / ss + 4.0;
        const qreal chromePad = chromeButtonSize() + kChromeOffsetPx / ss + 8.0;
        const qreal sidePad = qMax(handleHitRadius() + 4.0, rotPad);
        r.adjust(-sidePad, -rotPad, sidePad, chromePad);
        // Upright chrome can sit outside the simple bottom pad when rotated
        r = r.united(opacitySliderRect().adjusted(-chromePad, -chromePad, chromePad, chromePad));
        for (Handle h : {Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower}) {
            const QPointF c = handleCenter(h);
            const qreal rad = chromeButtonSize();
            r = r.united(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2));
        }
    }
    return r;
}

QPainterPath ImageItem::shape() const
{
    // Hit-test the content plus handles when selected
    QPainterPath path;
    path.addRect(QGraphicsPixmapItem::boundingRect());
    if (isSelected() && m_interactive) {
        const qreal r = handleHitRadius();
        QList<Handle> handles = {
            Handle::RotateTop, Handle::RotateRight, Handle::RotateLeft
        };
        if (m_scaleHandlesEnabled) {
            handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                    Handle::ScaleBottomLeft, Handle::ScaleBottomRight}
                      + handles;
        }
        for (Handle h : handles) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, r, r);
        }
        const qreal cr = chromeButtonSize() * 0.6;
        for (Handle h : {Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower}) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, cr, cr);
        }
        path.addRect(opacitySliderRect());
        const QRectF content = QGraphicsPixmapItem::boundingRect();
        path.moveTo(content.center().x(), content.top());
        path.lineTo(handleCenter(Handle::RotateTop));
        path.moveTo(content.right(), content.center().y());
        path.lineTo(handleCenter(Handle::RotateRight));
        path.moveTo(content.left(), content.center().y());
        path.lineTo(handleCenter(Handle::RotateLeft));
    }
    return path;
}

qreal ImageItem::screenScale() const
{
    // Item local scale × view transform scale → pixels per local unit
    qreal s = qMax(m_scale, 0.01);
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            const QTransform t = views.first()->transform();
            s *= std::hypot(t.m11(), t.m12());
        }
    }
    return qMax(s, 0.01);
}

qreal ImageItem::handleDrawSize() const
{
    // Constant size in screen pixels regardless of item or view zoom
    return kHandleScreenPx / screenScale();
}

qreal ImageItem::handleHitRadius() const
{
    return handleDrawSize() * 1.2;
}

void ImageItem::updateHandleLayout()
{
    prepareGeometryChange();
    update();
}

bool ImageItem::isChromeHandle(Handle h) const
{
    return h == Handle::FlipH || h == Handle::FlipV
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::OpacitySlider;
}

qreal ImageItem::chromeButtonSize() const
{
    return kChromeBtnScreenPx / screenScale();
}

QPointF ImageItem::toItemFromUpright(const QPointF &uprightLocal) const
{
    // Upright frame = item local with rotation removed. After the item's
    // rotation is applied by the view, these points sit under the
    // axis-aligned bounds with upright glyphs.
    if (qFuzzyIsNull(m_rotation)) {
        return uprightLocal;
    }
    QTransform inv;
    inv.rotate(-m_rotation);
    return inv.map(uprightLocal);
}

QPointF ImageItem::toUprightFromItem(const QPointF &itemLocal) const
{
    if (qFuzzyIsNull(m_rotation)) {
        return itemLocal;
    }
    QTransform rot;
    rot.rotate(m_rotation);
    return rot.map(itemLocal);
}

QRectF ImageItem::opacitySliderRectUpright() const
{
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal ss = screenScale();
    const qreal btn = kChromeBtnScreenPx / ss;
    const qreal gapBtn = kChromeBtnGapPx / ss;
    const qreal gapSlider = kSliderGapPx / ss;
    const qreal w = kSliderWidthPx / ss;
    const qreal h = kSliderHeightPx / ss;
    constexpr int kChromeBtns = 4;
    const qreal rowW = kChromeBtns * btn + (kChromeBtns - 1) * gapBtn + gapSlider + w;
    const qreal rowLeft = r.center().x() - rowW / 2.0;
    const qreal sliderLeft = rowLeft + kChromeBtns * btn + (kChromeBtns - 1) * gapBtn + gapSlider;
    const qreal y = r.bottom() + kChromeOffsetPx / ss + (btn - h) / 2.0;
    return QRectF(sliderLeft, y, w, h);
}

QRectF ImageItem::opacitySliderRect() const
{
    const QRectF upright = opacitySliderRectUpright();
    if (qFuzzyIsNull(m_rotation)) {
        return upright;
    }
    QPolygonF mapped;
    mapped << toItemFromUpright(upright.topLeft())
           << toItemFromUpright(upright.topRight())
           << toItemFromUpright(upright.bottomRight())
           << toItemFromUpright(upright.bottomLeft());
    return mapped.boundingRect();
}

void ImageItem::setOpacityFromSliderPos(const QPointF &itemPos)
{
    const QRectF track = opacitySliderRectUpright();
    if (track.width() <= 0) {
        return;
    }
    const QPointF upright = toUprightFromItem(itemPos);
    const qreal t = qBound(0.0, (upright.x() - track.left()) / track.width(), 1.0);
    setItemOpacity(0.05 + t * 0.95);
}

QPointF ImageItem::handleCenter(Handle h) const
{
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal ss = screenScale();
    const qreal rotOff = kRotateOffsetPx / ss;
    const qreal btn = kChromeBtnScreenPx / ss;
    const qreal gapBtn = kChromeBtnGapPx / ss;
    const qreal gapSlider = kSliderGapPx / ss;
    const qreal sliderW = kSliderWidthPx / ss;
    constexpr int kChromeBtns = 4;
    const qreal rowW = kChromeBtns * btn + (kChromeBtns - 1) * gapBtn + gapSlider + sliderW;
    const qreal rowLeft = r.center().x() - rowW / 2.0;
    const qreal chromeY = r.bottom() + kChromeOffsetPx / ss + btn / 2.0;
    const qreal cx = r.center().x();
    auto chromeBtnCenter = [&](int index) {
        const QPointF upright(rowLeft + index * (btn + gapBtn) + btn / 2.0, chromeY);
        return toItemFromUpright(upright);
    };
    switch (h) {
    case Handle::ScaleTopLeft:
        return r.topLeft();
    case Handle::ScaleTopRight:
        return r.topRight();
    case Handle::ScaleBottomLeft:
        return r.bottomLeft();
    case Handle::ScaleBottomRight:
        return r.bottomRight();
    case Handle::RotateTop:
        return QPointF(cx, r.top() - rotOff);
    case Handle::RotateRight:
        return QPointF(r.right() + rotOff, r.center().y());
    case Handle::RotateLeft:
        return QPointF(r.left() - rotOff, r.center().y());
    case Handle::FlipH:
        return chromeBtnCenter(0);
    case Handle::FlipV:
        return chromeBtnCenter(1);
    case Handle::Raise:
        return chromeBtnCenter(2);
    case Handle::Lower:
        return chromeBtnCenter(3);
    case Handle::OpacitySlider:
        return opacitySliderRect().center();
    default:
        return QPointF();
    }
}

ImageItem::Handle ImageItem::handleAt(const QPointF &itemPos) const
{
    if (!isSelected() || !m_interactive) {
        return Handle::None;
    }

    // Opacity slider: test in upright frame so rotation does not skew the hit box
    {
        const QRectF slider = opacitySliderRectUpright().adjusted(
            -4.0 / screenScale(), -6.0 / screenScale(),
            4.0 / screenScale(), 6.0 / screenScale());
        if (slider.contains(toUprightFromItem(itemPos))) {
            return Handle::OpacitySlider;
        }
    }

    // Raise / lower use larger chrome radius
    const qreal chromeR = chromeButtonSize() * 0.65;
    const qreal chromeR2 = chromeR * chromeR;
    for (Handle h : {Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower}) {
        const QPointF d = itemPos - handleCenter(h);
        if (QPointF::dotProduct(d, d) <= chromeR2) {
            return h;
        }
    }

    const qreal r = handleHitRadius();
    const qreal r2 = r * r;
    QList<Handle> handles = {
        Handle::RotateTop, Handle::RotateRight, Handle::RotateLeft
    };
    if (m_scaleHandlesEnabled) {
        handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                Handle::ScaleBottomLeft, Handle::ScaleBottomRight}
                  + handles;
    }
    for (Handle h : handles) {
        const QPointF d = itemPos - handleCenter(h);
        if (QPointF::dotProduct(d, d) <= r2) {
            return h;
        }
    }
    return Handle::None;
}

void ImageItem::notifyViewStatus()
{
    if (!scene()) {
        return;
    }
    for (QGraphicsView *v : scene()->views()) {
        QMetaObject::invokeMethod(v, "refreshStatus", Qt::DirectConnection);
    }
}

void ImageItem::activateChromeHandle(Handle h)
{
    switch (h) {
    case Handle::FlipH:
        toggleHFlip();
        break;
    case Handle::FlipV:
        toggleVFlip();
        break;
    case Handle::Raise:
        setStackZ(m_stackZ + 1.0);
        break;
    case Handle::Lower:
        setStackZ(m_stackZ - 1.0);
        break;
    default:
        break;
    }
    notifyViewStatus();
}

QVariant ImageItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged) {
        prepareGeometryChange();
        refreshStackingOrder();
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    // Pixmap at item opacity; handles always fully opaque
    {
        QStyleOptionGraphicsItem opt = *option;
        opt.state &= ~QStyle::State_Selected;
        painter->save();
        painter->setOpacity(m_opacity);
        QGraphicsPixmapItem::paint(painter, &opt, widget);
        painter->restore();
    }

    if (!(option->state & QStyle::State_Selected) || !m_interactive) {
        return;
    }

    painter->save();
    painter->setOpacity(1.0);

    const QRectF r = QGraphicsPixmapItem::boundingRect();
    QPen pen(QColor(0, 160, 255), 0); // cosmetic width via setCosmetic
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));

    // Lines to rotate handles
    const QPointF topMid(r.center().x(), r.top());
    const QPointF rightMid(r.right(), r.center().y());
    const QPointF leftMid(r.left(), r.center().y());
    painter->drawLine(topMid, handleCenter(Handle::RotateTop));
    painter->drawLine(rightMid, handleCenter(Handle::RotateRight));
    painter->drawLine(leftMid, handleCenter(Handle::RotateLeft));

    const qreal hs = handleDrawSize();
    pen.setWidthF(1.0);
    painter->setPen(pen);

    // Scale handles (squares) — only in free-form layouts
    if (m_scaleHandlesEnabled) {
        for (Handle h : {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                         Handle::ScaleBottomLeft, Handle::ScaleBottomRight}) {
            const QPointF c = handleCenter(h);
            const bool hot = (m_hoverHandle == h || m_activeHandle == h);
            const qreal s = hs * (hot ? 1.25 : 1.0);
            painter->setBrush(hot ? QColor(80, 200, 255) : QColor(0, 160, 255));
            QPen hp = pen;
            if (hot) {
                hp.setColor(Qt::white);
                hp.setWidthF(2.0);
                hp.setCosmetic(true);
            }
            painter->setPen(hp);
            painter->drawRect(QRectF(c.x() - s / 2, c.y() - s / 2, s, s));
            painter->setPen(pen);
        }
    }

    // Rotate handles (circles) on top / left / right
    for (Handle h : {Handle::RotateTop, Handle::RotateRight, Handle::RotateLeft}) {
        const QPointF c = handleCenter(h);
        const bool hot = (m_hoverHandle == h || m_activeHandle == h);
        const qreal s = hs * (hot ? 1.25 : 1.0);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp = pen;
        if (hot) {
            hp.setColor(Qt::white);
            hp.setWidthF(2.0);
            hp.setCosmetic(true);
        }
        painter->setPen(hp);
        painter->drawEllipse(c, s / 2, s / 2);
        painter->setPen(pen);
    }

    // Chrome row: flip / raise / lower / opacity — upright under the AABB
    painter->save();
    if (!qFuzzyIsNull(m_rotation)) {
        painter->rotate(-m_rotation);
    }
    {
        const QRectF r = QGraphicsPixmapItem::boundingRect();
        const qreal ss = screenScale();
        const qreal btn = kChromeBtnScreenPx / ss;
        const qreal gapBtn = kChromeBtnGapPx / ss;
        const qreal gapSlider = kSliderGapPx / ss;
        const qreal sliderW = kSliderWidthPx / ss;
        constexpr int kChromeBtns = 4;
        const qreal rowW = kChromeBtns * btn + (kChromeBtns - 1) * gapBtn + gapSlider + sliderW;
        const qreal rowLeft = r.center().x() - rowW / 2.0;
        const qreal chromeY = r.bottom() + kChromeOffsetPx / ss + btn / 2.0;
        const qreal btnR = btn / 2.0;

        auto uprightBtnCenter = [&](int index) {
            return QPointF(rowLeft + index * (btn + gapBtn) + btn / 2.0, chromeY);
        };
        auto drawChromeBtn = [&](Handle h, int index, const QString &glyph) {
            const QPointF c = uprightBtnCenter(index);
            const bool hovered = (m_hoverHandle == h);
            const bool active = (m_activeHandle == h);
            const qreal rad = btnR * (hovered || active ? 1.12 : 1.0);
            QColor fill = hovered || active ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border = pen;
            border.setColor(hovered || active ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered || active ? 2.0 : 1.0);
            border.setCosmetic(true);
            painter->setBrush(fill);
            painter->setPen(border);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QPen(Qt::white));
            painter->save();
            painter->translate(c);
            painter->scale(1.0 / ss, 1.0 / ss);
            QFont f = painter->font();
            f.setBold(true);
            f.setPixelSize(hovered || active ? 18 : 16);
            painter->setFont(f);
            painter->drawText(QRectF(-16, -16, 32, 32), Qt::AlignCenter, glyph);
            painter->restore();
        };
        drawChromeBtn(Handle::FlipH, 0, QStringLiteral("↔"));
        drawChromeBtn(Handle::FlipV, 1, QStringLiteral("↕"));
        drawChromeBtn(Handle::Raise, 2, QStringLiteral("↑"));
        drawChromeBtn(Handle::Lower, 3, QStringLiteral("↓"));

        const QRectF track = opacitySliderRectUpright();
        const qreal tval = qBound(0.0, (m_opacity - 0.05) / 0.95, 1.0);
        const bool hot = (m_hoverHandle == Handle::OpacitySlider
                          || m_activeHandle == Handle::OpacitySlider);
        QPen trackPen = Qt::NoPen;
        if (hot) {
            trackPen = QPen(QColor(255, 255, 255), 0);
            trackPen.setCosmetic(true);
            trackPen.setWidthF(1.5);
        }
        painter->setPen(trackPen);
        painter->setBrush(QColor(40, 40, 40, hot ? 230 : 200));
        painter->drawRoundedRect(track, 3.0 / ss, 3.0 / ss);
        QRectF filled = track;
        filled.setWidth(track.width() * tval);
        painter->setBrush(QColor(140, 100, 200, 230));
        painter->drawRoundedRect(filled, 3.0 / ss, 3.0 / ss);
        const QPointF thumb(track.left() + track.width() * tval, track.center().y());
        painter->setBrush(QColor(230, 230, 230));
        painter->setPen(QPen(QColor(30, 30, 30), 0));
        painter->drawEllipse(thumb, 7.0 / ss, 7.0 / ss);
    }
    painter->restore();

    painter->restore();
}

void ImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_interactive && event->button() == Qt::LeftButton) {
        const Handle h = handleAt(event->pos());
        if (h != Handle::None) {
            if (h == Handle::FlipH || h == Handle::FlipV
                || h == Handle::Raise || h == Handle::Lower) {
                activateChromeHandle(h);
                event->accept();
                return;
            }
            m_activeHandle = h;
            m_pressScenePos = event->scenePos();
            m_pressScale = m_scale;
            m_pressRotation = m_rotation;
            m_pressItemPos = event->pos();
            if (h == Handle::OpacitySlider) {
                setOpacityFromSliderPos(event->pos());
                notifyViewStatus();
            }
            event->accept();
            return;
        }
    }
    QGraphicsPixmapItem::mousePressEvent(event);
}

void ImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None) {
        if (m_activeHandle == Handle::OpacitySlider) {
            setOpacityFromSliderPos(event->pos());
        } else if (m_activeHandle == Handle::RotateTop
                   || m_activeHandle == Handle::RotateRight
                   || m_activeHandle == Handle::RotateLeft) {
            const QPointF centre = scenePos(); // item origin is image centre
            const QPointF v0 = m_pressScenePos - centre;
            const QPointF v1 = event->scenePos() - centre;
            const qreal a0 = qAtan2(v0.y(), v0.x());
            const qreal a1 = qAtan2(v1.y(), v1.x());
            const qreal deltaDeg = qRadiansToDegrees(a1 - a0);
            qreal angle = m_pressRotation + deltaDeg;
            // Shift → 45° snap, Ctrl → 90° snap (common CAD / design convention)
            if (event->modifiers() & Qt::ControlModifier) {
                angle = qRound(angle / 90.0) * 90.0;
            } else if (event->modifiers() & Qt::ShiftModifier) {
                angle = qRound(angle / 45.0) * 45.0;
            }
            setItemRotation(angle);
        } else {
            // Uniform scale from item centre based on distance ratio
            const QPointF centre = scenePos();
            const qreal d0 = QLineF(centre, m_pressScenePos).length();
            const qreal d1 = QLineF(centre, event->scenePos()).length();
            if (d0 > 1.0) {
                setItemScale(m_pressScale * (d1 / d0));
            }
        }
        notifyViewStatus();
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void ImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None && event->button() == Qt::LeftButton) {
        m_activeHandle = Handle::None;
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void ImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_interactive && isSelected()) {
        const Handle h = handleAt(event->pos());
        if (h != m_hoverHandle) {
            m_hoverHandle = h;
            setToolTip(tooltipForHandle(h));
            update();
        }
        switch (h) {
        case Handle::RotateTop:
        case Handle::RotateRight:
        case Handle::RotateLeft:
            setCursor(Qt::CrossCursor);
            break;
        case Handle::ScaleTopLeft:
        case Handle::ScaleBottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Handle::ScaleTopRight:
        case Handle::ScaleBottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Handle::FlipH:
        case Handle::FlipV:
        case Handle::Raise:
        case Handle::Lower:
            setCursor(Qt::PointingHandCursor);
            break;
        case Handle::OpacitySlider:
            setCursor(Qt::SizeHorCursor);
            break;
        default:
            unsetCursor();
            break;
        }
    } else {
        unsetCursor();
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void ImageItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        setToolTip(QString());
        update();
    }
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}
