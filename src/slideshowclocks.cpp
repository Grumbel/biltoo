// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowclocks.h"

#include <QtGlobal>

namespace SlideshowClocks {

void integrateMotionProgress01(qreal *t, QElapsedTimer *clock, bool running,
                               bool paused, int pathMs)
{
    if (!t || !clock || !running || paused || pathMs <= 0) {
        return;
    }
    if (!clock->isValid()) {
        clock->start();
        return;
    }
    const qint64 d = clock->restart();
    if (d > 0) {
        *t = qBound(0.0, *t + qreal(d) / qreal(pathMs), 1.0);
    }
}

void advancePhaseMotion(SlideshowPhaseState *phase, SlideshowDwellState *dwell,
                        int pathMs)
{
    if (!phase || !dwell) {
        return;
    }
    integrateMotionProgress01(&phase->fromMotionT, &phase->fromMotionClock,
                              phase->fromMotionClockRunning, dwell->motionPaused,
                              pathMs);
    if (phase->fromMotionClockRunning) {
        dwell->motionT = phase->fromMotionT;
    }
    integrateMotionProgress01(&phase->toMotionT, &phase->toMotionClock,
                              phase->toMotionClockRunning, dwell->motionPaused,
                              pathMs);
}

void advanceDwellMotion(SlideshowDwellState *dwell)
{
    if (!dwell || dwell->durationMs <= 0 || dwell->motionPaused) {
        return;
    }
    if (!dwell->clock.isValid()) {
        dwell->clock.start();
        return;
    }
    const qint64 d = dwell->clock.restart();
    if (d > 0) {
        dwell->motionT =
            qBound(0.0, dwell->motionT + qreal(d) / qreal(dwell->durationMs), 1.0);
        dwell->elapsedOffsetMs =
            qint64(dwell->motionT * qreal(dwell->durationMs));
    }
}

} // namespace SlideshowClocks
