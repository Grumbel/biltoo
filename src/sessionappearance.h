// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONAPPEARANCE_H
#define SESSIONAPPEARANCE_H

#include "imageview_types.h"
#include "coloradjust.h"
#include "contentxform.h"

#include <QHash>
#include <QSet>
#include <QImage>
#include <QRect>
#include <QSize>


/**
 * Session content appearance helpers (DOMAIN: session image crop / flips /
 * quarter turns). Geometry math lives here so ImageView does not fork it.
 *
 * cropRect is top-left origin in the coordinate space of cropSourceSize
 * (or the live image size when cropSourceSize is empty).
 *
 * ## Install contract (raw vs baked)
 *
 * Raw decode pixels (disk / ladder / cache / probe) must pass through one gate
 * before they are attached to a session image for display:
 *
 *   - ImageItem path: ImageView::installDisplayPixels(...)
 *   - QImage-only path (filmstrip override, slideshow blit): applyContentToImage
 *
 * Already-baked pixels (peer display copy, undo after-image) must not be
 * re-baked. Callers that only have pixels use applyContentToImage when the
 * source is known raw.
 */
namespace SessionAppearance {

/** Whether @p pixels are a full on-disk decode or a soft ladder stand-in. */
enum class PixelKind {
    FullSource,  /**< Full (or post-crop) on-disk pixels; crop uses native space */
    SoftPreview, /**< Soft ladder / thumbnail; crop scaled; no layout size write */
};

// Thin aliases so existing SessionAppearance:: call sites keep compiling.
inline int normalizeQuarterTurns(int t) { return ContentXform::normalizeQuarterTurns(t); }
inline bool contentSwapsAspect(const ContentXform::Value &x) { return ContentXform::swapsAspect(x); }
bool contentSwapsAspect(const WorkspaceItemState &state);
inline bool contentXformEqual(const ContentXform::Value &a, const ContentXform::Value &b)
{
    return ContentXform::equal(a, b);
}
inline QSize layoutSize(const QSize &native, const ContentXform::Value &x)
{
    return ContentXform::layoutSize(native, x);
}
inline QSize layoutSize(const QSize &native, const WorkspaceItemState &state)
{
    return ContentXform::layoutSize(native, state);
}
inline bool needsRematerialize(const ContentXform::Value &applied, const ContentXform::Value &want,
                               int shown, int incoming)
{
    return ContentXform::needsRematerialize(applied, want, shown, incoming);
}


/**
 * Sole pixel pipeline: raw decode → display pixels.
 *
 * Order (matches live bakeFlip / bakeRotate90, then crop in post-bake space):
 *   1. contentHFlip / contentVFlip
 *   2. contentQuarterTurns (QImage::trueMatrix + rotate)
 *   3. cropRect (post-orient space; scaled via cropSourceSize)
 *   4. color grade
 *
 * Call this for every QImage that came from disk/ladder/cache before display.
 * Never call twice on the same pixels. Live incremental bakeFlip/bakeRotate90
 * still mutate an already-materialized item and update session state; the next
 * install from raw uses this function with the updated absolute state.
 */
QImage materializeDisplay(const QImage &raw, const WorkspaceItemState &state,
                          PixelKind kind);

/** Map a crop rect from @p recorded size into @p live size (identity if equal). */
QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live);

/**
 * Map stored crop geometry through a content flip of the full source.
 * @p cropSourceSize is the space cropRect lives in; rotation sign flips.
 */
void mapCropThroughContentFlip(WorkspaceItemState &state, bool horizontal, bool vertical);

/**
 * Map stored crop geometry through content 90° steps (same sign as bakeRotate90).
 * Updates cropRect, cropSourceSize, and cropRotation.
 */
void mapCropThroughContentRotate90(WorkspaceItemState &state, int quarterTurns);

/**
 * Map a rectangle from full on-disk (pre-content) image pixel space into the
 * post-content display pixel space used by the baked item.
 *
 * Order matches the *live* post-bake image the user sees (crop UI stores
 * cropRect in post-content space; see enterCropMode / recordSessionCrop):
 *   1. Content horizontal / vertical flip about @p sourceSize
 *   2. Content quarter-turns clockwise (0..3), same matrix as bakeRotate90
 *   3. Session crop (cropRect in cropSourceSize → oriented size)
 *
 * @p sourceSize is the unoriented full-page raster size (native / probe).
 * Returns empty if the rect misses the crop or sizes are invalid.
 */
QRectF mapSourceRectToContentDisplay(const QRectF &sourceRect, const QSize &sourceSize,
                                     const WorkspaceItemState &state);

/**
 * Bake session content appearance into a QImage (no ImageItem).
 *
 * Same order as materializeDisplay. Used for filmstrip overrides, slideshow
 * handoff blits, and SoftPreview pixel prep before setPreviewImage.
 *
 * SoftPreview: crop is scaled into the soft pixel space; result aspect follows
 * content quarter-turns (odd turns swap width/height). Does not touch layout.
 * FullSource: crop uses cropSourceSize → image size mapping.
 *
 * Returns @p src unchanged when state is identity or src is null.
 */
QImage applyContentToImage(const QImage &src, const WorkspaceItemState &state,
                           PixelKind kind);

/** True when any content field differs from identity (crop / bake / grade). */
bool hasContentAppearance(const WorkspaceItemState &state);

/** Live content-meta (tileContentXform) counts as content mods (crop / flips). */
bool liveItemHasContentMods(const ContentXform::Value &live);

/**
 * True when content orientation changes the display aspect relative to the
 * on-disk / probe size (odd quarter-turns). Used by soft install to fit Image
 * mode without adopting soft dimensions into permanent layout geometry.
 */



/**
 * OR-merge content flags from @p flags into empty fields of @p appearance only
 * (never clears store). Used when there is no applied ContentXform fingerprint
 * and ItemWorld sparse Crop/ContentBake (or a derived ContentXform::Value) may
 * still hold orient/crop that the fat DTO slot lacks.
 */
void fillEmptyContentFlags(WorkspaceItemState &appearance,
                           const ContentXform::Value &flags);

/** Copy of @p state with crop fields cleared (orient-only layout / draft). */
WorkspaceItemState withoutCrop(const WorkspaceItemState &state);

/**
 * Drop content bake ops (flips, quarter turns, crop); keep colour grade and
 * non-content placement fields.
 */
WorkspaceItemState clearedContentOps(const WorkspaceItemState &state);

} // namespace SessionAppearance

/**
 * Per–session-image content appearance (crop, content flips, quarter turns,
 * colour grade). Identity is SessionImageId — never path (IDENTITY.md).
 *
 * Apply content onto pixels only via materializeDisplay / applyContentToImage
 * (order is fixed). Prefer ImageView::installDisplayPixels or
 * rematerializeItemContent when attaching to items so SoftPreview vs FullSource
 * and multi-MP async stay consistent.
 *
 * Path-keyed maps on ImageView remain legacy fallbacks for unbound tiles only.
 */
/**
 * Per-session seed-attempt book (Stage 4b residual).
 *
 * Formerly also held a fat WorkspaceItemState dual-write mirror. Content
 * appearance lives in ItemWorld sparse tables; project/clipboard assemble via
 * appearanceValue. This store only tracks "XDG/path seed already attempted"
 * so paint paths do not re-hit locatorId every frame.
 */
class SessionAppearanceStore
{
public:
    void clear();
    int size() const { return m_seedAttempted.size(); }
    bool isEmpty() const { return m_seedAttempted.isEmpty(); }

    /**
     * Durable XDG seed attempts (once per session id). Prevents archive/miss
     * paths from re-hitting locatorId on every paint.
     */
    bool seedAttempted(SessionImageId id) const;
    void markSeedAttempted(SessionImageId id);
    void clearSeedAttempted(SessionImageId id);
    /** Drop seed flag for @p id (session row remove). */
    void remove(SessionImageId id);

private:
    QSet<SessionImageId> m_seedAttempted;
};

#endif // SESSIONAPPEARANCE_H
