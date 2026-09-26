// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QString>
#include "util/debugflags.h"
#include <QVector>
#include <cstdio>
#include <cstdlib>

/**
 * Time-to-first-pixel stage tracer.
 * Enable: BILTOO_TTFP=1 or BILTOO_PERF=1
 * Prints a flamegraph-style stderr report when endSession() is called, or when
 * noteFirstPixels() fires (first installDisplayPixels with non-null image).
 */
namespace TtfpTrace {

inline bool enabled()
{
    return debugFlag(DebugFlags::Ttfp) || debugFlag(DebugFlags::Perf);
}

struct Stage {
    QString name;
    qint64 usFromStart = 0;
    qint64 usDelta = 0;
};

struct Session {
    QElapsedTimer clock;
    QVector<Stage> stages;
    qint64 lastMarkUs = 0;
    bool firstPixels = false;
    bool active = false;
};

inline Session &session()
{
    static Session s;
    return s;
}

inline void begin(const char *label)
{
    if (!enabled()) {
        return;
    }
    Session &s = session();
    s.stages.clear();
    s.firstPixels = false;
    s.active = true;
    s.clock.start();
    s.lastMarkUs = 0;
    Stage st;
    st.name = QString::fromUtf8(label);
    st.usFromStart = 0;
    st.usDelta = 0;
    s.stages.append(st);
    fprintf(stderr, "biltoo/ttfp: BEGIN %s\n", label);
}

inline void mark(const char *stage)
{
    if (!enabled() || !session().active) {
        return;
    }
    Session &s = session();
    const qint64 now = s.clock.nsecsElapsed() / 1000;
    Stage st;
    st.name = QString::fromUtf8(stage);
    st.usFromStart = now;
    st.usDelta = now - s.lastMarkUs;
    s.lastMarkUs = now;
    s.stages.append(st);
    fprintf(stderr, "biltoo/ttfp: +%6.1f ms  Δ%6.1f ms  %s\n",
            now / 1000.0, st.usDelta / 1000.0, stage);
}

inline void report(const char *why)
{
    if (!enabled() || !session().active) {
        return;
    }
    Session &s = session();
    const qint64 total = s.clock.nsecsElapsed() / 1000;
    fprintf(stderr, "biltoo/ttfp: === report (%s) total=%.1f ms ===\n", why,
            total / 1000.0);
    // Flame-style bars (60 cols max).
    const qint64 scale = total > 0 ? total : 1;
    for (const Stage &st : s.stages) {
        if (st.usFromStart == 0 && st.usDelta == 0 && st.name.startsWith(QLatin1String("BEGIN"))) {
            continue;
        }
        const int bars = int((st.usDelta * 60) / scale);
        fprintf(stderr, "  %6.1f ms |", st.usDelta / 1000.0);
        for (int i = 0; i < bars; ++i) {
            fputc('#', stderr);
        }
        for (int i = bars; i < 4; ++i) {
            fputc(' ', stderr);
        }
        fprintf(stderr, " %s (t+%.1f)\n", qPrintable(st.name), st.usFromStart / 1000.0);
    }
    fprintf(stderr, "biltoo/ttfp: === end ===\n");
}

inline void noteFirstPixels(const char *where)
{
    if (!enabled() || !session().active || session().firstPixels) {
        return;
    }
    session().firstPixels = true;
    mark(where);
    report("first_pixels");
}

inline void endSession(const char *why)
{
    if (!enabled() || !session().active) {
        return;
    }
    if (!session().firstPixels) {
        mark(why);
        report(why);
    }
    session().active = false;
}

} // namespace TtfpTrace
