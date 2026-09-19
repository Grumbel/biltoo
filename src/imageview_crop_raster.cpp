// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop Full raster load: Thumtoo scheduleFullPixels or pool ImageLoader::load.
// PathRaster suspend before request so PreferCache cannot race Full.

#include "imageview.h"
#include "croppathraster.h"
#include "cropflash.h"
#include "imagecache.h"
#include "imageloader.h"

#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>

void ImageView::onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen)
{
    if (gen != m_loadGate.generation()) {
        return;
    }
    if (!decoded.isNull()) {
        ImageCache::put(path, decoded);
    }
    maybeUpgradeCropFullRaster(path, decoded);
}

void ImageView::scheduleCropFullRasterFromPool(const QString &path)
{
    const quint64 gen = m_loadGate.generation();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        const QImage decoded = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, decoded, gen]() {
                if (ImageView *const host = guard.data()) {
                    host->onPoolCropFullRasterDecoded(path, decoded, gen);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageView::requestCropFullRaster(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Soft/PreferCache must not race Full encode for the crop subject.
    cancelPathRasterForCrop(path);
    // Prefer thumtoo Full; fall back to pool ImageLoader::load.
    if (CropSession::tryScheduleThumtooFullRaster(path)) {
        return;
    }
    scheduleCropFullRasterFromPool(path);
}


void ImageView::acceptCropFullRasterReady(const QString &path, const QImage &image)
{
    if (!path.isEmpty() && !image.isNull()) {
        ImageCache::put(path, image);
    }
    m_crop.clearAwaitingFull();
    flashCropHud(CropFlash::fullReady());
}

void ImageView::maybeUpgradeCropFullRaster(const QString &path, const QImage &image)
{
    ImageItem *item = m_crop.target();
    if (!item || item->path() != path) {
        if (m_crop.acceptsFullRasterUpgrade(path)) {
            m_crop.clearAwaitingFull();
        }
        return;
    }
    const bool covers = sampleCoversNativeLogical(path, image);
    if (!m_crop.shouldAcceptFullRasterUpgrade(path, image, item, covers)) {
        return;
    }
    // Cache for Apply accuracy; do not reinstall mid-draft (stalls interaction).
    acceptCropFullRasterReady(path, image);
}


