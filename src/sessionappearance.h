// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONAPPEARANCE_H
#define SESSIONAPPEARANCE_H

#include "imageview_types.h"

#include <QHash>
#include <QImage>
#include <QRect>
#include <QSize>

class ImageItem;

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
 * Order matches applyContentToItem / applyContentToImage:
 *   1. Session crop (cropRect in cropSourceSize → @p sourceSize)
 *   2. Content horizontal / vertical flip about the working size
 *   3. Content quarter-turns clockwise (0..3), same matrix as bakeRotate90
 *
 * @p sourceSize is the unoriented full-page raster size (native / probe).
 * Returns empty if the rect misses the crop or sizes are invalid.
 */
QRectF mapSourceRectToContentDisplay(const QRectF &sourceRect, const QSize &sourceSize,
                                     const WorkspaceItemState &state);

/**
 * Apply state.hasCrop / cropRect onto @p item's full source pixels.
 * Does not apply content flips or quarter turns.
 */
void applyCrop(ImageItem *item, const WorkspaceItemState &state);

/**
 * Single entry point for session-image *content* appearance on a decoded item.
 *
 * Order (must stay consistent everywhere):
 *   1. Crop (from full on-disk / current source pixels)
 *   2. Content flips + quarter turns (baked into source pixels)
 *   3. Session crop / content-flip chrome flags
 *   4. Non-destructive colour grade (display only)
 *
 * Does **not** touch placement (pos / scale / free tilt / opacity / z / item flips).
 * Callers must have already set full (or post-decode) source pixels on @p item.
 *
 * Prefer ImageView::installDisplayPixels for new install sites so raw vs baked
 * stays explicit.
 */
void applyContentToItem(ImageItem *item, const WorkspaceItemState &state);

/**
 * After content orientation is applied (pixels and/or flags), ensure layout
 * geometry (intrinsic size / offset) matches content aspect. Odd quarter-turns
 * transpose intrinsic when it still has the pre-rotate aspect — Gallery pack,
 * selection AABB, and Workspace footprint stay aligned with the pixels.
 *
 * Does not change placement scale/pos; only imageSize() basis.
 */
void syncItemLayoutToContentOrientation(ImageItem *item,
                                        const WorkspaceItemState &state);

/**
 * Bake session content appearance into a QImage (no ImageItem).
 *
 * Same order as applyContentToItem. Used for filmstrip overrides, slideshow
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

/**
 * True when content orientation changes the display aspect relative to the
 * on-disk / probe size (odd quarter-turns). Used by soft install to fit Image
 * mode without adopting soft dimensions into permanent layout geometry.
 */
bool contentSwapsAspect(const WorkspaceItemState &state);

} // namespace SessionAppearance

/**
 * Per–session-image content appearance (crop, content flips, quarter turns,
 * colour grade). Identity is SessionImageId — never path (IDENTITY.md).
 *
 * Apply content onto a decoded ImageItem only via
 * SessionAppearance::applyContentToItem — do not fork crop/bake/grade order
 * at call sites. Prefer ImageView::installDisplayPixels when attaching raw
 * decode pixels so SoftPreview vs FullSource stays consistent.
 *
 * Path-keyed maps on ImageView remain legacy fallbacks for unbound tiles only.
 */
class SessionAppearanceStore
{
public:
    const WorkspaceItemState *get(SessionImageId id) const;
    WorkspaceItemState value(SessionImageId id) const;
    bool contains(SessionImageId id) const;
    void set(SessionImageId id, const WorkspaceItemState &state);
    void remove(SessionImageId id);
    void clear();
    int size() const { return m_byId.size(); }
    bool isEmpty() const { return m_byId.isEmpty(); }

private:
    QHash<SessionImageId, WorkspaceItemState> m_byId;
};

#endif // SESSIONAPPEARANCE_H
