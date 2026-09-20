// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image size book, probes, and GallerySizeResolve host methods.

#include "imageview.h"
#include "gallerysoftsm.h"
#include "displayquality.h"
#include "archivepath.h"
#include "pagepath.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>

QSize ImageView::probeImageSize(const QString &path) const
{
    // Never open the source on the GUI thread (USB/NFS freeze). Cache-only or
    // neutral stand-in; scheduleImageSizeProbe / sizeReady supply the real size.
    if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        cached.isValid()) {
        return cached;
    }
    if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path)) {
        return ImageSizeBook::standInSquare();
    }
    return ImageSizeBook::standInNeutral();
}

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    // HARD RULE lives in ImageSizeBook::noteDefinitive (SIZE.md identity).
    if (!m_sizeBook.noteDefinitive(path, size)) {
        return;
    }
    ThumtooCache::noteCachedSize(path, size);
}

void ImageView::rememberSizeFromDecode(const QString &path, const QImage &image)
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
    if (m_sizeBook.hasDefinitive(path)) {
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

bool ImageView::isProvisionalImageSize(const QString &path) const
{
    return m_sizeBook.isProvisional(path);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return ImageSizeBook::standInNeutral();
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known)) {
        if (!m_sizeBook.contains(path)) {
            rememberImageSize(path, known); // install thumtoo hit into map
        }
        return known;
    }
    // Archives / multipage / embedded PDF: async probe; neutral stand-in.
    scheduleImageSizeProbe(path);
    const bool compound = ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path);
    // Square is only a last resort for compound refs until soft aspect or probe.
    const QSize standIn = ImageSizeBook::standInForCompoundPath(compound);
    m_sizeBook.markProvisional(path, standIn);
    return standIn;
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    // Prefer definitive logical size (map / thumtoo) — never soft/LQIP sample dims.
    // previewHint is display-only; using it for aspect made layout jump when LQIP
    // (wrong aspect / tiny box) was replaced by the size probe.
    Q_UNUSED(previewHint);
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !isProvisionalImageSize(path)) {
        return known;
    }
    if (!path.isEmpty()) {
        scheduleImageSizeProbe(path);
    }
    // Provisional or definitive entry already in the book (layout needs a size).
    const QSize bookSize = m_sizeBook.known(path);
    if (!bookSize.isEmpty()) {
        return bookSize;
    }
    return imageSizeForPath(path);
}


void ImageView::primeGalleryGeometryFromCache(const QStringList &paths)
{
    // Expect warmSessionOpenMemos to have filled size memo + ImageCache LQIP.
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (!m_sizeBook.hasDefinitive(path)) {
            if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
                isPositiveSize(cached)) {
                rememberImageSize(path, cached);
            }
        }
        // LQIP placeholder already in ImageCache from warmSessionOpenMemos.
        Q_UNUSED(ImageCache::has(path));
    }
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_sizeBook.isProbeScheduled(path)) {
        return;
    }
    // Definitive size already known — provisional stand-ins must still probe.
    if (m_sizeBook.hasDefinitive(path)) {
        return;
    }
    // Never isUnsupported on the GUI (Store get_meta). scheduleProbe / worker
    // skips unsupported locators.
    // Prefer thumtoo: scheduleProbe only; Bridge::sizeReady applies the size.
    // No thread-pool Qt/vips/extract size read when the durable client is up.
    if (ThumtooCache::isAvailable()) {
        m_sizeBook.markProbeScheduled(path);
        ThumtooCache::scheduleProbe(path);
        return;
    }
    // Builds without thumtoo: native size probe on a worker.
    m_sizeBook.markProbeScheduled(path);
    const QPointer<ImageView> guard(this);
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
            host->m_sizeBook.clearProbeScheduled(path);
            // Prefer a size already learned from a full decode — not provisional.
            if (host->m_sizeBook.hasDefinitive(path)) {
                return;
            }
            host->rememberImageSize(path, s);
            host->applyProbedImageSize(path, s);
        }, Qt::QueuedConnection);
    });
}

void ImageView::applyProbedImageSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || !size.isValid()) {
        return;
    }
    bool any = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        // Probe is authoritative file-native size. Layout = ContentXform
        // (turns + crop), not a simple axis swap.
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }
        WorkspaceItemState want = m_displayPipeline.wantAppearanceForItem(item, sid);
        QSize layoutSize = ContentXform::layoutSize(size, want);
        if (!(layoutSize.width() > 1 && layoutSize.height() > 1)) {
            layoutSize = size;
        }
        const QSize cur = item->imageSize();
        if (cur == layoutSize) {
            continue;
        }
        item->setIntrinsicSize(layoutSize);
        any = true;
        if (isImageMode() && item == targetItem()) {
            preserveImageViewOnLogicalSizeChange(item, cur, layoutSize);
        }
    }
    if (any && isGalleryMode() && !m_layout.isFreeForm()) {
        // While the open-time size-resolve gate is active, pack once when all
        // probes settle — not on every sizeReady (avoids thrash + tiny cells).
        if (!gallerySizeResolveActive()) {
            requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
        }
    } else if (any && viewport()) {
        viewport()->update();
    }
    // Slideshow paints from path→logical, not the underlay item. When the probe
    // lands for a phase path, refresh dest aspect (and atlas if needed).
    if (m_slideshow.hud().isProgressActive()
        && m_slideshow.phase().isPhasePath(path)) {
        if (m_slideshow.phase().isFromPath(path) && m_slideshow.phase().hasFromImage()) {
            m_slideshow.requestDwellAtlasRebuild();
        }
        if (m_slideshow.phase().isToPath(path) && m_slideshow.phase().hasToImage()) {
            m_slideshow.requestToPhaseAtlasRebuild();
        }
        if (viewport()) {
            viewport()->update();
        }
    }
}


bool ImageView::layoutDefersPopulateUntilSizes(LayoutMode mode)
{
    // Packaged Gallery layouts need definitive aspects before the first pack.
    // Provisional 1000² cells + sizeReady re-pack was the cold-open "glitch"
    // (items flash in random positions before layout settles).
    switch (mode) {
    case LayoutMode::FreeForm:
        return false;
    default:
        return true;
    }
}

bool ImageView::startGallerySizeResolveIfNeeded(const QStringList &paths)
{
    return m_gallerySizeResolve.startIfNeeded(paths);
}

void ImageView::noteGallerySizeProbeSettled(const QString &path)
{
    m_gallerySizeResolve.noteProbeSettled(path);
}

void ImageView::cancelGallerySizeResolve()
{
    m_gallerySizeResolve.cancel();
}

bool ImageView::hasDefinitiveHostSize(const QString &path) const
{
    return m_sizeBook.hasDefinitive(path);
}

void ImageView::adoptResolvedSize(const QString &path, const QSize &size)
{
    rememberImageSize(path, size);
    applyProbedImageSize(path, size);
}

void ImageView::scheduleSizeProbe(const QString &path)
{
    scheduleImageSizeProbe(path);
}

QStringList ImageView::sizeResolvePathOrder() const
{
    const PackOrderView pack = currentPackOrder();
    return pack.paths();
}

bool ImageView::sizeResolveLayoutDefersPopulate() const
{
    return layoutDefersPopulateUntilSizes(m_layout.currentMode());
}

void ImageView::setSizeResolveProgress(const QString &title, const QString &detail)
{
    setCentreProgress(title, detail);
}

void ImageView::clearSizeResolveProgress()
{
    if (m_centreProgress.matchesTitlePrefix(tr("Resolving sizes"))) {
        clearCentreProgress();
    }
}

void ImageView::onSizeResolveGateComplete()
{
    clearCentreProgress();
    if (isGalleryMode()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    // Create tiles only now — sizes are definitive (or timed out with stand-in).
    if (m_gallerySoftBook.isDeferPopulate()) {
        m_gallerySoftBook.setDeferPopulate(false);
        ensureGalleryPlaceholders();
    } else {
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(true);
                if (!isProvisionalImageSize(item->path())) {
                    const QSize sz = layoutSizeForPath(item->path());
                    if (isPositiveSize(sz)) {
                        item->setIntrinsicSize(sz);
                    }
                }
            }
        }
        // Safety: size-resolve used to refuse createPlaceholder → empty canvas.
        if (isGalleryMode() && m_items.isEmpty() && !pathOrderIsEmpty()) {
            ensureGalleryPlaceholders();
        }
    }
    if (isGalleryMode() && !m_items.isEmpty() && !m_layout.isFreeForm()) {
        applyLayout(GalleryPackReason::EnterGallery);
        updateGalleryDecodeWindow();
        QTimer::singleShot(0, this, [this]() {
            if (isGalleryMode() && !m_items.isEmpty()) {
                updateGalleryDecodeWindow();
            }
        });
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
    emit gallerySizeResolveFinished();
}

void ImageView::onSizeResolveGateCancelled()
{
    m_gallerySoftBook.setDeferPopulate(false);
    if (isGalleryMode() && m_centreProgress.titleRef().isEmpty()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    clearSizeResolveProgress();
    emit gallerySizeResolveFinished();
}

void ImageView::setCentreProgress(const QString &title, const QString &detail)
{
    if (title.isEmpty()) {
        clearCentreProgress();
        return;
    }
    if (!m_centreProgress.set(title, detail)) {
        return;
    }
    // Empty scene needs FullViewportUpdate or the centre panel never paints.
    // Gallery with tiles must keep BoundingRectViewportUpdate — FullViewport
    // during “Improving previews…” re-painted every item every frame and
    // undid ItemCoordinateCache scroll savings.
    if (m_items.isEmpty()) {
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearCentreProgress()
{
    if (!m_centreProgress.active()) {
        return;
    }
    m_centreProgress.clear();
    if (isGalleryMode() && !gallerySizeResolveActive()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

