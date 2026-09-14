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

    const QImage cached = ImageCache::get(path);
    const int have = ImageCache::longEdge(cached);
    if (have > 0) {
        st.have = qMax(st.have, have);
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
    const auto it = m_state.constFind(path);
    if (it != m_state.cend() && it->have > 0) {
        return it->have;
    }
    return ImageCache::longEdge(ImageCache::get(path));
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
    if (it == m_state.cend() || it->epoch != m_epoch) {
        return false;
    }
    if (it->softQueued || it->displayQueued || it->fullQueued) {
        return true;
    }
    // FocusFull / post-tile PreferCache still in progress (not yet terminal gave-up).
    if (it->preferGaveUp && !isGaveUp(path)) {
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

    if (requestEdge > 0 && got > 0 && got * 10 < requestEdge * 9) {
        st.preferGaveUp = true;
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

    if (st.have <= 0 && !st.softQueued) {
        st.softQueued = true;
        (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
    }

    const int displayWant = st.want;
    const int overviewCap = ThumtooCache::kBatchOverviewEdge;

    // PreferCache Display unless plateaued for this want.
    if (!st.preferGaveUp) {
        if (st.displayQueued && st.lastDisplayWant == displayWant) {
            return;
        }
        if (st.lastDisplayWant == displayWant && st.lastDisplayGot > 0
            && st.lastDisplayGot * 10 < displayWant * 9) {
            st.preferGaveUp = true;
        } else {
            st.displayQueued = true;
            st.lastDisplayWant = displayWant;
            ThumtooCache::scheduleProbe(path);
            if (st.have < ThumtooCache::kGalleryLadderEdge) {
                (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
            }
            (void)ThumtooCache::scheduleDisplayPixels(path, displayWant);
            return;
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
