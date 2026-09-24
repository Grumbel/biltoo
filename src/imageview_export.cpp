// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// High-resolution export / native size helpers.
// Blocking display materialize: DisplayPipelineController.

#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QImage>
#include <QTransform>
#include <algorithm>

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
    if (!painter || !sourceScene.isValid() || !targetRect.isValid()) {
        return;
    }
    QList<ImageItem *> ordered = m_items;
    std::sort(ordered.begin(), ordered.end(), [](ImageItem *a, ImageItem *b) {
        if (!a) {
            return false;
        }
        if (!b) {
            return true;
        }
        return a->zValue() < b->zValue();
    });

    QTransform sceneToPixel;
    sceneToPixel.translate(targetRect.left(), targetRect.top());
    sceneToPixel.scale(targetRect.width() / sourceScene.width(),
                       targetRect.height() / sourceScene.height());
    sceneToPixel.translate(-sourceScene.left(), -sourceScene.top());

    for (ImageItem *item : ordered) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const QRectF sceneR = item->contentSceneRect();
        if (!sceneR.isValid() || sceneR.isEmpty() || !sceneR.intersects(sourceScene)) {
            continue;
        }
        const QImage display = blockingExportDisplayForItem(item);
        if (display.isNull()) {
            continue;
        }
        painter->save();
        painter->setOpacity(item->opacity());
        painter->setTransform(sceneToPixel * item->sceneTransform());
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(item->contentRect(), display);
        painter->restore();
    }
}


QRectF ImageView::contentExportBounds() const
{
    QRectF bounds;
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QRectF r = item->contentSceneRect();
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    if (!bounds.isValid() || bounds.isEmpty()) {
        if (m_scene) {
            bounds = m_scene->itemsBoundingRect();
        }
    }
    if (bounds.isValid() && !bounds.isEmpty()) {
        bounds.adjust(-4, -4, 4, 4);
    }
    return bounds;
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
    paintHighResExportItems(&painter, sourceSceneRect, fitted);
    return img;
}

