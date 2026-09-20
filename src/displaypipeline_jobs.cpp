// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipeline_jobs.h"

#include "imageview.h"
#include "imagecache.h"
#include "displayedgepolicy.h"
#include "softdisplaypolicy.h"
#include "thumtoocache.h"
#include "imageloader.h"
#include "biltoo_logging.h"
#include "biltoo_thread.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>

/** Queue pipeline onImagePreviewLoaded on the GUI thread; no-op if @a guard is gone. */
void queuePreviewLoaded(const QPointer<ImageView> &guard, const QString &path,
                        const QImage &preview, quint64 gen, int role)
{
    if (!guard || preview.isNull()) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, preview, gen, role]() {
        if (!guard) {
            return;
        }
        guard->hostDisplayPipeline().onImagePreviewLoaded(path, preview, gen, role);
    });
}

/** Queue pipeline onImageLoaded on the GUI thread; no-op if @a guard is gone. */
void queueImageLoaded(const QPointer<ImageView> &guard, const QString &path,
                      const QImage &image, quint64 gen, int role)
{
    if (!guard) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, image, gen, role]() {
        if (!guard) {
            return;
        }
        guard->hostDisplayPipeline().onImageLoaded(path, image, gen, role);
    });
}

/** Worker LQIP/soft underlay — see SoftDisplayPolicy::lqipOrCachedSoft. */
QImage loadSoftPreviewPixels(const QString &path, int /*softEdge*/)
{
    return SoftDisplayPolicy::lqipOrCachedSoft(path);
}

/**
 * LQIP seed job only. Never SoftOnly / loadThumbnail / PreferCache soft encode.
 * Gallery product is tiles + LQIP underlay only.
 */
void startSoftPreviewJob(const QPointer<ImageView> &guard, const QString &path,
                         quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp)
{
    biltooLoadDbg("lqipSeed START path=%s gen=%llu",
                  qPrintable(QFileInfo(path).fileName()),
                  static_cast<unsigned long long>(gen));
    Q_UNUSED(softEdge);
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, sessionApp]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            ThumtooCache::scheduleProbe(path);
            QImage preview = loadSoftPreviewPixels(path, 0);
            if (!preview.isNull() && !path.isEmpty()) {
                ImageCache::put(path, preview);
            }
            Q_UNUSED(sessionApp);
            queuePreviewLoaded(guard, path, preview, gen, roleInt);
        },
        2);
}

/**
 * Low-priority pool job: PreferCache / loadThumbnail at a display edge
 * (slideshow quality climb). Schedules PreferCache on miss.
 */
void startDisplayQualityJob(const QPointer<ImageView> &guard, const QString &path,
                            quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp)
{
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, qualityEdge, sessionApp]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
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
            if (!guard) {
                return;
            }
            if (image.isNull()) {
                // PreferCache/Full only via PathRaster on the GUI (contract §1).
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }
            // Soft stand-in still upgrades the view; climb via PathRaster on GUI.
            if (!ImageCache::adequate(image, qualityEdge) && ThumtooCache::isAvailable()) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
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
            queueImageLoaded(guard, path, image, gen, roleInt);
        },
        -1);
}


