// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/biltoo_logging.h"

#include "util/debugflags.h"

#include <QDateTime>
#include <QThread>

#include <cstdarg>
#include <cstdio>

Q_LOGGING_CATEGORY(lcSlideshow, "biltoo.slideshow")

void configureBiltooDebugLogging(bool verbose)
{
    // Custom categories default to enabled for QtDebugMsg on some builds —
    // force slideshow noise off unless --debug.
    if (verbose) {
        QLoggingCategory::setFilterRules(QStringLiteral("biltoo.slideshow.debug=true"));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral("biltoo.slideshow.debug=false"));
    }
}

void biltooLoadDbg(const char *fmt, ...)
{
    if (!debugFlag(DebugFlags::Load) && !debugFlag(DebugFlags::ThumtooDebug)) {
        return;
    }
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    fprintf(stderr, "biltoo/load t=%lld gui=%d ", static_cast<long long>(ms),
            QThread::isMainThread() ? 1 : 0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void biltooModeDbg(const char *fmt, ...)
{
    if (!debugFlag(DebugFlags::Mode)) {
        return;
    }
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    fprintf(stderr, "biltoo/mode t=%lld gui=%d ", static_cast<long long>(ms),
            QThread::isMainThread() ? 1 : 0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
