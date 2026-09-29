// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BACKGROUNDWORKLOG_H
#define BACKGROUNDWORKLOG_H

#include <QString>
#include <QtGlobal>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <cstdint>

/**
 * Lightweight process-wide counters + recent event ring for diagnosing
 * post-settle CPU (Qt pool jobs, thumtoo schedules). Safe from any thread.
 */
namespace BackgroundWorkLog {

struct Counters {
    std::uint64_t poolStarts = 0;
    std::uint64_t scheduleProbe = 0;
    std::uint64_t scheduleRevalidate = 0;
    std::uint64_t schedulePixels = 0;
    std::uint64_t scheduleTile = 0;
    std::uint64_t scheduleGalleryDecode = 0;
    std::uint64_t tileLodTicks = 0;
};

struct Event {
    qint64 msEpoch = 0;
    QString kind;
    QString detail;
};

struct Snapshot {
    Counters totals;
    Counters delta; ///< since previous takeSnapshot (or clear)
    int qtPoolActive = 0;
    int qtPoolMax = 0;
    QVector<Event> recent; ///< newest last, up to capacity
};

void note(const char *kind, const QString &detail = QString());
void noteProbe();
void noteRevalidate();
void notePixels();
void noteTile();
void noteGalleryDecode();
void noteTileLodTick();
void notePoolStart(const char *kind, const QString &detail = QString());

/** Snapshot totals + deltas; updates internal baseline for next delta. */
Snapshot takeSnapshot();
void clear();

} // namespace BackgroundWorkLog

#endif
