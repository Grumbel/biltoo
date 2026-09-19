// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TILENEIGHBORPREFETCH_H
#define TILENEIGHBORPREFETCH_H

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace tilelod {
class TileLodController;
}

/**
 * Host for off-canvas neighbor tile prefetch.
 * ImageView supplies canvas membership, logical size, and viewport metrics.
 */
class TileNeighborPrefetchHost
{
public:
    virtual ~TileNeighborPrefetchHost() = default;

    /** True when a live canvas item already owns @p path (skip prefetch). */
    virtual bool pathOnLiveCanvas(const QString &path) const = 0;

    /** Logical / native size for density planning; empty if unknown. */
    virtual QSize logicalSizeForPath(const QString &path) const = 0;

    /** Viewport size in widget pixels (before device pixel ratio). */
    virtual QSize viewportWidgetSize() const = 0;

    virtual qreal prefetchDevicePixelRatio() const = 0;

    /** Key-repeat / slideshow nav: pause issue but keep slots. */
    virtual bool tilePrefetchNavHot() const = 0;
};

/**
 * Off-canvas neighbor tile prefetch.
 *
 * Keeps TileLodController instances alive so async completions land in the
 * shared path cache. Live canvas items take over via pathOnLiveCanvas.
 * Session replace must call clear().
 */
class TileNeighborPrefetch : public QObject
{
    Q_OBJECT

public:
    explicit TileNeighborPrefetch(TileNeighborPrefetchHost *host,
                                  QObject *parent = nullptr);

    void prefetchPaths(const QStringList &paths, int budgetPerPath);
    void clear();
    bool empty() const { return m_slots.empty(); }

private:
    void tick();
    void ensureTimer();

    struct Slot {
        QString path;
        std::unique_ptr<tilelod::TileLodController> controller;
        int ticksLeft = 0;
        int budgetPerTick = 4;
    };

    TileNeighborPrefetchHost *m_host = nullptr;
    std::vector<Slot> m_slots;
    class QTimer *m_timer = nullptr;

    static constexpr double kPrefetchMaxDpc = 0.25;
    static constexpr int kPrefetchMaxTicks = 30;
    static constexpr int kTickIntervalMs = 33;
    /** Retained Succeeded tiles before neighbor issue is skipped as warm. */
    static constexpr int kPrefetchWarmSucceededMin = 4;
};

#endif // TILENEIGHBORPREFETCH_H
