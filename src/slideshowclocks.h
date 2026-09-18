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

/** Durations below 250 ms are treated as invalid → @p fallbackMs. */
inline int sanitizeDwellDurationMs(int durationMs, int fallbackMs = 3000)
{
    return durationMs < 250 ? fallbackMs : durationMs;
}

inline int clampIntervalMs(int intervalMs)
{
    return intervalMs <= 0 ? 1 : intervalMs;
}

inline int clampTransitionMs(int transitionMs, int intervalMs)
{
    return qBound(0, transitionMs, qMax(0, intervalMs));
}

inline int clampZoomIndex(int index)
{
    return qBound(0, index, 2);
}

inline int clampMotionIndex(int index)
{
    return qBound(0, index, 2);
}

inline int clampTransitionKind(int kind)
{
    return qBound(0, kind, 3);
}

inline int clampLetterboxFillIndex(int index)
{
    return qBound(0, index, 2);
}

/** Session path index in [0, pathCount-1]; 0 when empty. */
inline int clampPathIndex(int index, int pathCount)
{
    if (pathCount <= 0) {
        return 0;
    }
    return qBound(0, index, pathCount - 1);
}

inline qreal pureFrac(int pureMs, int intervalMs)
{
    return intervalMs > 0 ? qreal(pureMs) / qreal(intervalMs) : 1.0;
}

inline qreal transitionBlendT(qreal phaseT, qreal pureFrac)
{
    const qreal denom = 1.0 - pureFrac;
    if (denom <= 1e-9) {
        return 1.0;
    }
    return qBound(0.0, (phaseT - pureFrac) / denom, 1.0);
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
