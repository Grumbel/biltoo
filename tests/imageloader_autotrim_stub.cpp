// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/** Minimal ImageLoader::autoTrimRect for croprecipe unit tests (no full loader). */

#include "host/imageloader.h"

#include <QImage>
#include <QRect>
#include <QtGlobal>

bool ImageLoader::autoTrimRect(const QImage &image, const QRect &searchRect, QRect *outRect,
                               int colorThreshold, int /*noisePercent*/)
{
    if (!outRect || image.isNull() || searchRect.isEmpty()) {
        return false;
    }
    const QRect bounds = searchRect.intersected(image.rect());
    if (bounds.isEmpty()) {
        return false;
    }

    // Sample corner as background.
    const QRgb bg = image.pixel(bounds.left(), bounds.top());
    const int thr = qMax(0, colorThreshold);

    auto nearBg = [bg, thr](QRgb p) {
        return qAbs(qRed(p) - qRed(bg)) <= thr
            && qAbs(qGreen(p) - qGreen(bg)) <= thr
            && qAbs(qBlue(p) - qBlue(bg)) <= thr;
    };

    int minX = bounds.right();
    int minY = bounds.bottom();
    int maxX = bounds.left();
    int maxY = bounds.top();
    bool any = false;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (!nearBg(image.pixel(x, y))) {
                any = true;
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (!any) {
        return false;
    }
    *outRect = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    return outRect->isValid() && !outRect->isEmpty();
}
