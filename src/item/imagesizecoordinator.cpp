// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image size book + probe scheduling (owned by ImageSizeCoordinator).
// GallerySizeResolve host / applyProbedImageSize remain on ImageView.

#include "item/imagesizecoordinator.h"
#include "imageview.h"
#include "content/contentxform.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "imageitem.h"
#include "host/imageloader.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "session/sessionappearance.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>



ImageSizeCoordinator::ImageSizeCoordinator(ImageView *view)
    : m_view(view)
{
}

void ImageSizeCoordinator::rememberImageSize(const QString &path, const QSize &size)
{
    GUI_BUDGET("ImageSizeCoordinator::rememberImageSize");
    // HARD RULE lives in ImageSizeBook::noteDefinitive (SIZE.md identity).
    if (!m_book.noteDefinitive(path, size)) {
        return;
    }
    ThumtooCache::noteCachedSize(path, size);
}

void ImageSizeCoordinator::rememberSizeFromDecode(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    // Durable index is authoritative when present (no revalidate on GUI).
    if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        isPositiveSize(cached)) {
        rememberImageSize(path, cached);
        return;
    }
    // Already have a definitive logical size — leave samples alone.
    if (m_book.hasDefinitive(path)) {
        return;
    }
    // Ladder / soft samples are not native identity. Probe for the real size;
    // do not write sample dimensions into the logical map.
    const int edge = qMax(image.width(), image.height());
    if (edge <= ThumtooCache::kImageLadderEdge) {
        scheduleImageSizeProbe(path);
        return;
    }
    // Larger than the image ladder max — treat as full native decode.
    rememberImageSize(path, image.size());
}


QSize ImageSizeCoordinator::imageSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return ImageSizeBook::standInNeutral();
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known)) {
        if (!m_book.contains(path)) {
            rememberImageSize(path, known); // install thumtoo hit into map
        }
        // Do not treat provisional stand-ins as known geometry for Gallery.
        if (m_view->isGalleryMode() && m_book.isProvisional(path)) {
            scheduleImageSizeProbe(path);
            return {};
        }
        return known;
    }
    // Gallery size-first: never install 1000² / square stand-ins into the book.
    // Probe only; ordered pack waits for definitive size or explicit failure.
    if (m_view->isGalleryMode()) {
        scheduleImageSizeProbe(path);
        return {};
    }
    // Archives / multipage / embedded PDF: async probe; neutral stand-in.
    scheduleImageSizeProbe(path);
    const bool compound = ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path);
    // Square is only a last resort for compound refs until soft aspect or probe.
    const QSize standIn = ImageSizeBook::standInForCompoundPath(compound);
    m_book.markProvisional(path, standIn);
    return standIn;
}

QSize ImageSizeCoordinator::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    // File-native size only (map / thumtoo) — not content-oriented layout.
    // Prefer contentLayoutSize(path, sessionId) for placeholder / pack cells.
    // previewHint is display-only; using it for aspect made layout jump when LQIP
    // (wrong aspect / tiny box) was replaced by the size probe.
    Q_UNUSED(previewHint);
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !m_book.isProvisional(path)) {
        return known;
    }
    if (!path.isEmpty()) {
        scheduleImageSizeProbe(path);
    }
    // Provisional or definitive entry already in the book (layout needs a size).
    const QSize bookSize = m_book.known(path);
    if (!bookSize.isEmpty() && !m_book.isProvisional(path)) {
        return bookSize;
    }
    if (m_view->isGalleryMode()) {
        scheduleImageSizeProbe(path);
        return {};
    }
    return imageSizeForPath(path);
}

void ImageSizeCoordinator::primeGalleryGeometryFromCache(const QStringList &paths)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("ImageSizeCoordinator::primeGalleryGeometryFromCache");
    // Expect warmSessionOpenMemos to have filled size memo + ImageCache LQIP.
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (!m_book.hasDefinitive(path)) {
            if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
                isPositiveSize(cached)) {
                rememberImageSize(path, cached);
            }
        }
        // LQIP placeholder already in ImageCache from warmSessionOpenMemos.
        Q_UNUSED(ImageCache::has(path));
    }
}

void ImageSizeCoordinator::scheduleImageSizeProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_book.isProbeScheduled(path)) {
        return;
    }
    // Definitive size already known — provisional stand-ins must still probe.
    if (m_book.hasDefinitive(path)) {
        return;
    }
    // Never isUnsupported on the GUI (Store get_meta). scheduleProbe / worker
    // skips unsupported locators.
    // Prefer thumtoo: scheduleProbe only; Bridge::sizeReady applies the size.
    // No thread-pool Qt/vips/extract size read when the durable client is up.
    if (ThumtooCache::isAvailable()) {
        m_book.markProbeScheduled(path);
        ThumtooCache::scheduleProbe(path);
        return;
    }
    // Builds without thumtoo: native size probe on a worker.
    m_book.markProbeScheduled(path);
    const QPointer<ImageView> guard(m_view);
    QThreadPool::globalInstance()->start([guard, path]() {
        QSize s = ImageLoader::probeSize(path);
        if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
            s = ImageSizeBook::standInNeutral();
        }
        if (!guard) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        QMetaObject::invokeMethod(view, [guard, path, s]() {
            ImageView *const host = guard.data();
            if (!host) {
                return;
            }
            host->hostSizeBook().clearProbeScheduled(path);
            // Prefer a size already learned from a full decode — not provisional.
            if (host->hostSizeBook().hasDefinitive(path)) {
                return;
            }
            host->rememberImageSize(path, s);
            host->applyProbedImageSize(path, s);
        }, Qt::QueuedConnection);
    });
}

QSize ImageSizeCoordinator::logicalSizeForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = m_book.known(path);
    if (!known.isEmpty()) {
        return known;
    }
    const QSize cached = ThumtooCache::cachedSize(path);
    if (isPositiveSize(cached)) {
        return cached;
    }
    return {};
}

QSize ImageSizeCoordinator::ensureLogicalSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !m_book.isProvisional(path)) {
        return known;
    }
    // imageSizeForPath may schedule a probe and/or install thumtoo cache.
    return imageSizeForPath(path);
}

