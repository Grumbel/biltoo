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
    QPointF pos;
    qreal scale = 1.0;   // scaleX
    qreal scaleY = 1.0;
    qreal rotation = 0.0;
    /** Cardinal content orientation (0/90/180/270); Image mode uses this only. */
    qreal orientation = 0.0;
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
