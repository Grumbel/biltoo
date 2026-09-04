// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icons.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QSvgRenderer>

namespace {

bool isStandardDesktopIconName(const QString &name)
{
    // FreeDesktop-style names that themes provide well — prefer the theme.
    return name.startsWith(QLatin1String("document-"))
        || name.startsWith(QLatin1String("edit-"))
        || name.startsWith(QLatin1String("go-"))
        || name.startsWith(QLatin1String("media-"))
        || name.startsWith(QLatin1String("zoom-"))
        || name.startsWith(QLatin1String("view-"))
        || name.startsWith(QLatin1String("object-"))
        || name == QLatin1String("folder-open")
        || name == QLatin1String("list-add")
        || name == QLatin1String("preferences-system");
}

QIcon iconFromSvgResource(const QString &resource)
{
    if (!QFile::exists(resource)) {
        return {};
    }
    {
        const QIcon native(resource);
        if (!native.isNull() && !native.availableSizes().isEmpty()) {
            return native;
        }
    }
    QSvgRenderer renderer(resource);
    if (!renderer.isValid()) {
        return {};
    }
    QIcon icon;
    for (int s : {16, 22, 24, 32, 48, 64}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);

    if (isStandardDesktopIconName(name)) {
        const QIcon theme = QIcon::fromTheme(name);
        if (!theme.isNull() && !theme.availableSizes().isEmpty()) {
            return theme;
        }
        const QIcon embedded = iconFromSvgResource(resource);
        if (!embedded.isNull()) {
            return embedded;
        }
        return QApplication::style()->standardIcon(fallback);
    }

    // Biltoo-specific names (gallery-*, workspace-*, …): our artwork first.
    const QIcon embedded = iconFromSvgResource(resource);
    if (!embedded.isNull()) {
        return embedded;
    }
    const QIcon theme = QIcon::fromTheme(name);
    if (!theme.isNull() && !theme.availableSizes().isEmpty()) {
        return theme;
    }
    return QApplication::style()->standardIcon(fallback);
}

QIcon resourceIcon(const QString &name)
{
    return iconFromSvgResource(QStringLiteral(":/icons/actions/%1.svg").arg(name));
}
