// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONTENTXFORM_H
#define CONTENTXFORM_H

#include "coloradjust.h"
#include "imageview_types.h"

#include <QRect>
#include <QSize>

/**
 * Pure content transform + layout helpers (no ImageItem / GUI).
 *
 * Display attachment stores the xform applied to a sample; the next install
 * compares want vs applied instead of guessing from aspect ratio.
 *
 * Pixel pipeline order (see SessionAppearance::materializeDisplay):
 *   flips → quarter turns → crop → color grade
 */
namespace ContentXform {

struct Value {
    int quarterTurns = 0; // normalized 0..3
    bool hFlip = false;
    bool vFlip = false;
    bool hasCrop = false;
    QRect cropRect;
    QSize cropSourceSize;
    qreal cropRotation = 0.0;
    ColorAdjustments colorAdjust;

    static Value fromState(const WorkspaceItemState &s);
    void applyToState(WorkspaceItemState &s) const;
};

int normalizeQuarterTurns(int quarterTurns);
bool swapsAspect(const Value &x);
bool equal(const Value &a, const Value &b);

/** File-native size → layout size after content turns. */
QSize layoutSize(const QSize &native, const Value &x);
QSize layoutSize(const QSize &native, const WorkspaceItemState &state);

/**
 * Whether a new raw sample must be materialized for @p want.
 * Blank shown, xform mismatch, or strict long-edge upgrade → true.
 */
bool needsRematerialize(const Value &applied, const Value &want,
                        int shownLongEdge, int incomingLongEdge);

} // namespace ContentXform

#endif
