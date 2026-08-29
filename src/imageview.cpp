// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"

#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(36, 36, 36)));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAlignment(Qt::AlignCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

ImageView::~ImageView() = default;

ImageItem *ImageView::loadItem(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    item->setInteractive(m_workspaceMode);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

void ImageView::clearWorkspace()
{
    m_scene->clear();
    m_items.clear();
    m_mouseInfo = {};
    m_rotateItem = nullptr;
    m_rotating = false;
    emit mouseInfoChanged(m_mouseInfo);
}

void ImageView::clearExtras()
{
    if (m_items.size() <= 1) {
        return;
    }
    ImageItem *keep = m_items.first();
    for (int i = m_items.size() - 1; i >= 1; --i) {
        ImageItem *item = m_items.takeAt(i);
        m_scene->removeItem(item);
        delete item;
    }
    m_scene->clearSelection();
    if (m_workspaceMode) {
        keep->setSelected(true);
    }
    m_fitMode = true;
    fitItem(keep);
    emit statusChanged();
}

void ImageView::setWorkspaceMode(bool on)
{
    m_workspaceMode = on;
    for (ImageItem *item : m_items) {
        item->setInteractive(on);
    }
    if (!on) {
        m_scene->clearSelection();
        clearExtras();
    }
}

bool ImageView::loadImage(const QString &path)
{
    clearWorkspace();
    ImageItem *item = loadItem(path);
    if (!item) {
        return false;
    }
    if (m_workspaceMode) {
        item->setSelected(true);
    }
    m_fitMode = true;
    fitItem(item);
    emit statusChanged();
    return true;
}

bool ImageView::addImage(const QString &path)
{
    if (!m_workspaceMode) {
        return false;
    }

    ImageItem *item = loadItem(path);
    if (!item) {
        return false;
    }

    // Offset newly added items so they do not fully cover existing ones
    const int n = m_items.size();
    if (n > 1) {
        const qreal dx = 40.0 * (n - 1);
        const qreal dy = 30.0 * (n - 1);
        item->setPos(dx, dy);
    }

    m_scene->clearSelection();
    item->setSelected(true);
    m_fitMode = false;
    ensureVisibleItem(item);
    emit statusChanged();
    return true;
}

ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            return item;
        }
    }
    if (m_items.size() == 1) {
        return m_items.first();
    }
    return nullptr;
}

void ImageView::zoomIn()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->zoomBy(1.25);
        emit statusChanged();
    }
}

void ImageView::zoomOut()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->zoomBy(1.0 / 1.25);
        emit statusChanged();
    }
}

void ImageView::zoomReset()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->setItemScale(1.0);
        emit statusChanged();
    }
}

void ImageView::zoomFit()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = true;
        fitItem(item);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        // Fit the whole scene
        m_fitMode = true;
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        emit statusChanged();
    }
}

void ImageView::rotateLeft()
{
    if (ImageItem *item = targetItem()) {
        item->rotateBy(-90.0);
        if (m_fitMode) {
            fitItem(item);
        }
        emit statusChanged();
    }
}

void ImageView::rotateRight()
{
    if (ImageItem *item = targetItem()) {
        item->rotateBy(90.0);
        if (m_fitMode) {
            fitItem(item);
        }
        emit statusChanged();
    }
}

void ImageView::raiseSelected()
{
    if (ImageItem *item = targetItem()) {
        item->setZValue(item->zValue() + 1.0);
        emit statusChanged();
    }
}

void ImageView::lowerSelected()
{
    if (ImageItem *item = targetItem()) {
        item->setZValue(item->zValue() - 1.0);
        emit statusChanged();
    }
}

void ImageView::opacityUp()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() + 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() - 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(1.0);
        emit statusChanged();
    }
}

void ImageView::layoutSideBySide()
{
    if (m_items.size() < 2) {
        return;
    }

    qreal x = 0.0;
    const qreal gap = 24.0;
    qreal maxH = 0.0;

    for (ImageItem *item : m_items) {
        // Reset rotation for a clean comparison layout; keep scale
        item->setItemRotation(0.0);
        const QRectF br = item->sceneBoundingRect();
        maxH = qMax(maxH, br.height());
    }

    for (ImageItem *item : m_items) {
        const QRectF br = item->boundingRect();
        // Position so the top edges align; origin is centre
        const qreal w = br.width() * item->itemScale();
        const qreal h = br.height() * item->itemScale();
        item->setPos(x + w / 2.0, h / 2.0);
        x += w + gap;
    }

    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32));
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_fitMode = true;
    emit statusChanged();
}

void ImageView::fitItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    fitInView(item, Qt::KeepAspectRatio);
    // Derive approximate scale from the view transform for status display
    const QTransform t = transform();
    const qreal viewScale = std::hypot(t.m11(), t.m12());
    // Item keeps its own scale at 1 when we use view-level fit for a single item;
    // for multi-item we prefer item-level scale. Reset item scale and use view fit
    // only when a single item is present.
    if (m_items.size() == 1) {
        item->setItemScale(1.0);
        item->setItemRotation(item->itemRotation());
        fitInView(item, Qt::KeepAspectRatio);
        Q_UNUSED(viewScale);
    }
}

void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(item, 32, 32);
    }
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

QSize ImageView::imageSize() const
{
    if (ImageItem *item = targetItem()) {
        return item->imageSize();
    }
    if (ImageItem *item = primaryItem()) {
        return item->imageSize();
    }
    return {};
}

int ImageView::itemCount() const
{
    return m_items.size();
}

QStringList ImageView::itemPaths() const
{
    QStringList paths;
    for (ImageItem *item : m_items) {
        paths.append(item->path());
    }
    return paths;
}

QString ImageView::statusText() const
{
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (!item) {
        return tr("Ready");
    }

    const QString name = QFileInfo(item->path()).fileName();
    QString text = tr("%1  |  %2×%3  |  Zoom: %4%  |  Rotation: %5°")
                       .arg(name)
                       .arg(item->imageSize().width())
                       .arg(item->imageSize().height())
                       .arg(qRound(item->itemScale() * 100))
                       .arg(qRound(item->itemRotation()));
    if (item->itemOpacity() < 0.999) {
        text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
    }

    if (m_items.size() > 1) {
        text = tr("Workspace: %1 images  |  %2").arg(m_items.size()).arg(text);
    }
    return text;
}

void ImageView::updateMouseInfo(const QPoint &viewPos)
{
    ImageMouseInfo info;
    const QPointF scenePos = mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = item;
            break;
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (info.valid != m_mouseInfo.valid
        || info.imagePos != m_mouseInfo.imagePos
        || info.pixelColor != m_mouseInfo.pixelColor
        || info.path != m_mouseInfo.path) {
        m_mouseInfo = info;
        emit mouseInfoChanged(m_mouseInfo);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    // Zoom the item under the mouse, or the selection
    ImageItem *item = nullptr;
    const QPointF scenePos = mapToScene(event->position().toPoint());
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            item = ii;
            break;
        }
    }
    if (!item) {
        item = targetItem();
    }
    if (!item) {
        return;
    }

    m_fitMode = false;
    if (event->angleDelta().y() > 0) {
        item->zoomBy(1.25);
    } else {
        item->zoomBy(1.0 / 1.25);
    }
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first());
    }
}

qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    const QPointF c = item->scenePos();
    return qRadiansToDegrees(std::atan2(scenePos.y() - c.y(), scenePos.x() - c.x()));
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    // Classic viewer: left-drag pans; no item selection/move
    if (!m_workspaceMode && event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::ShiftModifier)) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && event->modifiers() & Qt::AltModifier)) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Shift + left button: free continuous rotation of the item under the cursor
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
        ImageItem *hit = nullptr;
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        if (!hit) {
            hit = targetItem();
        }
        if (hit) {
            m_rotating = true;
            m_rotateItem = hit;
            m_rotateStartAngle = angleAt(scenePos, hit);
            m_rotateItemStart = hit->itemRotation();
            m_scene->clearSelection();
            hit->setSelected(true);
            setCursor(Qt::CrossCursor);
            event->accept();
            return;
        }
    }

    // Left click: let QGraphicsView handle selection / ItemIsMovable drag
    if (event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        emit statusChanged();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    updateMouseInfo(event->pos());

    if (m_rotating && m_rotateItem) {
        const QPointF scenePos = mapToScene(event->pos());
        const qreal angle = angleAt(scenePos, m_rotateItem);
        const qreal delta = angle - m_rotateStartAngle;
        m_rotateItem->setItemRotation(m_rotateItemStart + delta);
        m_fitMode = false;
        emit statusChanged();
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_rotating && event->button() == Qt::LeftButton) {
        m_rotating = false;
        m_rotateItem = nullptr;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_mouseInfo.valid) {
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
    }
    QGraphicsView::leaveEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Remove selected items from the workspace (session list is unchanged)
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        bool removed = false;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                m_items.removeOne(item);
                m_scene->removeItem(item);
                delete item;
                removed = true;
            }
        }
        if (removed) {
            emit statusChanged();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
