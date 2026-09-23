// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image size book, probes, and GallerySizeResolve host methods.

#include "imageview.h"
#include "contentxform.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "archivepath.h"
#include "pagepath.h"
#include "imageitem.h"
#include "imageloader.h"
#include "display/imagecache.h"
#include "thumtoocache.h"
#include "session/sessionappearance.h"
#include "biltoo_logging.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>


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
        // Do not treat provisional stand-ins as known geometry for Gallery.
        if (isGalleryMode() && m_sizeBook.isProvisional(path)) {
            scheduleImageSizeProbe(path);
            return {};
        }
        return known;
    }
    // Gallery size-first: never install 1000² / square stand-ins into the book.
    // Probe only; ordered pack waits for definitive size or explicit failure.
    if (isGalleryMode()) {
        scheduleImageSizeProbe(path);
        return {};
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
    // File-native size only (map / thumtoo) — not content-oriented layout.
    // Prefer contentLayoutSize(path, sessionId) for placeholder / pack cells.
    // previewHint is display-only; using it for aspect made layout jump when LQIP
    // (wrong aspect / tiny box) was replaced by the size probe.
    Q_UNUSED(previewHint);
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !m_sizeBook.isProvisional(path)) {
        return known;
    }
    if (!path.isEmpty()) {
        scheduleImageSizeProbe(path);
    }
    // Provisional or definitive entry already in the book (layout needs a size).
    const QSize bookSize = m_sizeBook.known(path);
    if (!bookSize.isEmpty() && !m_sizeBook.isProvisional(path)) {
        return bookSize;
    }
    if (isGalleryMode()) {
        scheduleImageSizeProbe(path);
        return {};
    }
    return imageSizeForPath(path);
}

QSize ImageView::contentLayoutSize(const QString &path, SessionImageId sessionId) const
{
    // Ground truth: native × ItemWorld content ops (same as filmstrip provider).
    QSize native = logicalSizeForPath(path);
    if (!(native.width() > 1 && native.height() > 1)) {
        native = m_sizeBook.known(path);
    }
    if (!(native.width() > 1 && native.height() > 1)) {
        // Trigger probe on non-const path via const_cast schedule is awkward;
        // callers still call layoutSizeForPath which schedules. Return native-ish.
        const QSize known = ThumtooCache::cachedSize(path);
        if (known.width() > 1 && known.height() > 1) {
            native = known;
        } else {
            return native; // may be empty/provisional
        }
    }
    WorkspaceItemState want;
    if (sessionId != kInvalidSessionImageId && hasSessionAppearance(sessionId)) {
        want = sessionAppearanceValue(sessionId);
        // Placement/color-only durable row is not content orient — same strip as
        // installDisplayPixels / createItemFromImage (2205–2211).
        want = SessionAppearance::orientAuthorityWant(
            m_itemWorld.hasContentOrient(sessionId), want);
    } else if (sessionId == kInvalidSessionImageId && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            want = *st;
        }
    }
    // Bound: never path XDG. Unbound path rows may still use full XDG including crop.
    if (!SessionAppearance::hasContentAppearance(want) && !path.isEmpty()
        && sessionId == kInvalidSessionImageId) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
            SessionAppearance::applyStoredContentAppearance(&want, stored, false);
        }
    }
    const QSize lay = ContentXform::layoutSize(native, want);
    if (lay.width() > 1 && lay.height() > 1) {
        return lay;
    }
    return native;
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
        const SessionImageId sid = resolveContentEditSessionId(item);
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
        if (!m_gallerySizeResolve.active()) {
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
    // FreeForm: no pack gate.
    // Grid / GridCrop: cells are fixed — stand-in sizes are fine (like filmstrip).
    // Fill modes need every aspect before a stable global pack.
    // Other packaged layouts (masonry, flow, …): progressive ordered prefix
    // while sizes arrive (ensurePlaceholders stops at first unresolved).
    if (mode == LayoutMode::FreeForm) {
        return false;
    }
    if (layoutIsGridFamily(mode)) {
        return false;
    }
    if (layoutNeedsAllSizes(mode)) {
        return true;
    }
    // Masonry / flow / strips: gate on for ordered progressive pack, not a
    // full block until the last size.
    return true;
}





void ImageView::adoptResolvedSize(const QString &path, const QSize &size)
{
    rememberImageSize(path, size);
    applyProbedImageSize(path, size);
}


void ImageView::adoptSizeProbeFailed(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_sizeBook.markFailed(path);
    // Apply error-cell layout so ordered pack can place this row.
    if (const QSize native = m_sizeBook.contains(path)
            ? logicalSizeForPath(path)
            : QSize(256, 256);
        isPositiveSize(native)) {
        applyProbedImageSize(path, native);
    }
    // Surface failure on any live tile for this path.
    for (ImageItem *item : m_items) {
        if (item && item->path() == path) {
            item->setToolTip(tr("Failed to read image size:\n%1").arg(path));
        }
    }
}

void ImageView::onSizeResolvePathSettled(const QString &path)
{
    Q_UNUSED(path);
    if (!isGalleryMode() || !m_gallerySizeResolve.active()) {
        return;
    }
    // Fill modes: one pack at gate complete only (global aspect needed).
    if (layoutNeedsAllSizes(m_layout.currentMode())) {
        return;
    }
    // Ordered progressive pack: create + pack coalesced on the layout debounce
    // timer (ensurePlaceholders once per pack, not per sizeReady).
    if (!m_layout.isFreeForm()) {
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
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
    // Always create remaining tiles when sizes are known. deferPopulate can be
    // cleared by populateGalleryCanvas while the gate is still active (one
    // progressive cell already live → needPlaceholders false → defer false).
    // Gate complete then skipped ensure and left a single-cell Gallery until
    // an explicit relayout.
    m_galleryDecodeBook.setDeferPopulate(false);
    if (isGalleryMode() && !pathOrderIsEmpty()) {
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(true);
            }
        }
        m_gallery.ensurePlaceholders();
    }
    if (isGalleryMode() && !m_items.isEmpty() && !m_layout.isFreeForm()) {
        m_gallery.applyLayout(GalleryPackReason::EnterGallery);
        m_gallery.updateDecodeWindow();
        QTimer::singleShot(0, this, [this]() {
            if (isGalleryMode() && !m_items.isEmpty()) {
                m_gallery.updateDecodeWindow();
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
    m_galleryDecodeBook.setDeferPopulate(false);
    if (isGalleryMode() && m_centreProgress.titleRef().isEmpty()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    // Size-resolve hides live tiles under defer. Cancel without complete must
    // not leave them invisible forever (Gallery "images disappeared").
    if (isGalleryMode()) {
        int hidden = 0;
        for (ImageItem *item : m_items) {
            if (item && !item->isVisible()) {
                item->setVisible(true);
                ++hidden;
            }
        }
        if (m_items.isEmpty() && !pathOrderIsEmpty()) {
            m_gallery.ensurePlaceholders();
            biltooModeDbg("sizeResolve CANCEL ensurePlaceholders items=%d pathOrder=%d",
                          itemCount(), static_cast<int>(currentPackOrder().size()));
        } else if (hidden > 0) {
            biltooModeDbg("sizeResolve CANCEL unhide n=%d items=%d",
                          hidden, itemCount());
            if (viewport()) {
                viewport()->update();
            }
        }
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
    // Size-resolve is a blocking centre panel — FullViewport so detail updates
    // are visible even when placeholders exist (BoundingRect alone can skip it).
    // “Improving previews…” stays BoundingRect (many tiles + frequent updates).
    if (m_items.isEmpty() || m_gallerySizeResolve.active()) {
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
    if (isGalleryMode() && !m_gallerySizeResolve.active()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

// --- Logical size (was imageview_view.cpp) ---

QSize ImageView::logicalSizeForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = m_sizeBook.known(path);
    if (!known.isEmpty()) {
        return known;
    }
    const QSize cached = ThumtooCache::cachedSize(path);
    if (isPositiveSize(cached)) {
        return cached;
    }
    return {};
}

QSize ImageView::ensureLogicalSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !m_sizeBook.isProvisional(path)) {
        return known;
    }
    // imageSizeForPath may schedule a probe and/or install thumtoo cache.
    return imageSizeForPath(path);
}

