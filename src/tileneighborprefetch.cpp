// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tileneighborprefetch.h"

#include "biltoo_thread.h"
#include "thumtoocache.h"
#include "tilelod/tile_lod_controller.hpp"

#include <QRectF>
#include <QTimer>
#include <QtMath>

TileNeighborPrefetch::TileNeighborPrefetch(TileNeighborPrefetchHost *host,
                                           QObject *parent)
    : QObject(parent)
    , m_host(host)
{
}

void TileNeighborPrefetch::clear()
{
    if (m_timer) {
        m_timer->stop();
    }
    m_slots.clear();
}

void TileNeighborPrefetch::prefetchPaths(const QStringList &paths, int budgetPerPath)
{
    ASSERT_GUI_THREAD();
    if (!m_host || paths.isEmpty() || budgetPerPath <= 0 || m_host->tilePrefetchNavHot()) {
        return;
    }
    if (!ThumtooCache::isAvailable()) {
        return;
    }

    const qreal dpr = m_host->prefetchDevicePixelRatio();
    const QSize vp = m_host->viewportWidgetSize();
    const int vpW = vp.width();
    const int vpH = vp.height();

    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (m_host->pathOnLiveCanvas(path)) {
            continue;
        }
        bool existing = false;
        for (Slot &slot : m_slots) {
            if (slot.path == path) {
                slot.ticksLeft = kPrefetchMaxTicks;
                slot.budgetPerTick = budgetPerPath;
                existing = true;
                break;
            }
        }
        if (existing) {
            continue;
        }
        const QSize sz = m_host->logicalSizeForPath(path);
        if (sz.width() < 256 || sz.height() < 1) {
            continue;
        }
        if (!ThumtooCache::hasDurableTilesKnown(path)) {
            (void)ThumtooCache::scheduleTilePyramid(path);
            continue;
        }
        double dpc = kPrefetchMaxDpc;
        if (vpW > 0 && vpH > 0 && sz.width() > 0 && sz.height() > 0) {
            const double sx = (static_cast<double>(vpW) * dpr)
                              / static_cast<double>(sz.width());
            const double sy = (static_cast<double>(vpH) * dpr)
                              / static_cast<double>(sz.height());
            dpc = qMin(kPrefetchMaxDpc, qMin(sx, sy));
        }
        if (!(dpc > 0.0)) {
            continue;
        }
        Slot slot;
        slot.path = path;
        slot.controller = std::make_unique<tilelod::TileLodController>();
        slot.controller->setPath(path);
        slot.controller->setContentSize(sz.width(), sz.height());
        slot.controller->updateViewport(QRectF(0.0, 0.0, sz.width(), sz.height()),
                                        dpc);
        slot.ticksLeft = kPrefetchMaxTicks;
        slot.budgetPerTick = budgetPerPath;
        (void)slot.controller->tick(slot.budgetPerTick);
        m_slots.push_back(std::move(slot));
    }

    if (m_slots.empty()) {
        return;
    }
    ensureTimer();
    if (!m_timer->isActive()) {
        m_timer->start();
    }
}

void TileNeighborPrefetch::tick()
{
    ASSERT_GUI_THREAD();
    if (m_slots.empty()) {
        if (m_timer) {
            m_timer->stop();
        }
        return;
    }
    if (m_host && m_host->tilePrefetchNavHot()) {
        return;
    }

    for (auto it = m_slots.begin(); it != m_slots.end();) {
        if ((m_host && m_host->pathOnLiveCanvas(it->path)) || !it->controller) {
            it = m_slots.erase(it);
            continue;
        }
        (void)it->controller->tick(it->budgetPerTick);
        --it->ticksLeft;
        bool done = it->controller->viewportFullyCovered() || it->ticksLeft <= 0;
        if (!done && it->controller->session()) {
            auto const snap = it->controller->session()->debug_snapshot();
            if (snap.in_flight == 0 && snap.visible > 0
                && snap.exact_succeeded + snap.cache_succeeded > 0
                && !it->controller->session()->request_scale_holding()) {
                done = true;
            }
        }
        if (done) {
            it = m_slots.erase(it);
        } else {
            ++it;
        }
    }

    if (m_slots.empty() && m_timer) {
        m_timer->stop();
    }
}

void TileNeighborPrefetch::ensureTimer()
{
    if (m_timer) {
        return;
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &TileNeighborPrefetch::tick);
}
