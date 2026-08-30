// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ICONS_H
#define ICONS_H

#include <QIcon>
#include <QStyle>
#include <QString>

/** Theme icon with bundled SVG fallback, then style standard pixmap. */
QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback);

/** Bundled action SVG from resources (e.g. coloured gallery glyphs). */
QIcon resourceIcon(const QString &name);

#endif // ICONS_H
