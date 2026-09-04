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

/** Rasterize an embedded SVG via linked Qt Svg (no iconengines plugin needed). */
QIcon iconFromSvgResource(const QString &resource)
{
    if (!QFile::exists(resource)) {
        return {};
    }
    // Prefer QIcon's native SVG engine when the plugin is on QT_PLUGIN_PATH.
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
    // Multi-size icon so toolbars/menus pick a crisp size.
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
    // Prefer embedded biltoo icons (works from out-of-tree build without install
    // and without QT_PLUGIN_PATH pointing at qtsvg iconengines).
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);
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
