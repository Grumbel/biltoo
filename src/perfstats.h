// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PERFSTATS_H
#define PERFSTATS_H

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

#include <cstdlib>

/**
 * BILTOO_PERF / THUMTOO_DEBUG paint + decode-window timings.
 * QElapsedTimer is process-local; no ImageView dependency.
 */
struct PerfStats {
    /** Log decode-window when last pass exceeds this (µs). */
    static constexpr qint64 kWarnDecodeWindowUs = 4000;

    bool enabled = false;

    bool isEnabled() const { return enabled; }

    qreal fpsValue() const { return fps; }

    qint64 lastPaintUsValue() const { return lastPaintUs; }

    qint64 lastDecodeWindowUsValue() const { return lastDecodeWindowUs; }

    qint64 maxDecodeWindowUsValue() const { return maxDecodeWindowUs; }

    int decodeWindowRunsValue() const { return decodeWindowRuns; }
    QElapsedTimer fpsClock;
    int frameCount = 0;
    qreal fps = 0.0;
    qint64 lastPaintUs = 0;
    qint64 lastDecodeWindowUs = 0;
    qint64 maxDecodeWindowUs = 0;
    int decodeWindowRuns = 0;

    void enableFromEnv()
    {
        const char *p = std::getenv("BILTOO_PERF");
        const char *d = std::getenv("THUMTOO_DEBUG");
        enabled = (p && p[0] && p[0] != '0')
                  || (d && d[0] && d[0] != '0');
        if (enabled) {
            fpsClock.start();
        }
    }

    /** Record end of a timed paintEvent; updates FPS every ~500 ms. */
    void notePaintUs(qint64 us)
    {
        lastPaintUs = us;
        ++frameCount;
        if (!fpsClock.isValid()) {
            fpsClock.start();
        } else if (fpsClock.elapsed() >= 500) {
            const qint64 ms = fpsClock.elapsed();
            fps = (ms > 0) ? (frameCount * 1000.0 / qreal(ms)) : 0.0;
            frameCount = 0;
            fpsClock.restart();
        }
    }

    /** Record one Gallery decode-window pass duration. */
    void noteDecodeWindowUs(qint64 us)
    {
        lastDecodeWindowUs = us;
        maxDecodeWindowUs = qMax(maxDecodeWindowUs, lastDecodeWindowUs);
        ++decodeWindowRuns;
    }

    bool lastDecodeWindowSlow() const
    {
        return lastDecodeWindowUs > kWarnDecodeWindowUs;
    }

    /**
     * HUD line body (without tr wrapper). Empty when disabled.
     * Caller localises via tr("FPS %1 · paint %2 ms · decode-win %3 ms (max %4)").
     */
    bool hasHudLine() const { return enabled; }
};

#endif // PERFSTATS_H
