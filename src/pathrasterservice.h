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
 *
 * PreferCache returns BestAvailable from soft / overview / TileSynth — it does
 * **not** generate a tile pyramid. When want exceeds overview (~1024) and
 * PreferCache plateaus, this service queues FocusFull (scheduleTilePyramid) so
 * tiles are built, then Full under EscalateToFull, then one PreferCache retry
 * for TileSynth.
 */
class PathRasterService : public QObject
{
    Q_OBJECT
public:
    enum class ClimbPolicy {
        SoftDisplay = 0,
        EscalateToFull = 1,
    };

    explicit PathRasterService(QObject *parent = nullptr);

    void ensure(const QString &path, int wantEdge,
                const QSize &knownNative = QSize(),
                ClimbPolicy policy = ClimbPolicy::SoftDisplay);

    void cancel(const QString &path);
    void invalidateAll();

    QImage best(const QString &path, int minLongEdge = 0) const;

    int haveEdge(const QString &path) const;
    int wantEdge(const QString &path) const;
    bool isGaveUp(const QString &path) const;
    void clearPreferGaveUp(const QString &path);
    bool isClimbPending(const QString &path) const;

    void noteDelivery(const QString &path, int requestEdge, const QImage &image);

signals:
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
        bool tilesQueued = false;
        int postTilePreferAttempts = 0;
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
