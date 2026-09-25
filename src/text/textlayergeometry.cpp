// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textlayergeometry.h"

#include <QtMath>
#include <algorithm>

namespace TextLayerGeometry {

QVector<int> indicesIntersecting(const QVector<QRectF> &regionRects, const QRectF &rubber)
{
    QVector<int> out;
    if (rubber.isEmpty()) {
        return out;
    }
    out.reserve(regionRects.size());
    for (int i = 0; i < regionRects.size(); ++i) {
        const QRectF &img = regionRects.at(i);
        if (img.isEmpty()) {
            continue;
        }
        if (img.intersects(rubber)) {
            out.push_back(i);
        }
    }
    return out;
}

void sortReadingOrder(QVector<int> *indices, const QVector<QRectF> &regionRects,
                      qreal topTolerance, const QVector<int> *blockIds)
{
    if (!indices || indices->isEmpty()) {
        return;
    }
    const bool useBlocks = blockIds && blockIds->size() == regionRects.size();
    std::sort(indices->begin(), indices->end(), [&](int a, int b) {
        if (a < 0 || a >= regionRects.size() || b < 0 || b >= regionRects.size()) {
            return a < b;
        }
        if (useBlocks) {
            const int ba = blockIds->at(a);
            const int bb = blockIds->at(b);
            if (ba >= 0 && bb >= 0 && ba != bb) {
                return ba < bb;
            }
        }
        const QRectF &ra = regionRects.at(a);
        const QRectF &rb = regionRects.at(b);
        if (qAbs(ra.top() - rb.top()) > topTolerance) {
            return ra.top() < rb.top();
        }
        return ra.left() < rb.left();
    });
}

} // namespace TextLayerGeometry
