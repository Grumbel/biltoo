// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pathrasterservice.h"

#include "imagecache.h"
#include "thumtoocache.h"
#include "biltoo_thread.h"

#include <QtGlobal>

PathRasterService::PathRasterService(QObject *parent)
    : QObject(parent)
{
}

bool PathRasterService::covers(int have, int need)
{
    if (need <= 0) {
        return have > 0;
    }
    if (have <= 0) {
        return false;
    }
    return have * 10 >= need * 9;
}

int PathRasterService::capWant(int want, const QSize &knownNative)
{
    int edge = want > 0 ? want : ThumtooCache::kImageLadderEdge;
    edge = qMin(edge, ThumtooCache::kImageLadderEdge);
    if (knownNative.isValid() && knownNative.width() > 0 && knownNative.height() > 0) {
        const int native = qMax(knownNative.width(), knownNative.height());
        if (native > 0) {
            edge = qMin(edge, native);
        }
    }
    edge = ThumtooCache::ceilLadderEdge(edge);
    if (knownNative.isValid() && knownNative.width() > 0 && knownNative.height() > 0) {
        const int native = qMax(knownNative.width(), knownNative.height());
        if (native > 0) {
            edge = qMin(edge, native);
        }
    }
    return qMax(1, edge);
}

void PathRasterService::ensure(const QString &path, int wantEdge,
                               const QSize &knownNative, ClimbPolicy policy)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty()) {
        return;
    }
    const int want = capWant(wantEdge, knownNative);
    State &st = m_state[path];
    st.epoch = m_epoch;
    if (policy == ClimbPolicy::EscalateToFull) {
        st.policy = ClimbPolicy::EscalateToFull;
    }
    if (want > st.lastDisplayWant) {
        st.preferGaveUp = false;
        st.displayQueued = false;
        st.fullDone = false;
        st.fullQueued = false;
        st.postTilePreferAttempts = 0;
        // Keep tilesQueued — pyramid build is still useful for higher want.
    }
    if (policy == ClimbPolicy::EscalateToFull) {
        // Gallery SoftDisplay may have settled Full shortfall at 1024; Image/Workspace
        // must be allowed to request Full / native again.
        ThumtooCache::forgetPixelsSettled(path, want);
        ThumtooCache::forgetPixelsSettled(path, ImageCache::kDisplayMaxEdge);
        st.fullDone = false;
        st.fullQueued = false;
    }
    st.want = qMax(st.want, want);

    // ImageCache is the only paint source. LRU (kMaxEntries) can drop soft
    // samples while PathRaster still remembers a prior delivery edge — then
    // covers(st.have, want) would skip reschedule and Gallery stays blank
    // with schedulePixels SKIP (settled). Always re-sync have from the host
    // cache; never claim pixels that are no longer resident.
    const int cacheEdge = ImageCache::longEdge(ImageCache::get(path));
    if (cacheEdge < st.have) {
        // Evicted (or never installed). Drop settled soft/display so SoftOnly
        // / PreferCache may run again for this path.
        if (st.have > 0) {
            ThumtooCache::forgetPixelsSettled(path, ThumtooCache::kGalleryLadderEdge);
            ThumtooCache::forgetPixelsSettled(path, st.want > 0 ? st.want : want);
            st.softQueued = false;
            st.displayQueued = false;
            st.preferGaveUp = false;
            st.lastDisplayGot = 0;
        }
        st.have = cacheEdge;
    } else if (cacheEdge > st.have) {
        st.have = cacheEdge;
    }
    if (covers(st.have, st.want)) {
        st.preferGaveUp = false;
        st.displayQueued = false;
        return;
    }
    pump(path, st);
}

void PathRasterService::cancel(const QString &path)
{
    m_state.remove(path);
}

void PathRasterService::invalidateAll()
{
    ++m_epoch;
    m_state.clear();
}

QImage PathRasterService::best(const QString &path, int minLongEdge) const
{
    return ImageCache::get(path, minLongEdge);
}

int PathRasterService::haveEdge(const QString &path) const
{
    // Prefer live ImageCache edge; PathRaster have can lag after LRU eviction.
    const int cacheEdge = ImageCache::longEdge(ImageCache::get(path));
    if (cacheEdge > 0) {
        return cacheEdge;
    }
    const auto it = m_state.constFind(path);
    if (it != m_state.cend() && it->have > 0) {
        // Stale bookkeeping only — callers that need paint must re-ensure.
        return 0;
    }
    return 0;
}

int PathRasterService::wantEdge(const QString &path) const
{
    const auto it = m_state.constFind(path);
    return it != m_state.cend() ? it->want : 0;
}

bool PathRasterService::isGaveUp(const QString &path) const
{
    const auto it = m_state.constFind(path);
    if (it == m_state.cend() || !it->preferGaveUp) {
        return false;
    }
    // Not terminal until Full has been attempted when want > overview.
    if (it->want > ThumtooCache::kBatchOverviewEdge && !it->fullDone) {
        return false;
    }
    if (it->want <= ThumtooCache::kBatchOverviewEdge
        && it->postTilePreferAttempts < 2) {
        return false;
    }
    return true;
}

void PathRasterService::clearPreferGaveUp(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    auto it = m_state.find(path);
    if (it == m_state.end()) {
        return;
    }
    it->preferGaveUp = false;
    it->displayQueued = false;
    it->lastDisplayGot = 0;
}

bool PathRasterService::isClimbPending(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    const auto it = m_state.constFind(path);
    if (it != m_state.cend() && it->epoch == m_epoch) {
        if (it->softQueued || it->displayQueued || it->fullQueued) {
            return true;
        }
        // FocusFull / post-tile PreferCache still in progress (not yet terminal gave-up).
        if (it->preferGaveUp && !isGaveUp(path)) {
            return true;
        }
    }
    // softQueued can be cleared on PreferCache LQIP delivery while SoftOnly is
    // still in the host pixel queue — treat real inflight as climb pending so
    // DisplayQuality does not call StuckWeak and assert.
    if (ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge)
        || ThumtooCache::isPixelsPending(path, ThumtooCache::kBatchOverviewEdge)) {
        return true;
    }
    return false;
}

void PathRasterService::noteDelivery(const QString &path, int requestEdge,
                                     const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty()) {
        return;
    }
    if (!image.isNull()) {
        ImageCache::put(path, image);
    }
    auto it = m_state.find(path);
    if (it == m_state.end()) {
        if (!image.isNull()) {
            emit rasterImproved(path, ImageCache::longEdge(image));
        }
        return;
    }
    State &st = it.value();
    if (st.epoch != m_epoch) {
        return;
    }
    const int got = ImageCache::longEdge(image);
    if (got > 0) {
        const int prev = st.have;
        st.have = qMax(st.have, got);
        if (requestEdge > 0) {
            st.lastDisplayGot = qMax(st.lastDisplayGot, got);
        }
        if (st.have > prev) {
            emit rasterImproved(path, st.have);
        }
    }
    st.displayQueued = false;
    st.softQueued = false;
    st.fullQueued = false;

    // PreferCache shortfall: durable soft (≤512) is never a win when want is
    // past soft max — otherwise Gallery zoom stays on SoftOnly forever while
    // PreferCache keeps returning the same 512 sample.
    constexpr int kMinPreferPlateauEdge = 96;
    if (requestEdge > 0 && got > 0 && got * 10 < requestEdge * 9) {
        if (got >= kMinPreferPlateauEdge
            || (st.want > ThumtooCache::kGalleryLadderEdge
                && got <= ThumtooCache::kGalleryLadderEdge)) {
            st.preferGaveUp = true;
        }
        if (covers(st.have, st.want)) {
            return;
        }
        pump(path, st);
        return;
    }
    if (covers(st.have, st.want)) {
        return;
    }
    // Full landed but still short: PreferCache retry (TileSynth after FocusFull).
    if (st.fullDone && st.tilesQueued && st.postTilePreferAttempts < 2) {
        st.preferGaveUp = false;
        st.lastDisplayGot = 0;
        ThumtooCache::forgetPixelsSettled(path, st.want);
        pump(path, st);
        return;
    }
    if (st.preferGaveUp && st.policy != ClimbPolicy::EscalateToFull
        && !(st.tilesQueued && st.postTilePreferAttempts < 2)) {
        return;
    }
    pump(path, st);
}

void PathRasterService::pump(const QString &path, State &st)
{
    if (!ThumtooCache::isAvailable()) {
        return;
    }
    if (st.epoch != m_epoch) {
        return;
    }
    if (covers(st.have, st.want)) {
        return;
    }

    // SoftOnly / PreferCache can finish (or fail URI resolve) without noteDelivery
    // clearing queue flags. A sticky softQueued blocks reschedule; a sticky
    // displayQueued hits the early return below and deadlocks the path on LQIP.
    if (st.softQueued
        && !ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge)) {
        st.softQueued = false;
    }
    if (st.displayQueued) {
        const int dw = st.lastDisplayWant > 0 ? st.lastDisplayWant : st.want;
        if (!ThumtooCache::isPixelsPending(path, dw)) {
            st.displayQueued = false;
        }
    }

    // LQIP / tiny stand-ins set have > 0 but must not skip the soft ladder.
    // Schedule SoftOnly until we reach durable soft max (kGalleryLadderEdge).
    // Only mark softQueued when a job was actually accepted — otherwise SKIP
    // (true inflight) leaves climb pending forever with no delivery.
    if (!covers(st.have, ThumtooCache::kGalleryLadderEdge) && !st.softQueued) {
        if (ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge)) {
            st.softQueued = true;
        }
    }

    const int displayWant = st.want;
    const int overviewCap = ThumtooCache::kBatchOverviewEdge;

    // PreferCache Display unless plateaued for this want.
    // When soft is already covered and want exceeds overview, PreferCache is
    // only an intermediate — do not return early; fall through to FocusFull/Full
    // so Gallery zoom is not stuck at 512 waiting for another soft PreferCache.
    const bool softCovered = covers(st.have, ThumtooCache::kGalleryLadderEdge)
        || st.have >= ThumtooCache::kGalleryLadderEdge;
    if (!st.preferGaveUp) {
        if (st.displayQueued && st.lastDisplayWant == displayWant
            && !(softCovered && displayWant > overviewCap)) {
            return;
        }
        if (st.lastDisplayWant == displayWant && st.lastDisplayGot > 0
            && st.lastDisplayGot * 10 < displayWant * 9) {
            st.preferGaveUp = true;
        } else if (!(st.displayQueued && st.lastDisplayWant == displayWant)) {
            st.lastDisplayWant = displayWant;
            ThumtooCache::scheduleProbe(path);
            if (st.have < ThumtooCache::kGalleryLadderEdge && !st.softQueued) {
                if (ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge)) {
                    st.softQueued = true;
                }
            }
            if (ThumtooCache::scheduleDisplayPixels(path, displayWant)) {
                st.displayQueued = true;
            }
            if (!(softCovered && displayWant > overviewCap)) {
                return;
            }
        }
    }

    // PreferCache above soft max is overview-clamped to ~1024 in thumtoo.
    // Contract: FocusFull (tile pyramid) + Full, then PreferCache retry for
    // TileSynth — otherwise slideshow/Image stay stuck at the 1024 plateau.
    if (displayWant > overviewCap) {
        if (!st.tilesQueued) {
            if (ThumtooCache::scheduleTilePyramid(path)) {
                st.tilesQueued = true;
            }
        }
        if (!st.fullQueued && !st.fullDone) {
            int edge = ImageCache::kDisplayMaxEdge;
            const QSize native = ThumtooCache::cachedSize(path);
            if (native.isValid() && native.width() > 0 && native.height() > 0) {
                edge = qMin(edge, qMax(native.width(), native.height()));
            }
            edge = qMax(edge, displayWant);
            edge = qMin(edge, ImageCache::kDisplayMaxEdge);
            if (ThumtooCache::scheduleFullPixels(path, edge)) {
                st.fullQueued = true;
                st.fullDone = true;
            }
            // else: concurrent Full limit — retry on next ensure/pump
            return;
        }
        // Full already attempted and still short of want: PreferCache again
        // (TileSynth after FocusFull) up to two times.
        if (st.fullDone && st.postTilePreferAttempts < 2 && !st.displayQueued) {
            ++st.postTilePreferAttempts;
            st.preferGaveUp = false;
            st.lastDisplayGot = 0;
            ThumtooCache::forgetPixelsSettled(path, displayWant);
            st.displayQueued = true;
            st.lastDisplayWant = displayWant;
            (void)ThumtooCache::scheduleDisplayPixels(path, displayWant);
        }
        return;
    }

    // want ≤ overview: PreferCache retries after a short plateau are enough.
    if (st.postTilePreferAttempts < 2 && !st.displayQueued) {
        ++st.postTilePreferAttempts;
        st.preferGaveUp = false;
        st.lastDisplayGot = 0;
        ThumtooCache::forgetPixelsSettled(path, displayWant);
        st.displayQueued = true;
        st.lastDisplayWant = displayWant;
        (void)ThumtooCache::scheduleDisplayPixels(path, displayWant);
    }
}
