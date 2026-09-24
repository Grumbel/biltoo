// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with display/ ownership.

#include "imageview.h"
#include "host/thumtoocache.h"
#include "content/contentxform.h"
#include "imageitem.h"
#include "view/viewtransform.h"
#include <QPainter>
#include <QImage>
#include <QTransform>

// --- from src/imageview_export.cpp ---
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


// --- from imageview_size_book.cpp ---
QSize ImageView::contentLayoutSize(const QString &path, SessionImageId sessionId,
                                   bool allowStoreAppearance) const
{
    WorkspaceItemState boundWant;
    const WorkspaceItemState *boundPtr = nullptr;
    bool hasBoundDurable = false;
    bool hasContentOrient = false;
    if (sessionId != kInvalidSessionImageId && hasSessionAppearance(sessionId)) {
        boundWant = sessionAppearanceValue(sessionId);
        boundPtr = &boundWant;
        hasBoundDurable = true;
        hasContentOrient = m_itemWorld.hasContentOrient(sessionId);
    }
    const WorkspaceItemState *pathState = nullptr;
    if (sessionId == kInvalidSessionImageId && !path.isEmpty()) {
        pathState = m_itemWorld.getPathState(path);
    }
    return SessionAppearance::resolveContentLayoutSize(
        logicalSizeForPath(path),
        m_size.book().known(path),
        allowStoreAppearance ? ThumtooCache::cachedSize(path) : QSize(),
        allowStoreAppearance,
        sessionId,
        path,
        hasBoundDurable,
        boundPtr,
        hasContentOrient,
        pathState);
}


void ImageView::applyProbedImageSize(const QString &path, const QSize &size)
{
    m_displayPipeline->applyProbedImageSize(path, size);
}

// --- Logical size (was imageview_view.cpp) ---


// --- Size book / probe: ImageSizeCoordinator (thin forwards) ---

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    m_size.rememberImageSize(path, size);
}

void ImageView::rememberSizeFromDecode(const QString &path, const QImage &image)
{
    m_size.rememberSizeFromDecode(path, image);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    return m_size.imageSizeForPath(path);
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    return m_size.layoutSizeForPath(path, previewHint);
}

void ImageView::primeGalleryGeometryFromCache(const QStringList &paths)
{
    m_size.primeGalleryGeometryFromCache(paths);
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    m_size.scheduleImageSizeProbe(path);
}

QSize ImageView::logicalSizeForPath(const QString &path) const
{
    return m_size.logicalSizeForPath(path);
}

QSize ImageView::ensureLogicalSizeForPath(const QString &path)
{
    return m_size.ensureLogicalSizeForPath(path);
}
