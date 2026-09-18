// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWCLOCKS_H
#define SLIDESHOWCLOCKS_H

#include "slideshowtypes.h"

/**
 * Pure-clock integration for slideshow Ken Burns / phase motion.
 * No QObject, no viewport — ImageView owns timers and calls these on tick.
 */
namespace SlideshowClocks {

/** Path length = dwell interval + transition (min 250 ms). */
inline int pathDurationMs(int progressIntervalMs, int transitionDurationMs)
{
    const int ms = progressIntervalMs + qMax(0, transitionDurationMs);
    return qMax(250, ms);
}

/** Elapsed / duration clamped to [0,1]; 0 when duration is non-positive. */
inline qreal progress01(qint64 elapsedMs, int durationMs)
{
    if (durationMs <= 0) {
        return 0.0;
    }
    return qBound(0.0, qreal(elapsedMs) / qreal(durationMs), 1.0);
}

/**
 * Integrate wall Δt into motion T ∈ [0,1].
 * Clocks only measure Δt; pathMs changes alter rate, not remapped progress.
 */
void integrateMotionProgress01(qreal *t, QElapsedTimer *clock, bool running,
                               bool paused, int pathMs);

/** Advance dual-phase from/to motion clocks; mirrors from→dwell motionT when from runs. */
void advancePhaseMotion(SlideshowPhaseState *phase, SlideshowDwellState *dwell,
                        int pathMs);

/** Advance single-dwell motionT from dwell.clock. */
void advanceDwellMotion(SlideshowDwellState *dwell);

} // namespace SlideshowClocks

#endif // SLIDESHOWCLOCKS_H
