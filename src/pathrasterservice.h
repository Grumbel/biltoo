// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PATHRASTERSERVICE_H
#define PATHRASTERSERVICE_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

/**
 * Central host-side path → display-raster climb.
 *
 * Owns the policy that was previously duplicated across Image-mode PreferCache,
 * gallery soft, and slideshow preload:
 *   soft → PreferCache display → (optional) stop when adequate for want edge.
 *
 * Pixels always land in ImageCache (upward-only). Consumers listen to
 * rasterImproved and read ImageCache::get — they do not schedule thumtoo jobs.
 *
 * Geometry (logical size) is not owned here; pass knownNative to cap want edge.
 */
class PathRasterService : public QObject
{
    Q_OBJECT
public:
    explicit PathRasterService(QObject *parent = nullptr);

    /**
     * Ensure ImageCache holds a sample covering @p wantEdge (capped by native
     * long edge when @p knownNative is valid). Idempotent; raises the target
     * if a higher edge is requested later.
     */
    void ensure(const QString &path, int wantEdge, const QSize &knownNative = QSize());

    /** Drop climb state for one path (session path leave). */
    void cancel(const QString &path);

    /** Invalidate all in-flight policy; bump epoch so late deliveries no-op. */
    void invalidateAll();

    /** Best sample already in ImageCache (possibly null / smaller than want). */
    QImage best(const QString &path, int minLongEdge = 0) const;

    int haveEdge(const QString &path) const;
    int wantEdge(const QString &path) const;
    bool isGaveUp(const QString &path) const;
    /**
     * Clear PreferCache shortfall latch so the same want edge can be
     * re-requested (slideshow: tiles may exist after a cold shortfall).
     * Does not change want/have; call ensure() afterward to pump.
     */
    void clearPreferGaveUp(const QString &path);
    /** True when soft or PreferCache work is queued for @p path (epoch-current). */
    bool isClimbPending(const QString &path) const;

    /**
     * Thumtoo ladderReady / pool soft completion. Always ImageCache::put first.
     * Continues the climb when still short of want.
     */
    void noteDelivery(const QString &path, int requestEdge, const QImage &image);

signals:
    /** ImageCache improved for path (long edge of best sample). GUI thread. */
    void rasterImproved(const QString &path, int longEdge);

private:
    struct State {
        int want = 0;
        int have = 0;
        int lastDisplayWant = 0;
        int lastDisplayGot = 0;
        bool displayQueued = false;
        bool softQueued = false;
        bool preferGaveUp = false;
        quint64 epoch = 0;
    };

    void pump(const QString &path, State &st);
    static int capWant(int want, const QSize &knownNative);

    QHash<QString, State> m_state;
    quint64 m_epoch = 1;
};

#endif
