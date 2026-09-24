// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// GallerySizeResolve host + applyProbedImageSize. Book/probe: ImageSizeCoordinator.

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


QSize ImageView::contentLayoutSize(const QString &path, SessionImageId sessionId,
                                   bool allowStoreAppearance) const
{
    // Ground truth: native × ItemWorld content ops (same as filmstrip provider).
    QSize native = logicalSizeForPath(path);
    if (!(native.width() > 1 && native.height() > 1)) {
        native = m_size.book().known(path);
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
    if (any && isGalleryMode() && !hostLayout().isFreeForm()) {
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
    if (m_items.isEmpty() || hostGallerySizeResolve().active()) {
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
    if (isGalleryMode() && !hostGallerySizeResolve().active()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
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
