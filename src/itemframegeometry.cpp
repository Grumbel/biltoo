// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemframegeometry.h"

#include <QtMath>

namespace ItemFrameGeometry {

QPointF unitOr(const QPointF &v, const QPointF &fallback)
{
    const qreal len = qHypot(v.x(), v.y());
    return len > 1e-6 ? v / len : fallback;
}

FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl)
{
    FrameViewGeom g;
    g.tl = tl;
    g.tr = tr;
    g.br = br;
    g.bl = bl;
    g.center = (tl + tr + br + bl) * 0.25;
    g.dirTop = unitOr(tr - tl);
    g.dirRight = unitOr(br - tr);
    g.dirBottom = unitOr(bl - br);
    g.dirLeft = unitOr(tl - bl);
    g.midTop = (tl + tr) * 0.5;
    g.midRight = (tr + br) * 0.5;
    g.midBottom = (br + bl) * 0.5;
    g.midLeft = (bl + tl) * 0.5;
    auto outward = [&](const QPointF &mid, const QPointF &along) {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - g.center) < 0) {
            n = -n;
        }
        return n;
    };
    g.outTop = outward(g.midTop, g.dirTop);
    g.outRight = outward(g.midRight, g.dirRight);
    g.outBottom = outward(g.midBottom, g.dirBottom);
    g.outLeft = outward(g.midLeft, g.dirLeft);
    return g;
}

} // namespace ItemFrameGeometry
