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
    if (m_safetyTimer) {
        m_safetyTimer->stop();
    }
    m_pending.clear();
    m_total = 0;
    m_active = false;

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
    }

    m_total = m_pending.size();
    if (m_pending.isEmpty()) {
        return false;
    }

    // Session order first so primary/cover pages finish before the tail (TTFP).
    QStringList need;
    need.reserve(m_pending.size());
    for (const QString &path : m_host->sizeResolvePathOrder()) {
        if (m_pending.contains(path)) {
            need.append(path);
        }
    }
    for (const QString &path : m_pending) {
        if (!need.contains(path)) {
            need.append(path);
        }
    }
    for (const QString &path : need) {
        m_host->scheduleSizeProbe(path);
    }

    if (!m_host->sizeResolveLayoutDefersPopulate()) {
        // FreeForm / non-packaged: provisional pack OK.
        m_pending.clear();
        m_total = 0;
        return false;
    }

    m_active = true;
    ensureTimers();
    m_safetyTimer->start(kSafetyTimeoutMs);
    m_progressTimer->start();
    updateProgressHud();
    return true;
}

void GallerySizeResolve::cancel()
{
    if (m_safetyTimer) {
        m_safetyTimer->stop();
    }
    const bool wasActive = m_active;
    m_active = false;
    m_pending.clear();
    m_total = 0;
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

void GallerySizeResolve::noteProbeSettled(const QString &path)
{
    if (!m_active) {
        return;
    }
    if (!path.isEmpty()) {
        m_pending.remove(path);
    }
    if (!m_pending.isEmpty()) {
        updateProgressHud();
        return;
    }
    finish();
}

void GallerySizeResolve::finish()
{
    if (m_safetyTimer) {
        m_safetyTimer->stop();
    }
    const bool wasActive = m_active;
    m_active = false;
    m_pending.clear();
    m_total = 0;
    if (!wasActive) {
        return;
    }
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    if (m_host) {
        m_host->onSizeResolveGateComplete();
    }
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
            continue;
        }
        const QSize cached =
            ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        if (!isPositiveSize(cached)) {
            continue;
        }
        m_host->adoptResolvedSize(path, cached);
        m_pending.remove(path);
    }
    if (m_pending.isEmpty()) {
        finish();
        return;
    }
    const int done = ViewTransform::nonNeg(m_total - m_pending.size());
    m_host->setSizeResolveProgress(
        tr("Resolving sizes…"),
        tr("%1 / %2").arg(done).arg(m_total));
}

void GallerySizeResolve::ensureTimers()
{
    if (!m_safetyTimer) {
        m_safetyTimer = new QTimer(this);
        m_safetyTimer->setSingleShot(true);
        connect(m_safetyTimer, &QTimer::timeout, this, [this]() {
            if (!m_active) {
                return;
            }
            finish();
        });
    }
    if (!m_progressTimer) {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(kProgressIntervalMs);
        connect(m_progressTimer, &QTimer::timeout, this,
                &GallerySizeResolve::updateProgressHud);
    }
}
