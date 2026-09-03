// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONAPPEARANCE_H
#define SESSIONAPPEARANCE_H

#include "imageview_types.h"

#include <QHash>
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
 */
namespace SessionAppearance {

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
 */
void applyContentToItem(ImageItem *item, const WorkspaceItemState &state);

/** True when any content field differs from identity (crop / bake / grade). */
bool hasContentAppearance(const WorkspaceItemState &state);

} // namespace SessionAppearance

/**
 * Per–session-image content appearance (crop, content flips, quarter turns,
 * colour grade). Identity is SessionImageId — never path (IDENTITY.md).
 *
 * Apply content onto a decoded ImageItem only via
 * SessionAppearance::applyContentToItem — do not fork crop/bake/grade order
 * at call sites.
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
