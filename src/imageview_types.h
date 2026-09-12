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
#include <QVector>
#include "coloradjust.h"
#include <QtGlobal>

/**
 * Stable identity of a session image (one entry in the ordered session list).
 * Never reuse an id after removal. Index in the session list is order only and
 * may shift on insert/delete; this id does not.
 * 0 is invalid / unbound.
 */
using SessionImageId = qint64;
inline constexpr SessionImageId kInvalidSessionImageId = 0;

/** True when width and height are both positive. */
inline bool isPositiveSize(const QSize &s)
{
    return s.isValid() && s.width() > 0 && s.height() > 0;
}

/**
 * Scale @a sample so its long edge equals @a longEdge (aspect preserved).
 * Used for provisional layout geometry — never adopt soft pixel magnitude.
 */
inline QSize scaleToLongEdge(const QSize &sample, int longEdge)
{
    if (!isPositiveSize(sample) || longEdge <= 0) {
        return {};
    }
    const int sampleLong = qMax(sample.width(), sample.height());
    if (sampleLong <= 0) {
        return {};
    }
    const qreal s = qreal(longEdge) / qreal(sampleLong);
    return QSize(qMax(1, int(sample.width() * s + 0.5)),
                 qMax(1, int(sample.height() * s + 0.5)));
}

/**
 * True when @a incoming covers less than ~90% of @a have's area — used to
 * reject soft/ladder sizes that would shrink a known logical size.
 */
inline bool isMuchSmallerArea(const QSize &incoming, const QSize &have)
{
    if (!isPositiveSize(incoming) || !isPositiveSize(have)) {
        return false;
    }
    const qint64 haveArea = qint64(have.width()) * qint64(have.height());
    const qint64 inArea = qint64(incoming.width()) * qint64(incoming.height());
    return haveArea > 0 && inArea * 10 < haveArea * 9;
}

/** Neutral long-edge for provisional pack/fit when only aspect is known. */
inline constexpr int kProvisionalLayoutLongEdge = 1024;

/** Pixel under the cursor for the status / colour readout. */
struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

/**
 * Per-Workspace canvas background (project state). When mode is AppDefault,
 * ImageView draws using application preferences (technical default).
 */
enum class WorkspaceBackgroundMode {
    AppDefault = 0, /**< Preferences / built-in default — not stored as override */
    Solid = 1,
    Checkerboard = 2,
    ImageTile = 3 /**< Tiled image pattern */
};

struct WorkspaceBackground {
    WorkspaceBackgroundMode mode = WorkspaceBackgroundMode::AppDefault;
    QColor color{42, 42, 42};
    QColor colorAlt{48, 48, 48};
    /** Absolute path to tile image when mode == ImageTile. */
    QString imagePath;
    /** Optional path relative to the project file (portable projects). */
    QString imagePathRelative;
    /** Optional content hash for the tile image (project verification). */
    QString imageSha256;

    bool isAppDefault() const { return mode == WorkspaceBackgroundMode::AppDefault; }
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
    /**
     * Horizontal shear in item-local space (before placement rotation).
     * Linear pose is R(θ)·H(k)·S(sx,sy) with H = [[1,k],[0,1]]. 0 = none.
     */
    qreal shear = 0.0;
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
    /**
     * Rotation of the crop rectangle about its centre, in degrees (counter-clockwise
     * in image / item-local space). Applied when extracting; output pixels are
     * axis-aligned. 0 = axis-aligned crop (default).
     */
    qreal cropRotation = 0.0;
    /**
     * Attention / focus points in normalized image coordinates (0–1, top-left
     * origin). Primary (index 0) drives slideshow Ken Burns; additional points
     * are user waypoints editable in Attention mode.
     */
    bool hasAttention = false;
    QPointF attentionNorm; // primary; mirrors attentionPoints[0] when non-empty
    QVector<QPointF> attentionPoints; // all points; empty ⇔ !hasAttention
    ColorAdjustments colorAdjust;

    /** Keep hasAttention / attentionNorm in sync with attentionPoints. */
    void syncAttentionPrimary()
    {
        if (attentionPoints.isEmpty()) {
            hasAttention = false;
            attentionNorm = QPointF(0.5, 0.5);
            return;
        }
        hasAttention = true;
        attentionNorm = attentionPoints.first();
    }
};

/**
 * Per-path Gallery soft/display ladder bookkeeping (host side).
 * Pixels themselves live in ImageCache / ImageItem — this is only policy state.
 *
 * have       — long edge of soft pixels known for the path (0 = none)
 * want       — last computed target ladder step from visibility + zoom
 * inflight   — soft edge currently requested (0 = idle); at most one per path
 * fullInflight — native ImageLoader::load in flight
 * gaveUpWant — highest want finished without ~90% delivery (anti-storm)
 * failed     — permanent hard failure for this path
 */
struct GallerySoftState {
    int have = 0;
    int want = 0;
    int inflight = 0;
    bool fullInflight = false;
    int gaveUpWant = 0;
    bool failed = false;
    qint64 inflightSinceMs = 0;

    /**
     * True when the decode window should enqueue more soft work for this path.
     * Pure policy — no I/O.
     *
     * @param anyBlank  at least one live tile for the path has no display pixels
     * @param anyFull   a tile already holds full (non-soft) decoded pixels
     */
    bool needsSoftSchedule(int wantEdge, bool anyBlank, bool anyFull) const
    {
        if (failed || anyFull) {
            return false;
        }
        if (have >= wantEdge && !anyBlank) {
            return false;
        }
        if (gaveUpWant >= wantEdge && !anyBlank) {
            return false;
        }
        // Soft already climbing and tiles show something — wait for delivery.
        if (inflight > 0 && have > 0 && !anyBlank) {
            return false;
        }
        return true;
    }
};

#endif // IMAGEVIEW_TYPES_H
