// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CROPRECIPE_H
#define CROPRECIPE_H

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVector>

/** Panel recipe for batch / current-page crop (BATCH_APPEARANCE_BRAINSTORM §14). */
struct CropPanelRecipe {
    enum class Mode {
        ManualMargins = 0, /**< L/T/R/B inset in pixels of logical page size */
        Autocrop = 1       /**< ImageLoader::autoTrimRect + optional extra margin */
    };

    Mode mode = Mode::ManualMargins;
    int marginLeft = 0;
    int marginTop = 0;
    int marginRight = 0;
    int marginBottom = 0;
    /** ImageLoader::autoTrimRect colorThreshold (default 24). */
    int autocropThreshold = 24;
    /** Expand result outward by these pixels, clamped to page. */
    int extraLeft = 0;
    int extraTop = 0;
    int extraRight = 0;
    int extraBottom = 0;

    bool isIdentity() const
    {
        return mode == Mode::ManualMargins
            && marginLeft == 0 && marginTop == 0
            && marginRight == 0 && marginBottom == 0
            && extraLeft == 0 && extraTop == 0
            && extraRight == 0 && extraBottom == 0;
    }
};

namespace CropRecipeUtil {

/** Manual-margin suggestion from text band regions (image / logical pixel space). */
struct SuggestedMargins {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool ok = false;
};


/**
 * Compute axis-aligned crop in @p logicalSize pixel space from a panel recipe.
 * @p sample may be empty for Manual mode; required (non-null) for Autocrop.
 */
QRect computeCropRect(const CropPanelRecipe &recipe, const QSize &logicalSize,
                      const QImage &sample);

/** True when rect is usable crop (non-empty, inside logical, not full frame). */
bool isUsableCrop(const QRect &rect, const QSize &logicalSize);

/**
 * Suggest manual L/T/R/B insets that exclude header/footer/page-number bands.
 * @p bandRegions are axis-aligned boxes in the same pixel space as @p logicalSize
 * (image / logical page pixels, top-left origin).
 * Only regions whose centre lies in the top or bottom 25% of the page contribute.
 * Side insets stay 0 in v1.
 */
SuggestedMargins suggestMarginsFromBandRegions(const QSize &logicalSize,
                                               const QVector<QRectF> &bandRegions);

} // namespace CropRecipeUtil

#endif
