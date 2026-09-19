// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_LOGGING_H
#define BILTOO_LOGGING_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcSlideshow)

/** Enable or mute biltoo.* debug categories (call once from main for --debug). */
void configureBiltooDebugLogging(bool verbose);

/**
 * Timestamped load/PreferCache debug (THUMTOO_DEBUG / BILTOO_LOAD_DEBUG /
 * BILTOO_THUMTOO_DEBUG). Shared by imageview_load and DisplayPipelineController.
 */
void biltooLoadDbg(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif // BILTOO_LOGGING_H
