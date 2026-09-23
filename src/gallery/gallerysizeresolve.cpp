// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallery/gallerysizeresolve.h"
#include "view/viewtransform.h"

#include "imageview_types.h"
#include "host/thumtoocache.h"
#include "display/imagecache.h"

#include <QTimer>
#include "util/biltoo_thread.h"

GallerySizeResolve::GallerySizeResolve(GallerySizeResolveHost *host,
                                       QObject *parent)
    : QObject(parent)
    , m_host(host)
{
}

bool GallerySizeResolve::startIfNeeded(const QStringList &paths)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("GallerySizeResolve::startIfNeeded");
    if (!m_host) {
        return false;
    }
    m_pending.clear();
    m_total = 0;
    m_resolved = 0;
    m_failed = 0;
    m_active = false;

    QStringList needProbe;
    needProbe.reserve(paths.size());
    // Paths already known (size book or process memo) count as done so a mode
    // switch does not show "0 / N" again after half the session was resolved.
    int already = 0;

    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // Do not call isUnsupported on the GUI (Store get_meta). Probes no-op
        // unsupported paths on the worker.
        // Size alone is not enough: underlay (EMB/LQIP) lives on the same Store
        // size row. Process size memo can survive ImageCache::clear — those
        // paths must still request_size so underlay is seeded again.
        const bool sizeKnown = m_host->hasDefinitiveHostSize(path)
            || isPositiveSize(ThumtooCache::cachedSize(
                   path, /*scheduleRevalidate=*/false));
        const bool underlayHot = ImageCache::has(path);
        if (sizeKnown && underlayHot) {
            if (!m_host->hasDefinitiveHostSize(path)) {
                const QSize cached = ThumtooCache::cachedSize(
                    path, /*scheduleRevalidate=*/false);
                m_host->adoptResolvedSize(path, cached);
            }
            ++already;
            continue;
        }
        m_pending.insert(path);
        needProbe.append(path);
    }

    m_total = already + m_pending.size();
    m_resolved = already;
    if (m_pending.isEmpty()) {
        return false;
    }

    // Session order first so primary/cover pages finish before the tail (TTFP).
    QStringList ordered;
    ordered.reserve(m_pending.size());
    QSet<QString> seen;
    for (const QString &path : m_host->sizeResolvePathOrder()) {
        if (m_pending.contains(path) && !seen.contains(path)) {
            ordered.append(path);
            seen.insert(path);
        }
    }
    for (const QString &path : needProbe) {
        if (!seen.contains(path)) {
            ordered.append(path);
            seen.insert(path);
        }
    }

    // Size first for every packaged Gallery layout. Gate stays active until
    // the full pending set settles — tiles are blocked while active.
    // Bounded concurrency + chunked sizeReady keep the GUI responsive.
    if (!m_host->sizeResolveLayoutDefersPopulate()) {
        // FreeForm / non-Gallery: probe without a gate.
        if (!ordered.isEmpty()) {
            m_host->scheduleSizeProbeBatch(ordered);
        }
        return false;
    }

    // Activate the gate BEFORE scheduling probes. scheduleProbeBatch may emit
    // synchronous sizeReady for process-memo hits; noteProbeSettled must see
    // m_active or those paths stay pending until the progress sweep.
    m_active = true;
    m_elapsed.start();
    ensureProgressTimer();
    m_progressTimer->start();
    updateProgressHud();

    // One batch enqueue — bounded parallel Store size work (not per-path serial).
    if (!ordered.isEmpty()) {
        m_host->scheduleSizeProbeBatch(ordered);
    }
    // Sweep again for sync memo hits that settled during the batch enqueue.
    updateProgressHud();
    return m_active;
}

void GallerySizeResolve::cancel()
{
    const bool wasActive = m_active;
    m_active = false;
    m_pending.clear();
    m_total = 0;
    m_resolved = 0;
    m_failed = 0;
    if (!wasActive) {
        return;
    }
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    if (m_host) {
        m_host->onSizeResolveGateCancelled();
    }
}

void GallerySizeResolve::noteProbeSettled(const QString &path, bool sizeValid)
{
    if (!m_active) {
        return;
    }
    if (!path.isEmpty() && m_pending.remove(path)) {
        if (sizeValid) {
            ++m_resolved;
        } else {
            ++m_failed;
            if (m_host) {
                m_host->adoptSizeProbeFailed(path);
            }
        }
        if (m_host) {
            m_host->onSizeResolvePathSettled(path);
        }
        // Light HUD only (no pending sweep) so the counter moves even when the
        // 100ms timer is starved under heavy sizeReady traffic.
        publishHudCounts();
    }
    if (!m_pending.isEmpty()) {
        return;
    }
    finish();
}

void GallerySizeResolve::finish()
{
    const bool wasActive = m_active;
    m_active = false;
    m_pending.clear();
    if (!wasActive) {
        return;
    }
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    if (m_host) {
        m_host->onSizeResolveGateComplete();
    }
    m_total = 0;
    m_resolved = 0;
    m_failed = 0;
}

void GallerySizeResolve::updateProgressHud()
{
    if (!m_active || m_total <= 0 || !m_host) {
        return;
    }
    // Defense: async warm can fill the process size memo without a probe
    // callback. Sweep pending against the memo every progress tick — but only
    // a bounded batch. Settling hundreds in one tick (adopt + pathSettled each)
    // froze the GUI the same way sync sizeReady did.
    constexpr int kMemoSettlePerTick = 48;
    int settledThisTick = 0;
    const QList<QString> pending = m_pending.values();
    for (const QString &path : pending) {
        if (settledThisTick >= kMemoSettlePerTick) {
            break;
        }
        if (path.isEmpty()) {
            continue;
        }
        // Never settle on definitive size alone while a re-probe for underlay
        // is in flight — that closed the gate before SizeReply seeded ImageCache
        // and let tiles run with blank cells. Settlement for in-flight probes is
        // noteProbeSettled (sizeReady). Warm shortcut: size memo + underlay hot.
        const QSize cached =
            ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        if (!isPositiveSize(cached) || !ImageCache::has(path)) {
            continue;
        }
        if (!m_host->hasDefinitiveHostSize(path)) {
            m_host->adoptResolvedSize(path, cached);
        }
        m_pending.remove(path);
        ++m_resolved;
        ++settledThisTick;
    }
    // One progressive pack arm per tick, not per path.
    if (settledThisTick > 0) {
        m_host->onSizeResolvePathSettled(QString());
    }
    if (m_pending.isEmpty()) {
        finish();
        return;
    }

    publishHudCounts();
}

void GallerySizeResolve::publishHudCounts()
{
    if (!m_active || m_total <= 0 || !m_host) {
        return;
    }
    // Prefer pending-based done so the HUD cannot drift from m_resolved if a
    // path settled only via the memo sweep.
    const int left = m_pending.size();
    const int done = qMax(0, m_total - left);
    // Keep counters aligned for ETA (failed already counted in done via total).
    if (m_resolved + m_failed < done) {
        m_resolved = done - m_failed;
    }
    const qint64 ms = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    QString detail = QStringLiteral("%1 / %2 sizes").arg(done).arg(m_total);
    if (m_failed > 0) {
        detail += QStringLiteral(" · %1 failed").arg(m_failed);
    }
    if (done > 0 && ms > 200) {
        const double per = double(ms) / double(qMax(1, done));
        const int etaMs = int(per * double(left));
        if (etaMs >= 1000) {
            detail += QStringLiteral(" · ~%1 s left").arg((etaMs + 500) / 1000);
        } else {
            detail += QStringLiteral(" · ~%1 ms left").arg(etaMs);
        }
    } else if (ms > 0) {
        detail += QStringLiteral(" · %1 s").arg(ms / 1000.0, 0, 'f', 1);
    }
    m_host->setSizeResolveProgress(QStringLiteral("Resolving sizes…"), detail);
}

void GallerySizeResolve::ensureProgressTimer()
{
    if (m_progressTimer) {
        return;
    }
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(kProgressIntervalMs);
    QObject::connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        updateProgressHud();
    });
}
