// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "attentiongeometry.h"
#include "viewtransform.h"

#include <QLineF>
#include <QtMath>

namespace AttentionGeometry {

QPointF localFromNorm(const QPointF &norm, const QRectF &contentRect)
{
    if (contentRect.isEmpty()) {
        return {};
    }
    return QPointF(contentRect.left() + norm.x() * contentRect.width(),
                   contentRect.top() + norm.y() * contentRect.height());
}

QPointF clampNorm(const QPointF &norm)
{
    return QPointF(ViewTransform::clamp01(norm.x()), ViewTransform::clamp01(norm.y()));
}

QVector<QPointF> clampNormPoints(const QVector<QPointF> &pts)
{
    QVector<QPointF> out;
    out.reserve(pts.size());
    for (const QPointF &p : pts) {
        out.append(clampNorm(p));
    }
    return out;
}

QPointF normFromLocal(const QPointF &local, const QRectF &contentRect)
{
    if (contentRect.isEmpty()) {
        return {};
    }
    const qreal nx = (local.x() - contentRect.left()) / ViewTransform::safeDivisor(contentRect.width());
    const qreal ny = (local.y() - contentRect.top()) / ViewTransform::safeDivisor(contentRect.height());
    return clampNorm(QPointF(nx, ny));
}

QPointF normDeltaFromLocalDelta(const QPointF &localDelta, const QRectF &contentRect)
{
    if (contentRect.isEmpty()) {
        return {};
    }
    return QPointF(localDelta.x() / ViewTransform::safeDivisor(contentRect.width()),
                   localDelta.y() / ViewTransform::safeDivisor(contentRect.height()));
}

QVector<QPointF> translateSelectedNorms(const QVector<QPointF> &startPts,
                                        const QVector<int> &selected,
                                        const QPointF &dNorm)
{
    QVector<QPointF> pts = startPts;
    for (int idx : selected) {
        if (idx < 0 || idx >= pts.size()) {
            continue;
        }
        pts[idx] = clampNorm(pts[idx] + dNorm);
    }
    return pts;
}

int handleIndexAt(const QPoint &viewPos, const QVector<QPointF> &viewPts)
{
    int best = -1;
    qreal bestDist = kHandleScreenPx + 1.0;
    for (int i = 0; i < viewPts.size(); ++i) {
        const qreal d = QLineF(viewPts.at(i), QPointF(viewPos)).length();
        const qreal lim = (i == 0) ? kPrimaryScreenPx : kHandleScreenPx;
        if (d <= lim && d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QVector<int> indicesInViewRect(const QVector<QPointF> &viewPts, const QRect &band)
{
    QVector<int> hit;
    const QRect b = band.normalized();
    for (int i = 0; i < viewPts.size(); ++i) {
        if (b.contains(viewPts.at(i).toPoint())) {
            hit.append(i);
        }
    }
    return hit;
}

QVector<int> mergeSelection(const QVector<int> &current, const QVector<int> &hit,
                            bool additive)
{
    if (!additive) {
        return hit;
    }
    QVector<int> out = current;
    for (int i : hit) {
        if (!out.contains(i)) {
            out.append(i);
        }
    }
    return out;
}

QVector<int> toggleSelectionIndex(const QVector<int> &selected, int index)
{
    QVector<int> out = selected;
    if (out.contains(index)) {
        out.removeAll(index);
    } else {
        out.append(index);
    }
    return out;
}

} // namespace AttentionGeometry
