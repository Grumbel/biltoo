// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// High-resolution export / native size helpers.

#include "imageview.h"
#include "imageitem.h"
#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "session/sessionappearance.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QImage>
#include "display/imagecache.h"
#include "host/imageloader.h"
#include "util/biltoo_thread.h"
#include <QMutex>
#include <QWaitCondition>
#include <QThreadPool>

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
    if (!item || item->path().isEmpty()) {
        return {};
    }
    const QString path = item->path();
    const WorkspaceItemState want = m_displayPipeline->wantAppearanceForItem(item, item->sessionId());
    const QImage fallback = item->displayImage();

    struct Shared {
        QMutex mu;
        QWaitCondition cv;
        QImage out;
        bool done = false;
    };
    auto shared = std::make_shared<Shared>();
    QThreadPool::globalInstance()->start([path, want, shared]() {
        ASSERT_NOT_GUI_THREAD();
        QImage host = ImageCache::get(path);
        const int hostEdge = ImageCache::longEdge(host);
        bool needLoad = host.isNull();
        if (!needLoad) {
            const QSize cached = ThumtooCache::cachedSize(path);
            if (cached.isValid() && cached.width() > 0 && cached.height() > 0) {
                const int native = ContentXform::longEdge(cached);
                if (hostEdge < native) {
                    needLoad = true;
                }
            } else if (hostEdge > 0 && hostEdge <= ThumtooCache::kBatchOverviewEdge) {
                needLoad = true;
            }
        }
        if (needLoad) {
            const QImage loaded = ImageLoader::load(path);
            if (!loaded.isNull()) {
                host = loaded;
                ImageCache::put(path, loaded);
            }
        }
        QImage display;
        if (host.isNull()) {
            display = {};
        } else if (!SessionAppearance::hasContentAppearance(want)
                   && want.colorAdjust.isIdentity()) {
            display = host;
        } else {
            display = SessionAppearance::materializeDisplay(
                host, want, SessionAppearance::PixelKind::FullSource);
        }
        QMutexLocker lock(&shared->mu);
        shared->out = display;
        shared->done = true;
        shared->cv.wakeOne();
    });
    QMutexLocker lock(&shared->mu);
    while (!shared->done) {
        shared->cv.wait(&shared->mu);
    }
    return shared->out.isNull() ? fallback : shared->out;
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

