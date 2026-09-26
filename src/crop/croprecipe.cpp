// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crop/croprecipe.h"
#include "host/imageloader.h"

#include <QtGlobal>
#include <QtMath>
#include <QRect>

namespace CropRecipeUtil {

namespace {

QRect expandOutward(QRect r, int el, int et, int er, int eb, const QSize &bounds)
{
    if (!r.isValid() || bounds.width() < 1 || bounds.height() < 1) {
        return r;
    }
    r.adjust(-el, -et, er, eb);
    return r.intersected(QRect(QPoint(0, 0), bounds));
}

QRect manualInset(const CropPanelRecipe &recipe, const QSize &logicalSize)
{
    const int w = logicalSize.width();
    const int h = logicalSize.height();
    if (w < 1 || h < 1) {
        return {};
    }
    const int left = qBound(0, recipe.marginLeft, w - 1);
    const int top = qBound(0, recipe.marginTop, h - 1);
    const int right = qBound(0, recipe.marginRight, w - left);
    const int bottom = qBound(0, recipe.marginBottom, h - top);
    const int cw = w - left - right;
    const int ch = h - top - bottom;
    if (cw < 1 || ch < 1) {
        return {};
    }
    return QRect(left, top, cw, ch);
}

} // namespace

QRect computeCropRect(const CropPanelRecipe &recipe, const QSize &logicalSize,
                      const QImage &sample)
{
    if (logicalSize.width() < 1 || logicalSize.height() < 1) {
        return {};
    }

    QRect core;
    if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
        if (sample.isNull()) {
            return {};
        }
        // Map sample pixels to logical size: autoTrim in sample space, then scale.
        const QRect search(0, 0, sample.width(), sample.height());
        QRect trimmed;
        if (!ImageLoader::autoTrimRect(sample, search, &trimmed,
                                       recipe.autocropThreshold, /*noisePercent=*/4)) {
            return {};
        }
        if (sample.size() == logicalSize) {
            core = trimmed;
        } else {
            const qreal sx = qreal(logicalSize.width()) / qreal(sample.width());
            const qreal sy = qreal(logicalSize.height()) / qreal(sample.height());
            core = QRect(
                int(qRound(trimmed.x() * sx)),
                int(qRound(trimmed.y() * sy)),
                int(qRound(trimmed.width() * sx)),
                int(qRound(trimmed.height() * sy)));
            core = core.intersected(QRect(QPoint(0, 0), logicalSize));
        }
    } else {
        core = manualInset(recipe, logicalSize);
    }

    if (!core.isValid() || core.isEmpty()) {
        return {};
    }

    core = expandOutward(core, recipe.extraLeft, recipe.extraTop,
                         recipe.extraRight, recipe.extraBottom, logicalSize);
    if (!isUsableCrop(core, logicalSize)) {
        // Full-frame after expand → treat as no crop.
        return {};
    }
    return core;
}

SuggestedMargins suggestMarginsFromBandRegions(const QSize &logicalSize,
                                               const QVector<QRectF> &bandRegions)
{
    SuggestedMargins out;
    const int w = logicalSize.width();
    const int h = logicalSize.height();
    if (w < 8 || h < 8 || bandRegions.isEmpty()) {
        return out;
    }
    const qreal topBand = h * 0.25;
    const qreal botBand = h * 0.75;
    int top = 0;
    int bottom = 0;
    for (const QRectF &r : bandRegions) {
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        const QRectF clipped = r.intersected(QRectF(0, 0, w, h));
        if (clipped.isEmpty()) {
            continue;
        }
        const qreal cy = clipped.center().y();
        if (cy <= topBand) {
            top = qMax(top, int(qCeil(clipped.bottom())));
        } else if (cy >= botBand) {
            bottom = qMax(bottom, h - int(qFloor(clipped.top())));
        }
    }
    // Leave at least ~40% of the page as content.
    const int maxInset = qMax(1, h * 3 / 10);
    top = qBound(0, top, maxInset);
    bottom = qBound(0, bottom, maxInset);
    if (top + bottom >= h - 4) {
        return out;
    }
    if (top == 0 && bottom == 0) {
        return out;
    }
    out.top = top;
    out.bottom = bottom;
    out.ok = true;
    return out;
}

bool isUsableCrop(const QRect &rect, const QSize &logicalSize)
{
    if (!rect.isValid() || rect.isEmpty() || logicalSize.width() < 1
        || logicalSize.height() < 1) {
        return false;
    }
    const QRect page(QPoint(0, 0), logicalSize);
    if (!page.contains(rect)) {
        return false;
    }
    // Full frame is identity — not a crop.
    if (rect == page) {
        return false;
    }
    return rect.width() >= 1 && rect.height() >= 1;
}

} // namespace CropRecipeUtil
