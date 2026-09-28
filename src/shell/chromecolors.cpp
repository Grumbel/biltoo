// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/chromecolors.h"

#include <QMutex>
#include <QMutexLocker>

namespace ChromeColors {
namespace {

QMutex g_mutex;

struct Bases {
    QColor selectFill{0, 210, 255};
    QColor selectStroke{0, 165, 230};
    QColor viewFill{255, 200, 60};
    QColor viewStroke{230, 170, 30};
    QColor activityFill{40, 200, 100};
    QColor activityStroke{20, 160, 80};
    QColor searchFill{180, 80, 220};
    QColor searchStroke{180, 80, 220};
};

Bases &store()
{
    static Bases s;
    return s;
}

QColor withAlpha(const QColor &rgb, int alpha)
{
    QColor c = rgb;
    c.setAlpha(qBound(0, alpha, 255));
    return c;
}

QColor &fillRef(Bases &b, Role role)
{
    switch (role) {
    case Role::Select:
        return b.selectFill;
    case Role::View:
        return b.viewFill;
    case Role::Activity:
        return b.activityFill;
    case Role::Search:
        return b.searchFill;
    }
    return b.selectFill;
}

QColor &strokeRef(Bases &b, Role role)
{
    switch (role) {
    case Role::Select:
        return b.selectStroke;
    case Role::View:
        return b.viewStroke;
    case Role::Activity:
        return b.activityStroke;
    case Role::Search:
        return b.searchStroke;
    }
    return b.selectStroke;
}

} // namespace

QColor defaultFillBase(Role role)
{
    switch (role) {
    case Role::Select:
        // Cyan-leaning: separates from neutral grey filmstrip / canvas chrome.
        return QColor(0, 210, 255);
    case Role::View:
        return QColor(255, 200, 60);
    case Role::Activity:
        return QColor(40, 200, 100);
    case Role::Search:
        return QColor(180, 80, 220);
    }
    return QColor(0, 210, 255);
}

QColor defaultStrokeBase(Role role)
{
    switch (role) {
    case Role::Select:
        return QColor(0, 165, 230);
    case Role::View:
        return QColor(230, 170, 30);
    case Role::Activity:
        return QColor(20, 160, 80);
    case Role::Search:
        return QColor(180, 80, 220);
    }
    return QColor(0, 165, 230);
}

QColor fillBase(Role role)
{
    QMutexLocker lock(&g_mutex);
    return fillRef(store(), role);
}

QColor strokeBase(Role role)
{
    QMutexLocker lock(&g_mutex);
    return strokeRef(store(), role);
}

void setFillBase(Role role, const QColor &rgb)
{
    if (!rgb.isValid()) {
        return;
    }
    QMutexLocker lock(&g_mutex);
    QColor c = rgb;
    c.setAlpha(255);
    fillRef(store(), role) = c;
}

void setStrokeBase(Role role, const QColor &rgb)
{
    if (!rgb.isValid()) {
        return;
    }
    QMutexLocker lock(&g_mutex);
    QColor c = rgb;
    c.setAlpha(255);
    strokeRef(store(), role) = c;
}

void resetToDefaults()
{
    QMutexLocker lock(&g_mutex);
    Bases &b = store();
    b.selectFill = defaultFillBase(Role::Select);
    b.selectStroke = defaultStrokeBase(Role::Select);
    b.viewFill = defaultFillBase(Role::View);
    b.viewStroke = defaultStrokeBase(Role::View);
    b.activityFill = defaultFillBase(Role::Activity);
    b.activityStroke = defaultStrokeBase(Role::Activity);
    b.searchFill = defaultFillBase(Role::Search);
    b.searchStroke = defaultStrokeBase(Role::Search);
}

QColor selectFill(int alpha)
{
    return withAlpha(fillBase(Role::Select), alpha);
}
QColor selectStroke(int alpha)
{
    return withAlpha(strokeBase(Role::Select), alpha);
}

QColor viewFill(int alpha)
{
    return withAlpha(fillBase(Role::View), alpha);
}
QColor viewStroke(int alpha)
{
    return withAlpha(strokeBase(Role::View), alpha);
}

QColor activityFill(int alpha)
{
    return withAlpha(fillBase(Role::Activity), alpha);
}
QColor activityStroke(int alpha)
{
    return withAlpha(strokeBase(Role::Activity), alpha);
}

QColor searchFill(int alpha)
{
    return withAlpha(fillBase(Role::Search), alpha);
}
QColor searchStroke(int alpha)
{
    return withAlpha(strokeBase(Role::Search), alpha);
}

} // namespace ChromeColors
