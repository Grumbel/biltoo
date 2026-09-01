// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_TYPES_H
#define IMAGEVIEW_TYPES_H

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

/**
 * Stable identity of a session image (one entry in the ordered session list).
 * Never reuse an id after removal. Index in the session list is order only and
 * may shift on insert/delete; this id does not.
 * 0 is invalid / unbound.
 */
using SessionImageId = qint64;
inline constexpr SessionImageId kInvalidSessionImageId = 0;

/** Pixel under the cursor for the status / colour readout. */
struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

/**
 * Placement + content appearance for one session image / canvas object.
 * Identity is @a sessionId (not path). Path is the decode source only.
 */
struct WorkspaceItemState {
    QString path;
    /** Stable session-image id; 0 = unbound. */
    SessionImageId sessionId = kInvalidSessionImageId;
    /**
     * @deprecated List position cache only — not identity. Prefer sessionId.
     * May be -1 when unknown; do not use for matching after insert/delete.
     */
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
     * Session crop in original on-disk pixel coordinates (top-left origin),
     * relative to @a cropSourceSize at record time. Applied after decode so
     * navigation reloads keep the crop. If the decode size differs (EXIF /
     * decoder variance), the rect is scaled to the live image size.
     */
    bool hasCrop = false;
    QRect cropRect;
    /** Image size when cropRect was recorded; empty = assume live size. */
    QSize cropSourceSize;
};

#endif // IMAGEVIEW_TYPES_H
