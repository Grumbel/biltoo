// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icons.h"

#include <QApplication>
#include <QFile>
#include <QStyle>

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    // Prefer embedded biltoo icons (works from out-of-tree ./build without
    // install). System theme names like "document-open" would otherwise hide
    // our custom SVGs forever.
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);
    if (QFile::exists(resource)) {
        const QIcon icon(resource);
        if (!icon.isNull()) {
            return icon;
        }
    }
    const QIcon theme = QIcon::fromTheme(name);
    if (!theme.isNull() && !theme.availableSizes().isEmpty()) {
        return theme;
    }
    return QApplication::style()->standardIcon(fallback);
}

QIcon resourceIcon(const QString &name)
{
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);
    if (QFile::exists(resource)) {
        return QIcon(resource);
    }
    return QIcon();
}
