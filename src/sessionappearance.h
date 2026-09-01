// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONAPPEARANCE_H
#define SESSIONAPPEARANCE_H

#include "imageview_types.h"

#include <QRect>
#include <QSize>

class ImageItem;

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
 * Apply state.hasCrop / cropRect onto @p item's full source pixels.
 * Does not apply content flips or quarter turns.
 */
void applyCrop(ImageItem *item, const WorkspaceItemState &state);

} // namespace SessionAppearance

/**
 * Why Gallery may repack. Decode, delete, and view resize are not reasons.
 * applyLayout(reason) is the only pack entry point for Gallery.
 */
enum class GalleryPackReason {
    ExplicitLayout, /**< Layout toolbar / menu while already in Gallery */
    EnterGallery,   /**< Entering Gallery or rebuilding from session list */
    Reload,         /**< F5 / explicit reload with relayout */
    ContentChange,  /**< Content flip/rotate changed tile aspect for pack */
    SessionMutate,  /**< Intentional add/duplicate that must show new tiles */
};

#endif // SESSIONAPPEARANCE_H
