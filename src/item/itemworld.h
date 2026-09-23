// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMWORLD_H
#define ITEMWORLD_H

#include "imagesizebook.h"
#include "item/itemcomponents.h"
#include "contentxform.h"
#include "pathitemstatebook.h"

#include <QHash>
#include <QString>

/**
 * Facade over per-item stores (Phase 7 / REFACTOR.md Stage 4b).
 *
 * Owns sparse tables (Crop, Attention, ContentBake, Color, Placement) and
 * non-owning links to PathItemStateBook and ImageSizeBook.
 *
 * Persistence tags (REFACTOR.md Stage 4b):
 *   Persistent — SessionDocument paths/ids; sparse Crop / ContentBake / Color /
 *     Attention; Placement (Workspace-scoped pose); path book only for unbound.
 *   Derived only — applied ContentXform (runtime table + ImageItem mirror),
 *     tile LOD, soft pixels, sessionIndex.
 *   Seed book — SessionSeedBook (seedAttempted only; not content).
 *
 * setCrop / setColor / setAppearance write sparse tables only.
 * appearanceValue assembles WorkspaceItemState for project/clipboard.
 *
 * Entity key for content appearance: SessionImageId (IDENTITY.md).
 */
class ItemWorld
{
public:
    void bindPathBook(PathItemStateBook *book) { m_pathBook = book; }
    void bindSizeBook(ImageSizeBook *book) { m_sizeBook = book; }

    bool hasPathBookBound() const { return m_pathBook != nullptr; }
    bool hasSizeBookBound() const { return m_sizeBook != nullptr; }

    PathItemStateBook &pathBook()
    {
        Q_ASSERT(m_pathBook);
        return *m_pathBook;
    }
    const PathItemStateBook &pathBook() const
    {
        Q_ASSERT(m_pathBook);
        return *m_pathBook;
    }

    ImageSizeBook &sizeBook()
    {
        Q_ASSERT(m_sizeBook);
        return *m_sizeBook;
    }
    const ImageSizeBook &sizeBook() const
    {
        Q_ASSERT(m_sizeBook);
        return *m_sizeBook;
    }

    /**
     * True when any persistent sparse table has a row for @p id.
     */
    bool hasDurableAppearance(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return false;
        }
        return hasCrop(id) || hasContentBake(id) || hasColor(id)
            || hasAttention(id) || hasPlacement(id);
    }

    /**
     * Alias for hasDurableAppearance. Prefer hasDurableAppearance in new code.
     */
    bool hasAppearance(SessionImageId id) const
    {
        return hasDurableAppearance(id);
    }

    /**
     * Store-read authority: assemble WorkspaceItemState from sparse tables only
     * (Stage 4b). Same policy as ImageView::sessionAppearanceValue.
     */
    WorkspaceItemState appearanceValue(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        WorkspaceItemState s;
        s.sessionId = id;
        if (hasCrop(id)) {
            const ItemComponents::Crop c = crop(id);
            s.hasCrop = !c.isEmpty();
            s.cropRect = c.rect;
            s.cropSourceSize = c.sourceSize;
            s.cropRotation = c.rotation;
        }
        if (hasContentBake(id)) {
            const ItemComponents::ContentBake b = contentBake(id);
            s.contentHFlip = b.hFlip;
            s.contentVFlip = b.vFlip;
            s.contentQuarterTurns = b.quarterTurns;
        }
        if (hasColor(id)) {
            s.colorAdjust = color(id).grade;
        }
        if (hasAttention(id)) {
            const ItemComponents::Attention a = attention(id);
            s.attentionPoints = a.points;
            s.syncAttentionPrimary();
        }
        if (hasPlacement(id)) {
            ItemComponents::applyPlacementToState(s, placement(id));
        }
        return s;
    }

    /**
     * Write sparse component tables from @p state (Stage 4b).
     * Content tables always sync. Placement: non-identity always writes; identity
     * pose does not clobber an existing Placement row (use setPlacement to clear).
     */
    void setAppearance(SessionImageId id, const WorkspaceItemState &state)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        WorkspaceItemState s = state;
        s.sessionId = id;
        syncComponentsFromState(id, s);
    }

    /**
     * Upsert content components present in @p state; never remove siblings.
     * Empty crop / identity bake / identity color / empty attention are skipped
     * (not treated as clears). Placement is never touched. Use for XDG seed and
     * orient bake paths that must not wipe attention or Workspace pose.
     */
    void mergeContentFromState(SessionImageId id, const WorkspaceItemState &state)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        const ItemComponents::Crop c = ItemComponents::cropFromState(state);
        if (!c.isEmpty()) {
            m_crops.insert(id, c);
        }
        const ItemComponents::Attention a = ItemComponents::attentionFromState(state);
        if (!a.isEmpty()) {
            m_attentions.insert(id, a);
        }
        const ItemComponents::ContentBake b = ItemComponents::contentBakeFromState(state);
        if (!b.isIdentity()) {
            m_contentBakes.insert(id, b);
        }
        const ItemComponents::Color col = ItemComponents::colorFromState(state);
        if (!col.isIdentity()) {
            m_colors.insert(id, col);
        }
    }

        /**
     * Remove crop / content-bake / color for @p id (Reset Content Appearance).
     * Attention and Placement are left intact.
     */
    void clearContentComponents(SessionImageId id)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_crops.remove(id);
        m_contentBakes.remove(id);
        m_colors.remove(id);
    }

/**
     * Runtime-only applied ContentXform fingerprint (Stage 2 residual).
     * Never project-persisted. Dual-written with ImageItem mid-edit authority;
     * keyed by SessionImageId so bound tiles share one fingerprint after
     * restamp. Unbound tiles keep ImageItem-only storage.
     */
    void setAppliedContentXform(SessionImageId id, const ContentXform::Value &x)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_appliedContentXforms.insert(id, x);
    }

    void clearAppliedContentXform(SessionImageId id)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_appliedContentXforms.remove(id);
    }

    void clearAllAppliedContentXforms()
    {
        m_appliedContentXforms.clear();
    }

    bool hasAppliedContentXform(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return false;
        }
        return m_appliedContentXforms.contains(id);
    }

    ContentXform::Value appliedContentXform(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_appliedContentXforms.value(id);
    }

    /**
     * Runtime-only live colour grade lag (Stage 2 residual host-side scratch).
     * Never project-persisted (distinct from durable Color sparse table).
     * Dual-written with ImageItem::m_colorAdjust when bound so host reads can
     * prefer ItemWorld; paint keeps the item mirror.
     */
    void setLiveColorLag(SessionImageId id, const ColorAdjustments &grade)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_liveColorLags.insert(id, grade);
    }

    bool hasLiveColorLag(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return false;
        }
        return m_liveColorLags.contains(id);
    }

    ColorAdjustments liveColorLag(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_liveColorLags.value(id);
    }

    void removeAppearance(SessionImageId id)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_crops.remove(id);
        m_attentions.remove(id);
        m_contentBakes.remove(id);
        m_colors.remove(id);
        m_placements.remove(id);
        m_appliedContentXforms.remove(id);
        m_liveColorLags.remove(id);
    }

    /** Clear sparse component tables and runtime applied / live-color lag. */
    void clearAppearance()
    {
        m_crops.clear();
        m_attentions.clear();
        m_contentBakes.clear();
        m_colors.clear();
        m_placements.clear();
        m_appliedContentXforms.clear();
        m_liveColorLags.clear();
    }

    /** Sparse crop table (Stage 1). Empty crop ⇒ absent. */
    ItemComponents::Crop crop(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_crops.value(id);
    }

    void setCrop(SessionImageId id, const ItemComponents::Crop &c)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        if (c.isEmpty()) {
            m_crops.remove(id);
        } else {
            m_crops.insert(id, c);
        }
    }

    bool hasCrop(SessionImageId id) const { return !crop(id).isEmpty(); }

    /** Sparse attention table (Stage 1). Empty points ⇒ absent. */
    ItemComponents::Attention attention(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_attentions.value(id);
    }

    void setAttention(SessionImageId id, const ItemComponents::Attention &a)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        if (a.isEmpty()) {
            m_attentions.remove(id);
        } else {
            m_attentions.insert(id, a);
        }
    }

    bool hasAttention(SessionImageId id) const { return !attention(id).isEmpty(); }

    /** Sparse content-bake table (Stage 1). Identity bake ⇒ absent. */
    ItemComponents::ContentBake contentBake(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_contentBakes.value(id);
    }

    void setContentBake(SessionImageId id, const ItemComponents::ContentBake &b)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        if (b.isIdentity()) {
            m_contentBakes.remove(id);
        } else {
            m_contentBakes.insert(id, b);
        }
    }

    bool hasContentBake(SessionImageId id) const
    {
        return !contentBake(id).isIdentity();
    }

    /** True when sparse contentBake or crop drives orient/layout (not placement-only). */
    bool hasContentOrient(SessionImageId id) const
    {
        return hasContentBake(id) || hasCrop(id);
    }

    /** Sparse color-grade table (Stage 1). Identity grade ⇒ absent. */
    ItemComponents::Color color(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_colors.value(id);
    }

    void setColor(SessionImageId id, const ItemComponents::Color &c)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        if (c.isIdentity()) {
            m_colors.remove(id);
        } else {
            m_colors.insert(id, c);
        }
    }

    bool hasColor(SessionImageId id) const { return !color(id).isIdentity(); }

    /**
     * Sparse placement table (Stage 2 start). Always present once setAppearance
     * or setPlacement has written an id (including identity pose).
     */
    ItemComponents::Placement placement(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        return m_placements.value(id);
    }

    void setPlacement(SessionImageId id, const ItemComponents::Placement &pl)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        m_placements.insert(id, pl);
    }

    bool hasPlacement(SessionImageId id) const
    {
        return id != kInvalidSessionImageId && m_placements.contains(id);
    }

    /** Path-keyed placement / unbound fallback (not identity). */
    const WorkspaceItemState *getPathState(const QString &path) const
    {
        if (!m_pathBook || path.isEmpty()) {
            return nullptr;
        }
        return m_pathBook->get(path);
    }

    /**
     * Path-keyed store for **unbound** tiles only (content + placement).
     * IDENTITY: when @p state carries a bound SessionImageId, this is a no-op —
     * crop/bake/color/pose for bound images live in sparse ItemWorld tables
     * (and XDG). Callers must branch on sessionId before writing path state.
     */
    void setPathState(const QString &path, const WorkspaceItemState &state)
    {
        if (!m_pathBook || path.isEmpty()) {
            return;
        }
        // Bound identity must not touch the path book (duplicates share a path).
        if (state.sessionId != kInvalidSessionImageId) {
            return;
        }
        m_pathBook->set(path, state);
    }

    QSize knownSize(const QString &path) const
    {
        if (!m_sizeBook) {
            return {};
        }
        return m_sizeBook->known(path);
    }

    bool noteDefinitiveSize(const QString &path, const QSize &size)
    {
        if (!m_sizeBook) {
            return false;
        }
        return m_sizeBook->noteDefinitive(path, size);
    }

    int cropCount() const { return m_crops.size(); }
    int attentionCount() const { return m_attentions.size(); }
    int contentBakeCount() const { return m_contentBakes.size(); }
    int colorCount() const { return m_colors.size(); }
    int placementCount() const { return m_placements.size(); }

private:
    void syncComponentsFromState(SessionImageId id, const WorkspaceItemState &state)
    {
        const ItemComponents::Crop c = ItemComponents::cropFromState(state);
        if (c.isEmpty()) {
            m_crops.remove(id);
        } else {
            m_crops.insert(id, c);
        }
        const ItemComponents::Attention a = ItemComponents::attentionFromState(state);
        if (a.isEmpty()) {
            m_attentions.remove(id);
        } else {
            m_attentions.insert(id, a);
        }
        const ItemComponents::ContentBake b = ItemComponents::contentBakeFromState(state);
        if (b.isIdentity()) {
            m_contentBakes.remove(id);
        } else {
            m_contentBakes.insert(id, b);
        }
        const ItemComponents::Color col = ItemComponents::colorFromState(state);
        if (col.isIdentity()) {
            m_colors.remove(id);
        } else {
            m_colors.insert(id, col);
        }
        // Placement is Workspace-scoped and optional. Content-only setAppearance
        // (crop/bake/color with identity pose) must not clobber an existing pose
        // with origin/scale-1. Non-identity pose always writes; identity only
        // seeds when no placement row exists yet. Clear pose via setPlacement.
        const ItemComponents::Placement pl = ItemComponents::placementFromState(state);
        if (!pl.isIdentity()) {
            m_placements.insert(id, pl);
        }
        // else: identity pose and row already present — keep existing placement
    }

    // Linked stores (not owned).
    PathItemStateBook *m_pathBook = nullptr;        // persistent unbound only
    ImageSizeBook *m_sizeBook = nullptr;            // derived (host probe)

    // Sparse component tables — persistent (project/clipboard via appearanceValue).
    QHash<SessionImageId, ItemComponents::Crop> m_crops;
    QHash<SessionImageId, ItemComponents::Attention> m_attentions;
    QHash<SessionImageId, ItemComponents::ContentBake> m_contentBakes;
    QHash<SessionImageId, ItemComponents::Color> m_colors;
    // Workspace pose; optional per id on disk (hasWorkspacePose).
    QHash<SessionImageId, ItemComponents::Placement> m_placements;
    /** Runtime-only; not in appearanceValue / project save. */
    QHash<SessionImageId, ContentXform::Value> m_appliedContentXforms;
    /** Runtime-only live grade lag; not durable Color. */
    QHash<SessionImageId, ColorAdjustments> m_liveColorLags;
};

#endif // ITEMWORLD_H
