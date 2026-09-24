// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaypipeline_jobs.h"

#include "display/displaypipelinecontroller.h"
#include "display/imagecache.h"
#include "display/displayedgepolicy.h"
#include "display/lqipdisplaypolicy.h"
#include "host/thumtoocache.h"
#include "host/imageloader.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>

/** Queue pipeline onImagePreviewLoaded on the GUI thread; no-op if @a life is gone. */
void queuePreviewLoaded(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                        const QString &path, const QImage &preview, quint64 gen, int role)
{
    if (!life || !pipe || preview.isNull()) {
        return;
    }
    QTimer::singleShot(0, life.data(), [life, pipe, path, preview, gen, role]() {
        if (!life || !pipe) {
            return;
        }
        pipe->onImagePreviewLoaded(path, preview, gen, role);
    });
}

/** Queue pipeline onImageLoaded on the GUI thread; no-op if @a life is gone. */
void queueImageLoaded(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                      const QString &path, const QImage &image, quint64 gen, int role)
{
    if (!life || !pipe) {
        return;
    }
    QTimer::singleShot(0, life.data(), [life, pipe, path, image, gen, role]() {
        if (!life || !pipe) {
            return;
        }
        pipe->onImageLoaded(path, image, gen, role);
    });
}

/** Worker LQIP/soft underlay — see LqipDisplayPolicy::lqipOrCachedSample. */
QImage loadSoftPreviewPixels(const QString &path, int /*softEdge*/)
{
    return LqipDisplayPolicy::lqipOrCachedSample(path);
}

/**
 * LQIP seed job only. Never SoftOnly / loadThumbnail / PreferCache soft encode.
 * Gallery product is tiles + LQIP underlay only.
 */
void startSoftPreviewJob(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                         const QString &path, quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp)
{
    biltooLoadDbg("lqipSeed START path=%s gen=%llu",
                  qPrintable(QFileInfo(path).fileName()),
                  static_cast<unsigned long long>(gen));
    Q_UNUSED(softEdge);
    QThreadPool::globalInstance()->start(
        [life, pipe, path, roleInt, gen, sessionApp]() {
            if (!life || !pipe || !pipe->loadGate().accepts(gen)) {
                return;
            }
            ThumtooCache::scheduleProbe(path);
            QImage preview = loadSoftPreviewPixels(path, 0);
            if (!preview.isNull() && !path.isEmpty()) {
                ImageCache::put(path, preview);
            }
            Q_UNUSED(sessionApp);
            queuePreviewLoaded(life, pipe, path, preview, gen, roleInt);
        },
        2);
}

/**
 * Low-priority pool job: PreferCache / loadThumbnail at a display edge
 * (slideshow quality climb). Schedules PreferCache on miss.
 */
void startDisplayQualityJob(const QPointer<QObject> &life, DisplayPipelineController *pipe,
                            const QString &path, quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp)
{
    QThreadPool::globalInstance()->start(
        [life, pipe, path, roleInt, gen, qualityEdge, sessionApp]() {
            if (!life || !pipe || !pipe->loadGate().accepts(gen)) {
                return;
            }
            // Keep any host sample; do not require qualityEdge up front or a
            // smaller soft is discarded and the UI waits on high-res only.
            QImage image = ImageCache::get(path, qualityEdge);
            if (image.isNull()) {
                image = ImageCache::get(path);
            }
            if (!ImageCache::adequate(image, qualityEdge)) {
                // Tiles/TileSynth only when thumtoo is up — never soft PreferCache
                // encode. Without thumtoo, classic shrink decode as last resort.
                if (ThumtooCache::isAvailable()) {
                    const int edge = DisplayEdgePolicy::tileSynthEdge(
                        qualityEdge, ThumtooCache::kBatchOverviewEdge);
                    (void)ThumtooCache::scheduleTileSynthOrPyramid(path, edge);
                } else {
                    const QImage loaded =
                        ImageLoader::loadThumbnail(path, qualityEdge);
                    if (!loaded.isNull()
                        && ImageCache::longEdge(loaded)
                            >= ImageCache::longEdge(image)) {
                        image = loaded;
                    }
                }
            }
            if (!life || !pipe) {
                return;
            }
            if (image.isNull()) {
                // PreferCache/Full only via PathRaster on the GUI (contract §1).
                QMetaObject::invokeMethod(
                    life.data(),
                    [life, pipe, path, qualityEdge]() {
                        if (life && pipe) {
                            pipe->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }
            // Soft stand-in still upgrades the view; climb via PathRaster on GUI.
            if (!ImageCache::adequate(image, qualityEdge) && ThumtooCache::isAvailable()) {
                QMetaObject::invokeMethod(
                    life.data(),
                    [life, pipe, path, qualityEdge]() {
                        if (life && pipe) {
                            pipe->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
            }
            // Host-raw only; GUI install materializes (soft stand-in + async
            // for multi-MP). Do not bake here — same double-bake/cache pollution
            // as the soft job.
            if (!image.isNull() && !path.isEmpty()) {
                ImageCache::put(path, image);
            }
            if (ImageCache::longEdge(image) > qualityEdge) {
                image = image.scaled(qualityEdge, qualityEdge, Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
            }
            Q_UNUSED(sessionApp);
            queueImageLoaded(life, pipe, path, image, gen, roleInt);
        },
        -1);
}
