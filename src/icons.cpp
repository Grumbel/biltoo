// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icons.h"

#include <QApplication>
#include <QFile>
#include <QStyle>

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) {
        return icon;
    }
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);
    if (QFile::exists(resource)) {
        icon = QIcon(resource);
        if (!icon.isNull()) {
            return icon;
        }
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
