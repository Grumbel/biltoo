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

#include "gallerysoftsm.h"
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

/**
 * Why Gallery may repack. Decode and view resize are not reasons.
 * applyLayout(reason) is the only pack entry point for Gallery.
 */
enum class GalleryPackReason {
    ExplicitLayout, /**< Layout toolbar / menu while already in Gallery */
    EnterGallery,   /**< Entering Gallery or rebuilding from session list */
    Reload,         /**< F5 / explicit reload with relayout */
    ContentChange,  /**< Content flip/rotate changed tile aspect for pack */
    SessionMutate,  /**< Add/duplicate/remove that must show tiles without holes */
};

/** Canvas interaction tool (Select rubber-band, Pan, Zoom region). */
enum class Tool {
    Select,
    Pan,
    Zoom /**< Workspace: rubber-band zoom to region */
};

/** Workspace / gallery packing mode (ImageView layout engine). */
enum class LayoutMode {
    FreeForm,
    SideBySide, // horizontal strip (UI: "Horizontal")
    Vertical,   // vertical strip
    Grid,
    /** Square cells; image scaled to cover and centre-cropped (like thumb crop). */
    GridCrop,
    /** Column masonry: N columns spanning the view width; variable row heights. */
    Masonry,
    /** Row masonry: N rows spanning the view height; variable column widths. */
    MasonryRows,
    /** Column masonry scaled per-column to a shared bottom edge (no dangling). */
    MasonryFill,
    /** Row masonry scaled per-row to a shared right edge (no dangling). */
    MasonryRowsFill,
    /** Session order wrap (book/comic contact sheet). */
    Flow,
    /** Flow with each row scaled to full layout width. */
    FlowFill,
    /** Two-up spreads; cover page alone, then pairs. */
    Facing
};

/** True when layout packs tiles (not free-form Workspace arrangement). */
inline bool layoutIsPackaged(LayoutMode mode)
{
    return mode != LayoutMode::FreeForm;
}

/** Height-fitted pack modes (side-by-side / row masonry family). */
inline bool layoutIsHeightFitted(LayoutMode mode)
{
    switch (mode) {
    case LayoutMode::SideBySide:
    case LayoutMode::MasonryRows:
    case LayoutMode::MasonryRowsFill:
        return true;
    default:
        return false;
    }
}

/** Grid / GridCrop — no intrinsic sizes required for pack. */
inline bool layoutIsGridFamily(LayoutMode mode)
{
    return mode == LayoutMode::Grid || mode == LayoutMode::GridCrop;
}

/** Flow / FlowFill pack family. */
inline bool layoutIsFlowFamily(LayoutMode mode)
{
    return mode == LayoutMode::Flow || mode == LayoutMode::FlowFill;
}

/** Masonry column family (vertical bands, not rows). */
inline bool layoutIsMasonryColumns(LayoutMode mode)
{
    return mode == LayoutMode::Masonry || mode == LayoutMode::MasonryFill;
}

/** Facing (book spread) pack mode. */
inline bool layoutIsFacing(LayoutMode mode)
{
    return mode == LayoutMode::Facing;
}

/** Side-by-side horizontal strip. */
inline bool layoutIsSideBySide(LayoutMode mode)
{
    return mode == LayoutMode::SideBySide;
}

/** Vertical strip pack mode. */
inline bool layoutIsVertical(LayoutMode mode)
{
    return mode == LayoutMode::Vertical;
}

/** Fill-mode pack that needs global sizes before first pack. */
inline bool layoutNeedsAllSizes(LayoutMode mode)
{
    switch (mode) {
    case LayoutMode::MasonryFill:
    case LayoutMode::MasonryRowsFill:
    case LayoutMode::FlowFill:
        return true;
    default:
        return false;
    }
}


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
 * True when @a incoming is a strict downgrade vs @a have (edge-only) — used to
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

    void clear()
    {
        valid = false;
        imagePos = {};
        pixelColor = {};
        path.clear();
    }
};

/**
 * Per-Workspace canvas background (project state). When mode is AppDefault,
 * ImageView draws using application preferences (technical default).
 */
enum class WorkspaceBackgroundMode {
    AppDefault = 0, /**< Preferences / built-in default — not stored as override */
    Solid = 1,
    Checkerboard = 2,
    ImageTile = 3, /**< Tiled image pattern */
    /** Cover-scale blur of the current image (Image mode view override only). */
    ContentBlur = 4
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

    /** True when durable override fields match (ignores tile pixmap cache). */
    bool matches(const WorkspaceBackground &o) const
    {
        return mode == o.mode && color == o.color && colorAlt == o.colorAlt
            && imagePath == o.imagePath && imagePathRelative == o.imagePathRelative;
    }
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
 * Per-path Gallery decode-window bookkeeping (host side).
 *
 * Gallery underlay is LQIP only; sharpness is tiles (docs/GALLERY_PIXELS.md).
 * PathRaster PreferCache soft climb is not used. This struct only answers:
 * "should the decode window spend a concurrency slot on this path?" —
 * visibility, blank cells, inflight budget, tile-pyramid-queued.
 *
 * have       — best installed long edge (LQIP / host sample) for this path
 * want       — last on-screen need from decode-window pass
 * inflight   — concurrency token (0 = idle); at most one per path
 * failed     — permanent hard failure for this path
 */
struct GallerySoftState : GallerySoft::State {
    /**
     * Record a ladder delivery for concurrency bookkeeping only.
     */
    void noteLadderDelivery(int requestEdge, int gotEdge, int softFloor)
    {
        GallerySoft::noteLadderDelivery(*this, requestEdge, gotEdge, softFloor);
    }

    /** scheduleTilePyramid issued once for this path (tile band). */
    bool tilesPyramidQueued = false;

    bool isTilesPyramidQueued() const { return tilesPyramidQueued; }

    void markTilesPyramidQueued() { tilesPyramidQueued = true; }
};


#endif // IMAGEVIEW_TYPES_H
