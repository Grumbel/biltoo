// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerysizeresolve.h"
#include "viewtransform.h"

#include "imageview_types.h"
#include "thumtoocache.h"

#include <QTimer>

GallerySizeResolve::GallerySizeResolve(GallerySizeResolveHost *host,
                                       QObject *parent)
    : QObject(parent)
    , m_host(host)
{
}

bool GallerySizeResolve::startIfNeeded(const QStringList &paths)
{
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

    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // Do not call isUnsupported on the GUI (Store get_meta). Probes no-op
        // unsupported paths on the worker.
        if (m_host->hasDefinitiveHostSize(path)) {
            continue;
        }
        if (const QSize cached =
                ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
            isPositiveSize(cached)) {
            m_host->adoptResolvedSize(path, cached);
            continue;
        }
        m_pending.insert(path);
        needProbe.append(path);
    }

    m_total = m_pending.size();
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

    if (!m_host->sizeResolveLayoutDefersPopulate()) {
        // FreeForm / non-packaged: still batch-probe sizes, but do not hold the gate.
        m_pending.clear();
        m_total = 0;
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
    }
    if (!m_pending.isEmpty()) {
        // Do not updateProgressHud here — it sweeps all pending + setCentreProgress
        // on every sizeReady. Chunked memo delivery (16/turn) made that O(n) per
        // path and froze the GUI. The 100ms progress timer owns the HUD.
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
    // callback. Sweep pending against the memo every progress tick.
    const QList<QString> pending = m_pending.values();
    for (const QString &path : pending) {
        if (path.isEmpty()) {
            continue;
        }
        if (m_host->hasDefinitiveHostSize(path)) {
            m_pending.remove(path);
            ++m_resolved;
            m_host->onSizeResolvePathSettled(path);
            continue;
        }
        const QSize cached =
            ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        if (!isPositiveSize(cached)) {
            continue;
        }
        m_host->adoptResolvedSize(path, cached);
        m_pending.remove(path);
        ++m_resolved;
        m_host->onSizeResolvePathSettled(path);
    }
    if (m_pending.isEmpty()) {
        finish();
        return;
    }

    const int done = m_resolved + m_failed;
    const int left = m_pending.size();
    const qint64 ms = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    QString detail = QStringLiteral("%1 / %2 sizes").arg(done).arg(m_total);
    if (m_failed > 0) {
        detail += QStringLiteral(" · %1 failed").arg(m_failed);
    }
    if (done > 0 && ms > 200) {
        const double per = double(ms) / double(done);
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
