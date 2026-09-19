// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "biltoo_logging.h"

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

#include <QDateTime>
#include <QThread>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {

bool biltooLoadDebugEnabled()
{
    static const bool on = []() {
        auto env = [](const char *k) {
            const char *e = std::getenv(k);
            return e && e[0] && e[0] != '0';
        };
        return env("THUMTOO_DEBUG") || env("BILTOO_LOAD_DEBUG")
            || env("BILTOO_THUMTOO_DEBUG");
    }();
    return on;
}

} // namespace

void biltooLoadDbg(const char *fmt, ...)
{
    if (!biltooLoadDebugEnabled()) {
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
