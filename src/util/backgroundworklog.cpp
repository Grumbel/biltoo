// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/backgroundworklog.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QVector>

namespace BackgroundWorkLog {
namespace {

constexpr int kRing = 80;

QMutex g_mu;
QVector<Event> g_ring;

std::atomic<std::uint64_t> g_poolStarts{0};
std::atomic<std::uint64_t> g_probe{0};
std::atomic<std::uint64_t> g_reval{0};
std::atomic<std::uint64_t> g_pixels{0};
std::atomic<std::uint64_t> g_tile{0};
std::atomic<std::uint64_t> g_gallery{0};
std::atomic<std::uint64_t> g_tileLod{0};

Counters g_lastSnapshot;

Counters readTotals()
{
    Counters t;
    t.poolStarts = g_poolStarts.load(std::memory_order_relaxed);
    t.scheduleProbe = g_probe.load(std::memory_order_relaxed);
    t.scheduleRevalidate = g_reval.load(std::memory_order_relaxed);
    t.schedulePixels = g_pixels.load(std::memory_order_relaxed);
    t.scheduleTile = g_tile.load(std::memory_order_relaxed);
    t.scheduleGalleryDecode = g_gallery.load(std::memory_order_relaxed);
    t.tileLodTicks = g_tileLod.load(std::memory_order_relaxed);
    return t;
}

void pushEvent(const char *kind, const QString &detail)
{
    Event e;
    e.msEpoch = QDateTime::currentMSecsSinceEpoch();
    e.kind = QString::fromUtf8(kind);
    e.detail = detail;
    QMutexLocker lock(&g_mu);
    // Coalesce consecutive identical kind+base-detail within 80ms so a viewport
    // of many cells does not fill the ring with the same line.
    if (!g_ring.isEmpty()) {
        Event &last = g_ring.last();
        auto baseDetail = [](QString d) -> QString {
            const int x = d.lastIndexOf(QStringLiteral(" ×"));
            if (x >= 0) {
                bool ok = false;
                d.mid(x + 2).toInt(&ok);
                if (ok) {
                    return d.left(x).trimmed();
                }
            }
            return d;
        };
        const QString baseNew = baseDetail(e.detail);
        const QString baseOld = baseDetail(last.detail);
        if (last.kind == e.kind && baseOld == baseNew
            && (e.msEpoch - last.msEpoch) < 80) {
            int times = 2;
            const int x = last.detail.lastIndexOf(QStringLiteral(" ×"));
            if (x >= 0) {
                bool ok = false;
                const int n = last.detail.mid(x + 2).toInt(&ok);
                if (ok && n >= 1) {
                    times = n + 1;
                }
            }
            last.detail = baseNew.isEmpty()
                ? QStringLiteral("×%1").arg(times)
                : (baseNew + QStringLiteral(" ×%1").arg(times));
            last.msEpoch = e.msEpoch;
            return;
        }
    }
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

void noteProbe(const QString &detail)
{
    g_probe.fetch_add(1, std::memory_order_relaxed);
    pushEvent("probe", detail);
}

void noteRevalidate(const QString &detail)
{
    g_reval.fetch_add(1, std::memory_order_relaxed);
    pushEvent("revalidate", detail);
}

void notePixels(const QString &detail)
{
    g_pixels.fetch_add(1, std::memory_order_relaxed);
    pushEvent("pixels", detail);
}

void noteTile(const QString &detail)
{
    g_tile.fetch_add(1, std::memory_order_relaxed);
    pushEvent("tile", detail);
}

void noteGalleryDecode(const QString &detail)
{
    g_gallery.fetch_add(1, std::memory_order_relaxed);
    pushEvent("galleryDecode", detail);
}

void noteTileLodTick()
{
    g_tileLod.fetch_add(1, std::memory_order_relaxed);
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
