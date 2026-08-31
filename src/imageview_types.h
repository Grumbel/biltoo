// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_TYPES_H
#define IMAGEVIEW_TYPES_H

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>

/** Pixel under the cursor for the status / colour readout. */
struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

/** Persisted free-form / workspace placement for one path. */
struct WorkspaceItemState {
    QString path;
    /** Filmstrip / session slot; -1 = unbound. Distinguishes path duplicates. */
    int sessionIndex = -1;
    QPointF pos;
    qreal scale = 1.0;   // scaleX
    qreal scaleY = 1.0;
    /** Workspace placement angle only (free rotate). Never content. */
    qreal rotation = 0.0;
    /** @deprecated kept for older session merges; prefer contentQuarterTurns. */
    qreal orientation = 0.0;
    /** Content transforms baked into pixels (disk → crop → flip → quarter turns). */
    int contentQuarterTurns = 0; // 0..3
    bool contentHFlip = false;
    bool contentVFlip = false;
    qreal opacity = 1.0;
    qreal z = 0.0;
    bool hFlip = false;
    bool vFlip = false;
    /**
     * Session crop in original on-disk pixel coordinates (top-left origin).
     * Applied after decode so navigation reloads keep the crop (same lifetime
     * as rotation/flip in m_itemStates).
     */
    bool hasCrop = false;
    QRect cropRect;
};

#endif // IMAGEVIEW_TYPES_H
