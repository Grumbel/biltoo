// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pathrasterservice.h"

#include "imagecache.h"
#include "displayedgepolicy.h"
#include "thumtoocache.h"
#include "biltoo_thread.h"

#include <QtGlobal>

PathRasterService::PathRasterService(QObject *parent)
    : QObject(parent)
{
}

RasterClimb::Policy PathRasterService::toSmPolicy(ClimbPolicy p)
{
    return p == ClimbPolicy::EscalateToFull ? RasterClimb::Policy::EscalateToFull
                                            : RasterClimb::Policy::SoftDisplay;
}

int PathRasterService::capWant(int want, const QSize &knownNative)
{
    return DisplayEdgePolicy::cappedDisplayEdge(want, knownNative);
}

RasterClimb::PendingFlags PathRasterService::pendingFlagsFor(const QString &path,
                                                             int want)
{
    RasterClimb::PendingFlags f;
    f.soft = ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge);
    f.display = ThumtooCache::isPixelsPending(path, want > 0 ? want
                                                            : ThumtooCache::kBatchOverviewEdge)
        || ThumtooCache::isPixelsPending(path, ThumtooCache::kBatchOverviewEdge);
    f.full = ThumtooCache::isPixelsPending(path, want > 0 ? want : ImageCache::kDisplayMaxEdge)
        || ThumtooCache::isPixelsPending(path, ImageCache::kDisplayMaxEdge);
    return f;
}

void PathRasterService::ensure(const QString &path, int wantEdge,
                               const QSize &knownNative, ClimbPolicy policy)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("PathRasterService::ensure", 2);
    if (path.isEmpty()) {
        return;
    }
    const int want = capWant(wantEdge, knownNative);
    int native = 0;
    if (knownNative.isValid() && knownNative.width() > 0 && knownNative.height() > 0) {
        native = qMax(knownNative.width(), knownNative.height());
    }
    if (native <= 0) {
        const QSize c = ThumtooCache::cachedSize(path);
        if (c.isValid()) {
            native = qMax(c.width(), c.height());
        }
    }

    Entry &entry = m_state[path];
    entry.epoch = m_epoch;
    RasterClimb::Machine &m = entry.machine;
    const int prevWant = m.state().want;
    m.setWant(want, native, toSmPolicy(policy), ThumtooCache::kGalleryLadderEdge,
              ThumtooCache::kBatchOverviewEdge);
    if (want > prevWant) {
        entry.scheduleCycles = 0;
    }

    if (policy == ClimbPolicy::EscalateToFull) {
        ThumtooCache::forgetPixelsSettled(path, want);
        ThumtooCache::forgetPixelsSettled(path, ImageCache::kDisplayMaxEdge);
    }

    const int cacheEdge = ImageCache::longEdge(ImageCache::get(path));
    if (cacheEdge < m.state().have && m.state().have > 0) {
        ThumtooCache::forgetPixelsSettled(path, ThumtooCache::kGalleryLadderEdge);
        ThumtooCache::forgetPixelsSettled(path, want);
    }
    m.setHaveFromHost(cacheEdge, ThumtooCache::kGalleryLadderEdge);

    if (RasterClimb::covers(m.state().have, m.effectiveNeed())) {
        return;
    }
    pump(path, entry);
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
    const int cacheEdge = ImageCache::longEdge(ImageCache::get(path));
    if (cacheEdge > 0) {
        return cacheEdge;
    }
    return 0;
}

int PathRasterService::wantEdge(const QString &path) const
{
    const auto it = m_state.constFind(path);
    return it != m_state.cend() ? it->machine.state().want : 0;
}

bool PathRasterService::isGaveUp(const QString &path) const
{
    const auto it = m_state.constFind(path);
    if (it == m_state.cend()) {
        return false;
    }
    return it->machine.isGaveUp(ThumtooCache::kBatchOverviewEdge);
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
    it->machine.state().preferGaveUp = false;
    it->machine.state().displayQueued = false;
    it->machine.state().lastDisplayGot = 0;
}

bool PathRasterService::isClimbPending(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    const auto it = m_state.constFind(path);
    const int want = it != m_state.cend() ? it->machine.state().want : 0;
    const RasterClimb::PendingFlags ext = pendingFlagsFor(path, want);
    if (it != m_state.cend() && it->epoch == m_epoch) {
        if (it->machine.isClimbPending(ext, ThumtooCache::kBatchOverviewEdge)) {
            return true;
        }
    }
    if (ext.soft || ext.display || ext.full) {
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
    if (it->epoch != m_epoch) {
        return;
    }
    RasterClimb::Machine &m = it->machine;
    const int prev = m.state().have;
    const int got = ImageCache::longEdge(image);
    m.noteDelivery(requestEdge, got, ThumtooCache::kGalleryLadderEdge);
    if (m.state().have > prev) {
        emit rasterImproved(path, m.state().have);
    }
    if (RasterClimb::covers(m.state().have, m.effectiveNeed())) {
        return;
    }
    pump(path, *it);
}

void PathRasterService::pump(const QString &path, Entry &entry)
{
    if (!ThumtooCache::isAvailable()) {
        return;
    }
    if (entry.epoch != m_epoch) {
        return;
    }
    RasterClimb::Machine &m = entry.machine;
    const int want = m.state().want;
    m.reconcilePending(pendingFlagsFor(path, want));
    m.setHaveFromHost(ImageCache::longEdge(ImageCache::get(path)),
                      ThumtooCache::kGalleryLadderEdge);

    // Soft/Full schedule storm guard — Gallery was re-pumping for seconds.
    constexpr int kMaxScheduleCycles = 16;
    constexpr int kAssertScheduleCycles = 32;
    if (entry.scheduleCycles >= kMaxScheduleCycles) {
        m.state().preferGaveUp = true;
        m.state().softQueued = false;
        m.state().displayQueued = false;
        m.state().fullQueued = false;
        if (entry.scheduleCycles >= kAssertScheduleCycles) {
            Q_ASSERT_X(false, "PathRasterService::pump",
                       "schedule cycle cap — climb stuck in unexpected loop");
        }
        return;
    }

    const RasterClimb::Plan plan =
        m.plan(ThumtooCache::kGalleryLadderEdge, ThumtooCache::kBatchOverviewEdge,
               ImageCache::kDisplayMaxEdge);
    if (plan.forgetSoftSettled) {
        ThumtooCache::forgetPixelsSettled(path, ThumtooCache::kGalleryLadderEdge);
    }
    if (plan.forgetDisplaySettled) {
        ThumtooCache::forgetPixelsSettled(path, plan.displayEdge > 0 ? plan.displayEdge
                                                                     : want);
    }
    if (plan.forgetFullSettled) {
        ThumtooCache::forgetPixelsSettled(path, plan.fullEdge);
        if (m.state().native > 0) {
            ThumtooCache::forgetPixelsSettled(path, m.state().native);
        }
        ThumtooCache::forgetPixelsSettled(path, ImageCache::kDisplayMaxEdge);
        m.state().fullDone = false;
    }

    RasterClimb::Plan accepted;
    accepted.softEdge = plan.softEdge;
    accepted.displayEdge = plan.displayEdge;
    accepted.fullEdge = plan.fullEdge;

    if (plan.scheduleSoft) {
        const int edge = plan.softEdge > 0 ? plan.softEdge
                                           : ThumtooCache::kGalleryLadderEdge;
        if (ThumtooCache::scheduleTileSynthOrPyramid(path, edge)) {
            if (ThumtooCache::hasDurableTilesKnown(path)) {
                accepted.scheduleSoft = true; // TileSynth via PreferCache
            } else {
                accepted.scheduleTiles = true;
            }
        }
    }
    if (plan.scheduleDisplay) {
        ThumtooCache::scheduleProbe(path);
        const int edge = plan.displayEdge > 0 ? plan.displayEdge : want;
        if (ThumtooCache::scheduleTileSynthOrPyramid(path, edge)) {
            if (ThumtooCache::hasDurableTilesKnown(path)) {
                accepted.scheduleDisplay = true;
                accepted.forgetDisplaySettled = plan.forgetDisplaySettled;
            } else {
                accepted.scheduleTiles = true;
            }
        }
    }
    if (plan.scheduleTiles) {
        if (ThumtooCache::scheduleTilePyramid(path)) {
            accepted.scheduleTiles = true;
        }
    }
    if (plan.scheduleFull) {
        if (ThumtooCache::scheduleFullPixels(path, plan.fullEdge)
            || ThumtooCache::isPixelsPending(path, plan.fullEdge)) {
            accepted.scheduleFull = true;
        }
    }
    if (accepted.scheduleSoft || accepted.scheduleDisplay || accepted.scheduleFull
        || accepted.scheduleTiles) {
        ++entry.scheduleCycles;
    }
    m.markScheduled(accepted);
}
