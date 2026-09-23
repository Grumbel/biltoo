// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"
#include "thumtoo_process_memos.h"
#include "display/imagecache.h"
#include "display/displayquality.h"
#include "biltoo_thread.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
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

void pumpProbeQueue();

/**
 * Deliver sizeReady on the GUI thread in small chunks so the event loop can
 * paint between batches. Synchronous emit of hundreds of memo hits from
 * scheduleProbeBatch froze the GUI for the whole warm size pass.
 */
void emitSizeReadyChunked(QVector<QPair<QString, QSize>> hits)
{
    if (hits.isEmpty()) {
        return;
    }
    Bridge *b = bridge();
    if (!b) {
        return;
    }
    auto deliver = std::make_shared<std::function<void(int)>>();
    *deliver = [hits, b, deliver](int index) {
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
                     const QImage &lqip)
{
    {
        QMutexLocker lock(&g_probeMu);
        g_probeQueued.remove(pathCopy);
        if (g_probeInflight > 0) {
            --g_probeInflight;
        }
    }
    if (!ok || !size.isValid() || size.width() < 1 || size.height() < 1) {
        emit bridge()->sizeReady(pathCopy, QSize());
        pumpProbeQueue();
        return;
    }
    if (!lqip.isNull() && !ImageCache::has(pathCopy)) {
        // requestSizeAsync already put EMB/LQIP when SizeReply had them; this is
        // a fallback for the underlay QImage returned on the callback.
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
    {
        QMutexLocker lock(&g_probeMu);
        // Drain memo hits without holding a Store slot.
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
            // Skip pure memo hits already handled; if next is memo, drain again.
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
        emitSizeReadyChunked(std::move(memoHits));
    }
    for (const QString &pathCopy : toStart) {
        requestSizeAsync(pathCopy, [pathCopy](bool ok, const QSize &size, const QImage &lqip) {
            finishProbeSlot(pathCopy, ok, size, lqip);
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
    // Already have size in process memo — no Store round-trip, but still notify
    // so Gallery size-resolve pending is not left waiting forever.
    if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
        memo.isValid() && memo.width() > 0 && memo.height() > 0) {
        emitSizeReadyChunked({{path, memo}});
        return;
    }
    enqueueProbePaths(QStringList{path});
}

void scheduleProbeBatch(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    QVector<QPair<QString, QSize>> memoHits;
    QStringList need;
    need.reserve(paths.size());
    memoHits.reserve(paths.size());
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
            memo.isValid() && memo.width() > 0 && memo.height() > 0) {
            memoHits.append(qMakePair(path, memo));
            continue;
        }
        need.append(path);
    }
    if (!memoHits.isEmpty()) {
        emitSizeReadyChunked(std::move(memoHits));
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

} // namespace ThumtooCache
