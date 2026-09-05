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
