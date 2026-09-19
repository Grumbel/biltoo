// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWCLOCKS_H
#define SLIDESHOWCLOCKS_H

#include "slideshowtypes.h"

#include <QString>

/**
 * Pure-clock integration for slideshow Ken Burns / phase motion.
 * No QObject, no viewport — ImageView owns timers and calls these on tick.
 */
namespace SlideshowClocks {

/** UI spin (seconds) → internal milliseconds. */
inline int secondsToMs(double sec)
{
    return qMax(0, qRound(sec * 1000.0));
}

/** Internal milliseconds → UI spin (seconds), clamped to @p capSec. */
inline double msToSeconds(int ms, double capSec = 3600.0)
{
    return qBound(0.0, ms / 1000.0, capSec);
}

/**
 * Compact clock string for HUD: M:SS or H:MM:SS (non-negative ms).
 * Pure formatting — no locale.
 */
inline QString formatClockMs(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    const qint64 totalSec = ms / 1000;
    const int hours = int(totalSec / 3600);
    const int minutes = int((totalSec % 3600) / 60);
    const int seconds = int(totalSec % 60);
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

/** Timeline elapsed for unitless position in one cycle of @p intervalMs. */
inline qint64 timelineElapsedMs(qreal position, int intervalMs, qint64 totalMs)
{
    if (intervalMs <= 0 || totalMs <= 0) {
        return 0;
    }
    const qint64 elapsed = qint64(position * qreal(intervalMs)) % totalMs;
    return elapsed < 0 ? 0 : elapsed;
}

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

/** For rate math: treat non-positive as 1 ms (never divide by zero). */
inline int clampIntervalMs(int intervalMs)
{
    return intervalMs <= 0 ? 1 : intervalMs;
}

/** Stored dwell interval: 0 = as-fast-as-possible; UI max 3600 s. */
inline int clampStoredIntervalMs(int ms, int maxMs = 3600000)
{
    return qBound(0, ms, maxMs);
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


/**
 * Adaptive step toward a shorter interval (faster slideshow).
 * Fine additive steps near zero; coarser / multiplicative at longer dwells.
 */
inline int intervalFaster(int ms)
{
    if (ms <= 0) {
        return 0;
    }
    if (ms <= 50) {
        return qMax(0, ms - 10);
    }
    if (ms <= 200) {
        return qMax(0, ms - 25);
    }
    if (ms <= 1000) {
        return qMax(0, ms - 100);
    }
    if (ms <= 5000) {
        return qMax(1000, int(qRound(ms / 1.25)));
    }
    return qMax(5000, int(qRound(ms / 1.25)));
}

/** Adaptive step toward a longer interval (slower slideshow). */
inline int intervalSlower(int ms)
{
    if (ms < 50) {
        return ms + 10;
    }
    if (ms < 200) {
        return ms + 25;
    }
    if (ms < 1000) {
        return ms + 100;
    }
    if (ms < 5000) {
        const int next = int(qRound(ms * 1.25));
        return qMin(5000, qMax(ms + 1, next));
    }
    const int next = int(qRound(ms * 1.25));
    return qMin(60000, qMax(ms + 1, next));
}

} // namespace SlideshowClocks

#endif // SLIDESHOWCLOCKS_H
