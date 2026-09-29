// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/backgroundworklog.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>

namespace BackgroundWorkLog {
namespace {

constexpr int kRing = 96;

std::atomic<std::uint64_t> g_poolStarts{0};
std::atomic<std::uint64_t> g_probe{0};
std::atomic<std::uint64_t> g_reval{0};
std::atomic<std::uint64_t> g_pixels{0};
std::atomic<std::uint64_t> g_tile{0};
std::atomic<std::uint64_t> g_gallery{0};
std::atomic<std::uint64_t> g_tileLod{0};

QMutex g_mu;
QVector<Event> g_ring;
Counters g_lastSnapshot{};

Counters readTotals()
{
    Counters c;
    c.poolStarts = g_poolStarts.load(std::memory_order_relaxed);
    c.scheduleProbe = g_probe.load(std::memory_order_relaxed);
    c.scheduleRevalidate = g_reval.load(std::memory_order_relaxed);
    c.schedulePixels = g_pixels.load(std::memory_order_relaxed);
    c.scheduleTile = g_tile.load(std::memory_order_relaxed);
    c.scheduleGalleryDecode = g_gallery.load(std::memory_order_relaxed);
    c.tileLodTicks = g_tileLod.load(std::memory_order_relaxed);
    return c;
}

void pushEvent(const char *kind, const QString &detail)
{
    Event e;
    e.msEpoch = QDateTime::currentMSecsSinceEpoch();
    e.kind = QString::fromUtf8(kind);
    e.detail = detail;
    QMutexLocker lock(&g_mu);
    if (g_ring.size() >= kRing) {
        g_ring.remove(0, g_ring.size() - kRing + 1);
    }
    g_ring.append(e);
}

} // namespace

void note(const char *kind, const QString &detail)
{
    if (!kind || !kind[0]) {
        return;
    }
    pushEvent(kind, detail);
}

void noteProbe()
{
    g_probe.fetch_add(1, std::memory_order_relaxed);
    pushEvent("probe", QString());
}

void noteRevalidate()
{
    g_reval.fetch_add(1, std::memory_order_relaxed);
    pushEvent("revalidate", QString());
}

void notePixels()
{
    g_pixels.fetch_add(1, std::memory_order_relaxed);
    pushEvent("pixels", QString());
}

void noteTile()
{
    g_tile.fetch_add(1, std::memory_order_relaxed);
    pushEvent("tile", QString());
}

void noteGalleryDecode()
{
    g_gallery.fetch_add(1, std::memory_order_relaxed);
    pushEvent("galleryDecode", QString());
}

void noteTileLodTick()
{
    g_tileLod.fetch_add(1, std::memory_order_relaxed);
    // High rate — do not flood the ring; counter only.
}

void notePoolStart(const char *kind, const QString &detail)
{
    g_poolStarts.fetch_add(1, std::memory_order_relaxed);
    pushEvent(kind && kind[0] ? kind : "pool", detail);
}

Snapshot takeSnapshot()
{
    Snapshot s;
    s.totals = readTotals();
    s.delta.poolStarts = s.totals.poolStarts - g_lastSnapshot.poolStarts;
    s.delta.scheduleProbe = s.totals.scheduleProbe - g_lastSnapshot.scheduleProbe;
    s.delta.scheduleRevalidate = s.totals.scheduleRevalidate - g_lastSnapshot.scheduleRevalidate;
    s.delta.schedulePixels = s.totals.schedulePixels - g_lastSnapshot.schedulePixels;
    s.delta.scheduleTile = s.totals.scheduleTile - g_lastSnapshot.scheduleTile;
    s.delta.scheduleGalleryDecode =
        s.totals.scheduleGalleryDecode - g_lastSnapshot.scheduleGalleryDecode;
    s.delta.tileLodTicks = s.totals.tileLodTicks - g_lastSnapshot.tileLodTicks;
    g_lastSnapshot = s.totals;

    if (QThreadPool *pool = QThreadPool::globalInstance()) {
        s.qtPoolActive = pool->activeThreadCount();
        s.qtPoolMax = pool->maxThreadCount();
    }
    {
        QMutexLocker lock(&g_mu);
        s.recent = g_ring;
    }
    return s;
}

void clear()
{
    g_poolStarts.store(0);
    g_probe.store(0);
    g_reval.store(0);
    g_pixels.store(0);
    g_tile.store(0);
    g_gallery.store(0);
    g_tileLod.store(0);
    g_lastSnapshot = {};
    QMutexLocker lock(&g_mu);
    g_ring.clear();
}

} // namespace BackgroundWorkLog
