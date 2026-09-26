// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PATHRASTERSERVICE_H
#define PATHRASTERSERVICE_H

#include "display/rasterclimbsm.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

/**
 * Host-side path → display-raster climb.
 *
 * Policy lives in RasterClimb::Machine (pure SM). This service only:
 *   - maps path → Machine
 *   - reads ImageCache / ThumtooCache pending
 *   - executes Plan (TileSynth/pyramid, PreferCache overview, Full, tiles)
 *
 * Soft ladder encode is not a product path (docs/KILL_SOFT.md).
 * Contract: docs/THUMTOO_HOST_CONTRACT.md
 */
class PathRasterService : public QObject
{
    Q_OBJECT
public:
    enum class ClimbPolicy {
        /** TileSynth / pyramid / overview; never Full native. */
        TileDisplay = 0,
        /** Same first steps, then one Full when need exceeds overview. */
        EscalateToFull = 1,
    };

    explicit PathRasterService(QObject *parent = nullptr);

    void ensure(const QString &path, int wantEdge,
                const QSize &knownNative = QSize(),
                ClimbPolicy policy = ClimbPolicy::TileDisplay);

    void cancel(const QString &path);
    void invalidateAll();

    QImage best(const QString &path, int minLongEdge = 0) const;

    int haveEdge(const QString &path) const;
    bool isGaveUp(const QString &path) const;
    bool isClimbPending(const QString &path) const;

    void noteDelivery(const QString &path, int requestEdge, const QImage &image);

signals:
    void rasterImproved(const QString &path, int longEdge);

private:
    struct Entry {
        RasterClimb::Machine machine;
        quint64 epoch = 0;
        /** ensure()/pump schedule cycles — storm detection. */
        int scheduleCycles = 0;
    };

    void pump(const QString &path, Entry &entry);
    static int capWant(int want, const QSize &knownNative);
    static RasterClimb::PendingFlags pendingFlagsFor(const QString &path, int want);
    static RasterClimb::Policy toSmPolicy(ClimbPolicy p);

    QHash<QString, Entry> m_state;
    quint64 m_epoch = 1;
};

#endif
