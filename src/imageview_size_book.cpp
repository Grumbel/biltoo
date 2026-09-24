// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image size book, probes, and GallerySizeResolve host methods.

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


void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    GUI_BUDGET("ImageView::rememberImageSize");
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

QSize ImageView::contentLayoutSize(const QString &path, SessionImageId sessionId,
                                   bool allowStoreAppearance) const
{
    // Ground truth: native × ItemWorld content ops (same as filmstrip provider).
    QSize native = logicalSizeForPath(path);
    if (!(native.width() > 1 && native.height() > 1)) {
        native = m_sizeBook.known(path);
    }
    if (!(native.width() > 1 && native.height() > 1)) {
        // Trigger probe on non-const path via const_cast schedule is awkward;
        // callers still call layoutSizeForPath which schedules. Return native-ish.
        // Bulk virtual plan passes allowStoreAppearance=false — skip Store size too.
        if (allowStoreAppearance) {
            const QSize known = ThumtooCache::cachedSize(path);
            if (known.width() > 1 && known.height() > 1) {
                native = known;
            } else {
                return native; // may be empty/provisional
            }
        } else {
            return native;
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
    // Virtual plan / bulk open: never Store loadContentAppearance (GUI freeze).
    if (allowStoreAppearance && !SessionAppearance::hasContentAppearance(want)
        && !path.isEmpty() && sessionId == kInvalidSessionImageId) {
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
    ASSERT_GUI_THREAD();
    GUI_BUDGET("ImageView::primeGalleryGeometryFromCache");
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
    GUI_BUDGET("ImageView::applyProbedImageSize");
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
        WorkspaceItemState want = m_displayPipeline->wantAppearanceForItem(item, sid);
        QSize layoutSize = ContentXform::layoutSize(size, want);
        if (!(layoutSize.width() > 1 && layoutSize.height() > 1)) {
            layoutSize = size;
        }
        const QSize cur = item->imageSize();
        if (cur == layoutSize) {
            continue;
        }
        m_displayPipeline->hostSetIntrinsicSize(item, layoutSize);
        any = true;
        // Drop stale pack clip: square (or wrong-aspect) galleryCellSize was
        // cropping the updated contentRect until the next pack.
        if (isGalleryMode() && !item->galleryCellSize().isEmpty()
            && layoutSize.width() > 0 && layoutSize.height() > 0) {
            const QSizeF cell = item->galleryCellSize();
            if (cell.height() > 1e-3 && cell.width() > 1e-3) {
                const qreal cellAr = cell.width() / cell.height();
                const qreal layAr =
                    qreal(layoutSize.width()) / qreal(layoutSize.height());
                if (qAbs(cellAr - layAr) > 0.04) {
                    item->setGalleryCellSize({});
                }
            }
        }
        if (isImageMode() && item == targetItem()) {
            preserveImageViewOnLogicalSizeChange(item, cur, layoutSize);
        }
    }
    if (any && isGalleryMode() && !m_layout.isFreeForm()) {
        // ContentChange is allowed during the size gate (prefix pack). Debounced
        // so sizeReady chunks do not reflow every path.
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
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
    // Every other Gallery layout: sizes first (gate active). Tiles must not
    // compete with ProbeSize. Grid no longer packs on stand-ins while the
    // session is still resolving — that showed real pixels before sizes landed.
    if (mode == LayoutMode::FreeForm) {
        return false;
    }
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
    // Full pack still only on gate complete (avoids continuous reflow).
    // Coalesced plan/window refresh so virtual cells can show SizeReply underlay
    // as sizes land without waiting for the whole session.
    m_gallery.scheduleSizeGatePlanRefresh();
}






void ImageView::clearSizeResolveProgress()
{
    if (m_centreProgress.matchesTitlePrefix(tr("Resolving sizes"))) {
        clearCentreProgress();
    }
}

void ImageView::onSizeResolveGateComplete()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("ImageView::onSizeResolveGateComplete");
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
        // Stay hidden while placeholders are created (chunked). Pack once, then show.
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(false);
            }
        }
        const bool more = m_gallery.ensurePlaceholders();
        if (!more && !m_items.isEmpty() && !m_layout.isFreeForm()) {
            m_gallery.applyLayout(GalleryPackReason::EnterGallery);
            for (ImageItem *item : m_items) {
                if (item) {
                    item->setVisible(true);
                }
            }
            m_gallery.updateDecodeWindow();
            QTimer::singleShot(0, this, [this]() {
                if (isGalleryMode() && !m_items.isEmpty()) {
                    m_gallery.updateDecodeWindow();
                }
            });
        }
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
    // Empty scene needs FullViewportUpdate or the progress panel never paints.
    // Size-resolve is interactive (corner HUD) but still needs reliable redraws
    // while placeholders exist — BoundingRect alone can skip the HUD region.
    // “Improving previews…” keeps BoundingRect (many tiles + frequent updates).
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

