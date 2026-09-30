// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host/thumtoocache.h"
#include <QFileInfo>
#include "util/backgroundworklog.h"
#include "host/thumtoo_process_memos.h"
#include "display/imagecache.h"
#include "display/displayquality.h"
#include "util/biltoo_thread.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

namespace ThumtooCache {
namespace {

/** Paths queued or in-flight for size probe (dedupe). */
QSet<QString> g_probeQueued;
/** FIFO order — session order preserved; drained with bounded concurrency. */
QStringList g_probeFifo;
/** In-flight Store size requests (bounded parallel batch). */
int g_probeInflight = 0;
/** Cold-open / session size pass: probe many paths at once (not one-by-one). */
constexpr int kMaxConcurrentSizeProbes = 8;
/** Memo sizeReady emits per event-loop turn — avoids GUI freeze on warm open. */
constexpr int kSizeReadyChunk = 16;
QMutex g_probeMu;
/**
 * Bumped on cancelSizeProbes / session replace. In-flight Store callbacks and
 * chunked memo emits capture the generation at start and no-op side effects
 * when it no longer matches.
 */
std::atomic<quint64> g_probeGeneration{0};

void pumpProbeQueue();

/**
 * Deliver sizeReady on the GUI thread in small chunks so the event loop can
 * paint between batches. Synchronous emit of hundreds of memo hits from
 * scheduleProbeBatch froze the GUI for the whole warm size pass.
 */
void emitSizeReadyChunked(QVector<QPair<QString, QSize>> hits, quint64 generation)
{
    if (hits.isEmpty()) {
        return;
    }
    Bridge *b = bridge();
    if (!b) {
        return;
    }
    auto deliver = std::make_shared<std::function<void(int)>>();
    *deliver = [hits, b, deliver, generation](int index) {
        if (generation != g_probeGeneration.load(std::memory_order_acquire)) {
            return;
        }
        GUI_BUDGET("ThumtooCache::sizeReadyChunk");
        const int n = hits.size();
        if (index >= n) {
            return;
        }
        const int end = qMin(index + kSizeReadyChunk, n);
        for (int i = index; i < end; ++i) {
            emit b->sizeReady(hits.at(i).first, hits.at(i).second);
        }
        if (end < n) {
            QTimer::singleShot(0, b, [deliver, end]() { (*deliver)(end); });
        }
    };
    // Always queue — never run the first chunk synchronously on the GUI
    // inside scheduleProbeBatch / startIfNeeded (that still froze open).
    QTimer::singleShot(0, b, [deliver]() { (*deliver)(0); });
}

void finishProbeSlot(const QString &pathCopy, bool ok, const QSize &size,
                     const QImage &lqip, quint64 generation)
{
    const bool live = (generation == g_probeGeneration.load(std::memory_order_acquire));
    {
        QMutexLocker lock(&g_probeMu);
        // Only drop the queued mark when this slot is still live. After cancel +
        // re-enqueue of the same path, a stale callback must not erase the new
        // session's dedupe entry (that caused double request_size).
        if (live) {
            g_probeQueued.remove(pathCopy);
        }
        if (g_probeInflight > 0) {
            --g_probeInflight;
        }
    }
    if (!live) {
        // Superseded session: do not emit, memo, or refill ImageCache.
        pumpProbeQueue();
        return;
    }
    if (!ok || !size.isValid() || size.width() < 1 || size.height() < 1) {
        emit bridge()->sizeReady(pathCopy, QSize());
        pumpProbeQueue();
        return;
    }
    // Seed underlay from SizeReply (stored with size row — not get_lqip,
    // not generation). Always put when non-null; ImageCache keeps the larger
    // sample. GUI installs from ImageCache when present.
    if (!lqip.isNull()) {
        const int le = ImageCache::longEdge(lqip);
        const QString tag = (le > DisplayQuality::kLqipMaxEdge)
            ? QStringLiteral("EMB")
            : QStringLiteral("LQIP");
        ImageCache::put(pathCopy, lqip, tag);
    }
    noteCachedSize(pathCopy, size);
    emit bridge()->sizeReady(pathCopy, size);
    pumpProbeQueue();
}

void pumpProbeQueue()
{
    QVector<QPair<QString, QSize>> memoHits;
    QStringList toStart;
    const quint64 generation = g_probeGeneration.load(std::memory_order_acquire);
    {
        QMutexLocker lock(&g_probeMu);
        // Warm hit: process size memo only. Do not require ImageCache underlay
        // (that re-probed entire archives after restart with hot SQLite).
        while (!g_probeFifo.isEmpty()) {
            const QString p = g_probeFifo.first();
            const QSize memoSz = ProcessMemos::instance().size(p);
            if (memoSz.isValid() && memoSz.width() > 0 && memoSz.height() > 0) {
                g_probeFifo.removeFirst();
                g_probeQueued.remove(p);
                memoHits.append(qMakePair(p, memoSz));
                continue;
            }
            break;
        }
        while (g_probeInflight < kMaxConcurrentSizeProbes && !g_probeFifo.isEmpty()) {
            const QString p = g_probeFifo.first();
            const QSize memoSz = ProcessMemos::instance().size(p);
            if (memoSz.isValid() && memoSz.width() > 0 && memoSz.height() > 0) {
                g_probeFifo.removeFirst();
                g_probeQueued.remove(p);
                memoHits.append(qMakePair(p, memoSz));
                continue;
            }
            g_probeFifo.removeFirst();
            ++g_probeInflight;
            toStart.append(p);
        }
    }
    if (!memoHits.isEmpty()) {
        emitSizeReadyChunked(std::move(memoHits), generation);
    }
    for (const QString &pathCopy : toStart) {
        requestSizeAsync(pathCopy, [pathCopy, generation](bool ok, const QSize &size,
                                                          const QImage &lqip) {
            finishProbeSlot(pathCopy, ok, size, lqip, generation);
        });
    }
}

void enqueueProbePaths(const QStringList &paths)
{
    bool any = false;
    {
        QMutexLocker lock(&g_probeMu);
        for (const QString &path : paths) {
            if (path.isEmpty() || g_probeQueued.contains(path)) {
                continue;
            }
            g_probeQueued.insert(path);
            g_probeFifo.append(path);
            any = true;
        }
    }
    if (any) {
        pumpProbeQueue();
    }
}

} // namespace

void scheduleProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    BackgroundWorkLog::noteProbe(QFileInfo(path).fileName());
    // Size memo alone is enough to skip Store request_size. Requiring
    // ImageCache underlay forced a full-session re-probe after every restart
    // (hot SQLite, cold process underlay) — looked like "probing the whole
    // archive" on a warm durable cache. Underlay seeds opportunistically on
    // SizeReply when a probe does run; missing underlay must not block the
    // size gate or re-walk every member.
    if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
        memo.isValid() && memo.width() > 0 && memo.height() > 0) {
        emitSizeReadyChunked({{path, memo}},
                             g_probeGeneration.load(std::memory_order_acquire));
        return;
    }
    enqueueProbePaths(QStringList{path});
}

void scheduleProbeBatch(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    const quint64 generation = g_probeGeneration.load(std::memory_order_acquire);
    QVector<QPair<QString, QSize>> memoHits;
    QStringList need;
    need.reserve(paths.size());
    memoHits.reserve(paths.size());
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // GUI-safe memo peek only (cachedSize on GUI does not touch Store).
        if (const QSize memo = ProcessMemos::instance().size(path);
            memo.isValid() && memo.width() > 0 && memo.height() > 0) {
            memoHits.append(qMakePair(path, memo));
            continue;
        }
        need.append(path);
    }
    if (!memoHits.isEmpty()) {
        emitSizeReadyChunked(std::move(memoHits), generation);
    }
    if (need.isEmpty()) {
        return;
    }

    // One worker: Store get_size only (no request_size LQIP/EMB, no parallel
    // warmSessionOpenMemos race). Hydrate process memos, emit hits, then
    // enqueueProbePaths only for true Store misses (cold / incomplete rows).
    const QStringList needCopy = need;
    auto hydrate = [needCopy, generation]() {
        ASSERT_NOT_GUI_THREAD();
        if (generation != g_probeGeneration.load(std::memory_order_acquire)) {
            return;
        }
        init();
        QVector<QPair<QString, QSize>> storeHits;
        QStringList stillNeed;
        storeHits.reserve(needCopy.size());
        stillNeed.reserve(needCopy.size() / 8 + 1);
        for (const QString &path : needCopy) {
            if (generation != g_probeGeneration.load(std::memory_order_acquire)) {
                return;
            }
            if (path.isEmpty()) {
                continue;
            }
            // Light path: Client::get_size (region dims only).
            const QSize sz = cachedSize(path, /*scheduleRevalidate=*/false);
            if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
                storeHits.append(qMakePair(path, sz));
            } else {
                stillNeed.append(path);
            }
        }
        if (generation != g_probeGeneration.load(std::memory_order_acquire)) {
            return;
        }
        if (!storeHits.isEmpty()) {
            emitSizeReadyChunked(std::move(storeHits), generation);
        }
        if (!stillNeed.isEmpty()) {
            enqueueProbePaths(stillNeed);
        }
    };
    QThreadPool::globalInstance()->start(hydrate);
}

bool sizeProbesBusy()
{
    QMutexLocker lock(&g_probeMu);
    return g_probeInflight > 0 || !g_probeFifo.isEmpty();
}

ProbeQueueSnapshot probeQueueSnapshot()
{
    ProbeQueueSnapshot s;
    QMutexLocker lock(&g_probeMu);
    s.queued = g_probeFifo.size();
    s.inflight = g_probeInflight;
    s.busy = s.queued > 0 || s.inflight > 0;
    return s;
}

quint64 sizeProbeGeneration()
{
    return g_probeGeneration.load(std::memory_order_acquire);
}

void cancelSizeProbes()
{
    {
        QMutexLocker lock(&g_probeMu);
        g_probeFifo.clear();
        g_probeQueued.clear();
        // Leave g_probeInflight — finishProbeSlot decrements when Store replies.
    }
    g_probeGeneration.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace ThumtooCache
