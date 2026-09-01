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
 * Apply state.hasCrop / cropRect onto @p item's full source pixels.
 * Does not apply content flips or quarter turns.
 */
void applyCrop(ImageItem *item, const WorkspaceItemState &state);

} // namespace SessionAppearance

/**
 * Per–session-image content appearance (crop, content flips, quarter turns).
 * Identity is SessionImageId — never path (IDENTITY.md).
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
