// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host/thumtoocache.h"
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
    // Seed underlay only from SizeReply (stored with size — not get_lqip,
    // not generation). GUI installs from ImageCache when present.
    if (!lqip.isNull() && !ImageCache::has(pathCopy)) {
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
        // True warm hit: process size memo AND underlay already in ImageCache.
        // Size-only memo is not enough — request_size returns size + stored
        // EMB/LQIP in one Store row (never generate LQIP; never get_lqip alone).
        while (!g_probeFifo.isEmpty()) {
            const QString p = g_probeFifo.first();
            const QSize memoSz = ProcessMemos::instance().size(p);
            if (memoSz.isValid() && memoSz.width() > 0 && memoSz.height() > 0
                && ImageCache::has(p)) {
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
            if (memoSz.isValid() && memoSz.width() > 0 && memoSz.height() > 0
                && ImageCache::has(p)) {
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
    // Warm only when size memo and ImageCache underlay are both present.
    // Size-only memo still needs request_size so stored EMB/LQIP can seed the
    // cache with the size row (no separate get_lqip / generation).
    if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
        memo.isValid() && memo.width() > 0 && memo.height() > 0
        && ImageCache::has(path)) {
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
        if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
            memo.isValid() && memo.width() > 0 && memo.height() > 0
            && ImageCache::has(path)) {
            memoHits.append(qMakePair(path, memo));
            continue;
        }
        need.append(path);
    }
    if (!memoHits.isEmpty()) {
        emitSizeReadyChunked(std::move(memoHits), generation);
    }
    if (!need.isEmpty()) {
        enqueueProbePaths(need);
    }
}

bool sizeProbesBusy()
{
    QMutexLocker lock(&g_probeMu);
    return g_probeInflight > 0 || !g_probeFifo.isEmpty();
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
