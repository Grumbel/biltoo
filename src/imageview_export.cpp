// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// High-resolution export / native size helpers.
// Blocking display + high-res paint: DisplayPipelineController.

#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QImage>
#include <QTransform>

QSizeF ImageView::nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    // Logical size — never soft display pixmap dimensions.
    return QSizeF(item->imageSize());
}

QImage ImageView::blockingExportDisplayForItem(const ImageItem *item) const
{
    return m_displayPipeline->blockingExportDisplayForItem(item);
}

void ImageView::paintHighResExportItems(QPainter *painter, const QRectF &sourceScene,
                                        const QRectF &targetRect) const
{
    m_displayPipeline->paintHighResExportItems(painter, sourceScene, targetRect);
}

QRectF ImageView::contentExportBounds() const
{
    return m_displayPipeline->contentExportBounds();
}

QImage ImageView::renderExportImage(const QSize &pixelSize, const QRectF &sourceSceneRect,
                                    bool transparentBackground) const
{
    if (!m_scene || !pixelSize.isValid() || pixelSize.width() < 1 || pixelSize.height() < 1
        || !sourceSceneRect.isValid() || sourceSceneRect.isEmpty()) {
        return {};
    }
    QImage img(pixelSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Target rect with aspect preserved (same as QGraphicsScene::render KeepAspectRatio).
    const QRectF target(QPointF(0, 0), QSizeF(pixelSize));
    const QRectF fitted = ViewTransform::fitRectCentered(
        target, sourceSceneRect.size());

    if (!transparentBackground) {
        painter.save();
        QTransform xform;
        xform.translate(fitted.left(), fitted.top());
        xform.scale(fitted.width() / sourceSceneRect.width(),
                    fitted.height() / sourceSceneRect.height());
        xform.translate(-sourceSceneRect.left(), -sourceSceneRect.top());
        painter.setTransform(xform);
        const qreal exportScale = fitted.width() / sourceSceneRect.width();
        const_cast<ImageView *>(this)->paintCanvasBackground(
            &painter, sourceSceneRect, exportScale);
        painter.restore();
    }

    // High-res host materialize per item — do not scene-render Soft on-screen samples.
    m_displayPipeline->paintHighResExportItems(&painter, sourceSceneRect, fitted);
    return img;
}
