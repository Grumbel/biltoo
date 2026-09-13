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
 * Contract: docs/THUMTOO_HOST_CONTRACT.md
 *   Soft → PreferCache (Display) → optional Full under ClimbPolicy::EscalateToFull.
 *
 * PreferCache may return BestAvailable below the requested edge (e.g. overview
 * 1024 for want 2048). That is not a bug; under SoftDisplay the climb stops for
 * this want; under EscalateToFull the service schedules one Full.
 *
 * Pixels land in ImageCache (upward-only). Consumers listen to rasterImproved.
 * Geometry (logical size) is not owned here; pass knownNative to cap want.
 */
class PathRasterService : public QObject
{
    Q_OBJECT
public:
    /**
     * SoftDisplay — Gallery: Soft + PreferCache; plateau terminal for want.
     * EscalateToFull — Image mode / Slideshow: after PreferCache plateau, one Full.
     */
    enum class ClimbPolicy {
        SoftDisplay = 0,
        EscalateToFull = 1,
    };

    explicit PathRasterService(QObject *parent = nullptr);

    /**
     * Ensure ImageCache holds a sample covering @p wantEdge (capped by native
     * when @p knownNative is valid). Idempotent; raises the target if a higher
     * edge is requested later. @p policy is sticky max (SoftDisplay → Escalate
     * upgrades; never downgrades while state lives).
     */
    void ensure(const QString &path, int wantEdge,
                const QSize &knownNative = QSize(),
                ClimbPolicy policy = ClimbPolicy::SoftDisplay);

    /** Drop climb state for one path (session path leave). */
    void cancel(const QString &path);

    /** Invalidate all in-flight policy; bump epoch so late deliveries no-op. */
    void invalidateAll();

    /** Best sample already in ImageCache (possibly null / smaller than want). */
    QImage best(const QString &path, int minLongEdge = 0) const;

    int haveEdge(const QString &path) const;
    int wantEdge(const QString &path) const;
    /** PreferCache returned BestAvailable below want for this path. */
    bool isGaveUp(const QString &path) const;
    /**
     * Clear PreferCache plateau latch when the host raises product need via a
     * higher ensure() want (also done automatically when want > lastDisplayWant).
     */
    void clearPreferGaveUp(const QString &path);
    /** Soft, PreferCache Display, or Full work queued for @p path (epoch-current). */
    bool isClimbPending(const QString &path) const;

    /**
     * Thumtoo ladderReady / pool soft completion. Always ImageCache::put first.
     * Continues the climb when still short of want (and policy allows).
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
        bool fullQueued = false;
        bool fullDone = false;
        ClimbPolicy policy = ClimbPolicy::SoftDisplay;
        quint64 epoch = 0;
    };

    void pump(const QString &path, State &st);
    static int capWant(int want, const QSize &knownNative);
    static bool covers(int have, int need);

    QHash<QString, State> m_state;
    quint64 m_epoch = 1;
};

#endif
