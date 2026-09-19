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
/** FIFO order — whole session/archive sequential, not parallel. */
QStringList g_probeSerialFifo;
bool g_probeSerialInflight = false;
QMutex g_probeMu;

void pumpProbeSerial();

void finishProbeSerialSlot(const QString &pathCopy, bool ok, const QSize &size,
                           const QImage &lqip)
{
    {
        QMutexLocker lock(&g_probeMu);
        g_probeQueued.remove(pathCopy);
        g_probeSerialInflight = false;
    }
    if (!ok || !size.isValid()) {
        emit bridge()->sizeReady(pathCopy, QSize());
        pumpProbeSerial();
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
    pumpProbeSerial();
}

void pumpProbeSerial()
{
    QString next;
    // Memo hits: still emit sizeReady so Gallery size-resolve pending clears.
    // Async warmSessionOpenMemos can fill ProcessMemos size while probes sit
    // in the FIFO; skipping without a signal left "Resolving sizes…" stuck.
    QVector<QPair<QString, QSize>> memoHits;
    {
        QMutexLocker lock(&g_probeMu);
        if (g_probeSerialInflight) {
            return;
        }
        while (!g_probeSerialFifo.isEmpty()) {
            const QString p = g_probeSerialFifo.takeFirst();
            const QSize memoSz = ProcessMemos::instance().size(p);
            if (memoSz.isValid()) {
                g_probeQueued.remove(p);
                memoHits.append(qMakePair(p, memoSz));
                continue;
            }
            next = p;
            g_probeSerialInflight = true;
            break;
        }
        if (next.isEmpty() && memoHits.isEmpty()) {
            return;
        }
    }
    for (const auto &hit : memoHits) {
        emit bridge()->sizeReady(hit.first, hit.second);
    }
    if (next.isEmpty()) {
        return;
    }

    const QString pathCopy = next;
    requestSizeAsync(pathCopy, [pathCopy](bool ok, const QSize &size, const QImage &lqip) {
        finishProbeSerialSlot(pathCopy, ok, size, lqip);
    });
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
        memo.isValid()) {
        emit bridge()->sizeReady(path, memo);
        return;
    }
    {
        QMutexLocker lock(&g_probeMu);
        if (g_probeQueued.contains(path)) {
            return;
        }
        g_probeQueued.insert(path);
        g_probeSerialFifo.append(path);
    }
    pumpProbeSerial();
}

} // namespace ThumtooCache
