// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_LOGGING_H
#define BILTOO_LOGGING_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcSlideshow)

/** Enable or mute biltoo.* debug categories (call once from main for --debug). */
void configureBiltooDebugLogging(bool verbose);

#endif // BILTOO_LOGGING_H
