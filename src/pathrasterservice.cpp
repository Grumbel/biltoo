// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pathrasterservice.h"

#include "imagecache.h"
#include "thumtoocache.h"

#include <QFileInfo>
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
    // ~90% of target — Met vs request / want (THUMTOO_HOST_CONTRACT.md §3).
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
    if (path.isEmpty()) {
        return;
    }
    const int want = capWant(wantEdge, knownNative);
    State &st = m_state[path];
    st.epoch = m_epoch;
    // Sticky max policy: SoftDisplay may upgrade to EscalateToFull, never reverse.
    if (policy == ClimbPolicy::EscalateToFull) {
        st.policy = ClimbPolicy::EscalateToFull;
    }
    // PreferCache may improve when the display target moves past the last
    // shortfall request (e.g. gallery zoom soft→overview→display).
    if (want > st.lastDisplayWant) {
        st.preferGaveUp = false;
        st.displayQueued = false;
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
    return it != m_state.cend() && it->preferGaveUp;
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
    return it->softQueued || it->displayQueued || it->fullQueued;
}

void PathRasterService::noteDelivery(const QString &path, int requestEdge,
                                     const QImage &image)
{
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

    // PreferCache BestAvailable vs request — do not spin the same Display edge
    // (THUMTOO_HOST_CONTRACT.md §3). EscalateToFull continues via pump → Full.
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
    if (st.preferGaveUp && st.policy != ClimbPolicy::EscalateToFull) {
        return;
    }
    // Full delivery that is still short of want: terminal (no Full spin).
    if (st.fullDone && st.preferGaveUp) {
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

    // Soft band first when we have nothing.
    if (st.have <= 0 && !st.softQueued) {
        st.softQueued = true;
        (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
    }

    const int displayWant = st.want;

    // PreferCache Display — unless already plateaued for this want.
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

    // PreferCache BestAvailable and still short of want.
    if (st.policy != ClimbPolicy::EscalateToFull) {
        return;
    }
    if (st.fullQueued || st.fullDone) {
        return;
    }
    st.fullQueued = true;
    st.fullDone = true; // one-shot: do not re-enter Full if schedule fails mid-flight
    int edge = ImageCache::kDisplayMaxEdge;
    // Prefer known native long edge when available (capped by display max).
    const QSize native = ThumtooCache::cachedSize(path);
    if (native.isValid() && native.width() > 0 && native.height() > 0) {
        edge = qMin(edge, qMax(native.width(), native.height()));
    }
    edge = qMax(edge, displayWant);
    edge = qMin(edge, ImageCache::kDisplayMaxEdge);
#if defined(BILTOO_HAVE_THUMTOO) && defined(THUMTOO_API_FULL_PIXELS) && THUMTOO_API_FULL_PIXELS
    if (!ThumtooCache::scheduleFullPixels(path, edge)) {
        st.fullQueued = false;
        // Already settled / unavailable — plateau stands.
    }
#else
    // No full API: last PreferCache at display max once.
    ThumtooCache::forgetPixelsSettled(path, displayWant);
    st.preferGaveUp = false;
    st.displayQueued = true;
    st.lastDisplayWant = displayWant;
    st.lastDisplayGot = 0;
    (void)ThumtooCache::scheduleDisplayPixels(path, displayWant);
#endif
}
