// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"
#include "thumtoo_process_memos.h"
#include "imagecache.h"
#include "biltoo_thread.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QVector>

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
QMutex g_probeMu;

void pumpProbeQueue();

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
#if defined(BILTOO_HAVE_THUMTOO_LQIP)
    if (!lqip.isNull() && !ImageCache::has(pathCopy)) {
        ImageCache::put(pathCopy, lqip);
    }
#else
    Q_UNUSED(lqip);
#endif
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
    for (const auto &hit : memoHits) {
        emit bridge()->sizeReady(hit.first, hit.second);
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
        emit bridge()->sizeReady(path, memo);
        return;
    }
    enqueueProbePaths(QStringList{path});
}

void scheduleProbeBatch(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    QStringList need;
    need.reserve(paths.size());
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (const QSize memo = cachedSize(path, /*scheduleRevalidate=*/false);
            memo.isValid() && memo.width() > 0 && memo.height() > 0) {
            emit bridge()->sizeReady(path, memo);
            continue;
        }
        need.append(path);
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
