// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACENAVGEOMETRY_H
#define WORKSPACENAVGEOMETRY_H

#include <QPointF>
#include <QtGlobal>
#include <QtCore/qnamespace.h>

/**
 * Pure spatial neighbour scoring for Workspace arrow-key navigation.
 * Prefer candidates in the arrow direction; score = primary + weight * cross.
 */
namespace WorkspaceNavGeometry {

constexpr qreal kDirectionEps = 1.0;
constexpr qreal kCrossWeight = 2.5;

struct DirectionScore {
    bool inDirection = false;
    qreal score = 0.0;
};

/** Score candidate relative to origin for @p key (Left/Right/Up/Down). */
inline DirectionScore scoreRelative(Qt::Key key, const QPointF &origin,
                                    const QPointF &candidate)
{
    DirectionScore out;
    const QPointF d = candidate - origin;
    qreal primary = 0.0;
    qreal cross = 0.0;
    switch (key) {
    case Qt::Key_Left:
        out.inDirection = d.x() < -kDirectionEps;
        primary = -d.x();
        cross = qAbs(d.y());
        break;
    case Qt::Key_Right:
        out.inDirection = d.x() > kDirectionEps;
        primary = d.x();
        cross = qAbs(d.y());
        break;
    case Qt::Key_Up:
        out.inDirection = d.y() < -kDirectionEps;
        primary = -d.y();
        cross = qAbs(d.x());
        break;
    case Qt::Key_Down:
        out.inDirection = d.y() > kDirectionEps;
        primary = d.y();
        cross = qAbs(d.x());
        break;
    default:
        return out;
    }
    if (out.inDirection) {
        out.score = primary + kCrossWeight * cross;
    }
    return out;
}

} // namespace WorkspaceNavGeometry

#endif // WORKSPACENAVGEOMETRY_H
