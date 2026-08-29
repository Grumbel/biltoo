// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImageReader>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QtMath>
#include <QFileInfo>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(40, 40, 40)));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
}

ImageView::~ImageView() = default;

bool ImageView::loadImage(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // honour Exif orientation

    const QImage image = reader.read();
    if (image.isNull()) {
        return false;
    }

    m_scene->clear();
    m_pixmapItem = m_scene->addPixmap(QPixmap::fromImage(image));
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);

    m_imageSize = image.size();
    m_currentPath = path;
    m_scale = 1.0;
    m_rotation = 0.0;
    m_fitMode = true;

    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    applyTransform();
    zoomFit();

    emit statusChanged();
    return true;
}

void ImageView::zoomIn()
{
    m_fitMode = false;
    m_scale *= 1.25;
    applyTransform();
    emit statusChanged();
}

void ImageView::zoomOut()
{
    m_fitMode = false;
    m_scale /= 1.25;
    if (m_scale < 0.01) {
        m_scale = 0.01;
    }
    applyTransform();
    emit statusChanged();
}

void ImageView::zoomReset()
{
    m_fitMode = false;
    m_scale = 1.0;
    applyTransform();
    emit statusChanged();
}

void ImageView::zoomFit()
{
    if (!m_pixmapItem) {
        return;
    }
    m_fitMode = true;
    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    // Derive current scale from the transform for status display
    const QTransform t = transform();
    m_scale = t.m11(); // approximate; rotation complicates exact scale
    emit statusChanged();
}

void ImageView::rotateLeft()
{
    m_rotation -= 90.0;
    if (m_rotation <= -360.0) {
        m_rotation += 360.0;
    }
    applyTransform();
    if (m_fitMode) {
        zoomFit();
    }
    emit statusChanged();
}

void ImageView::rotateRight()
{
    m_rotation += 90.0;
    if (m_rotation >= 360.0) {
        m_rotation -= 360.0;
    }
    applyTransform();
    if (m_fitMode) {
        zoomFit();
    }
    emit statusChanged();
}

QString ImageView::statusText() const
{
    if (m_currentPath.isEmpty()) {
        return tr("Ready");
    }
    const QString name = QFileInfo(m_currentPath).fileName();
    return tr("%1  |  %2×%3  |  Zoom: %4%  |  Rotation: %5°")
        .arg(name)
        .arg(m_imageSize.width())
        .arg(m_imageSize.height())
        .arg(qRound(m_scale * 100))
        .arg(qRound(m_rotation));
}

void ImageView::applyTransform()
{
    if (!m_pixmapItem) {
        return;
    }

    QTransform t;
    // Rotate around the center of the pixmap
    const QRectF br = m_pixmapItem->boundingRect();
    t.translate(br.center().x(), br.center().y());
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    t.translate(-br.center().x(), -br.center().y());

    m_pixmapItem->setTransform(t);
    // Update scene rect so scrolling works with rotated content
    m_scene->setSceneRect(m_pixmapItem->sceneBoundingRect());
}

void ImageView::updateFitIfNeeded()
{
    if (m_fitMode && m_pixmapItem) {
        fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateFitIfNeeded();
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
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
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
