// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "attentiongeometry.h"

#include <QLineF>

namespace AttentionGeometry {

QPointF localFromNorm(const QPointF &norm, const QRectF &contentRect)
{
    if (contentRect.isEmpty()) {
        return {};
    }
    return QPointF(contentRect.left() + norm.x() * contentRect.width(),
                   contentRect.top() + norm.y() * contentRect.height());
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

} // namespace AttentionGeometry
