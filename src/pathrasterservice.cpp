// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pathrasterservice.h"

#include "imagecache.h"
#include "thumtoocache.h"

#include <QFileInfo>
#include <QtGlobal>

namespace {

bool covers(int have, int need)
{
    if (need <= 0) {
        return have > 0;
    }
    if (have <= 0) {
        return false;
    }
    // ~90% of target — same idea as ImageCache::adequate / coversEdge.
    return have * 10 >= need * 9;
}

} // namespace

PathRasterService::PathRasterService(QObject *parent)
    : QObject(parent)
{
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
    // Snap up within remaining budget, then clamp native again.
    edge = ThumtooCache::ceilLadderEdge(edge);
    if (knownNative.isValid() && knownNative.width() > 0 && knownNative.height() > 0) {
        const int native = qMax(knownNative.width(), knownNative.height());
        if (native > 0) {
            edge = qMin(edge, native);
        }
    }
    return qMax(1, edge);
}

void PathRasterService::ensure(const QString &path, int wantEdge, const QSize &knownNative)
{
    if (path.isEmpty()) {
        return;
    }
    const int want = capWant(wantEdge, knownNative);
    State &st = m_state[path];
    st.epoch = m_epoch;
    // PreferCache may improve when the display target moves past the last
    // shortfall request (e.g. gallery zoom soft→overview→display).
    if (want > st.lastDisplayWant) {
        st.preferGaveUp = false;
        st.displayQueued = false;
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
    return path.isEmpty() ? QImage() : ImageCache::get(path, minLongEdge);
}

int PathRasterService::haveEdge(const QString &path) const
{
    if (path.isEmpty()) {
        return 0;
    }
    const auto it = m_state.constFind(path);
    if (it != m_state.cend() && it->have > 0) {
        return it->have;
    }
    return ImageCache::longEdge(ImageCache::get(path));
}

int PathRasterService::wantEdge(const QString &path) const
{
    const auto it = m_state.constFind(path);
    return it == m_state.cend() ? 0 : it->want;
}

bool PathRasterService::isGaveUp(const QString &path) const
{
    const auto it = m_state.constFind(path);
    return it != m_state.cend() && it->preferGaveUp;
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
        // No active ensure — still cache, no climb.
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

    // PreferCache shortfall vs request — do not spin the same edge.
    if (requestEdge > 0 && got > 0 && got * 10 < requestEdge * 9) {
        st.preferGaveUp = true;
        return;
    }
    if (covers(st.have, st.want)) {
        return;
    }
    if (st.preferGaveUp) {
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
    if (st.preferGaveUp) {
        return;
    }
    if (st.displayQueued && st.lastDisplayWant == displayWant) {
        return;
    }
    // Avoid re-requesting an edge that already shortfall'd.
    if (st.lastDisplayWant == displayWant && st.lastDisplayGot > 0
        && st.lastDisplayGot * 10 < displayWant * 9) {
        st.preferGaveUp = true;
        return;
    }

    st.displayQueued = true;
    st.lastDisplayWant = displayWant;
    ThumtooCache::scheduleProbe(path);
    if (st.have < ThumtooCache::kGalleryLadderEdge) {
        (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
    }
    (void)ThumtooCache::scheduleDisplayPixels(path, displayWant);
}

