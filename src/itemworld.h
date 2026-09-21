// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMWORLD_H
#define ITEMWORLD_H

#include "imagesizebook.h"
#include "itemcomponents.h"
#include "pathitemstatebook.h"
#include "sessionappearance.h"

#include <QHash>
#include <QString>

/**
 * Facade over per-item stores (Phase 7 / REFACTOR.md Stage 4).
 *
 * Owns sparse tables (Crop, Attention, ContentBake, Color, Placement) and
 * non-owning links to SessionAppearanceStore, PathItemStateBook, ImageSizeBook.
 *
 * Persistence tags (REFACTOR.md Stage 4 design):
 *   Persistent — SessionDocument paths/ids; sparse Crop / ContentBake / Color /
 *     Attention; Placement (Workspace-scoped pose); path book only for unbound.
 *   Derived mirror — fat WorkspaceItemState (dual-write until Stage 4b format).
 *   Derived only — applied ContentXform, tile LOD, soft pixels, sessionIndex.
 *
 * Writes: setAppearance / setCrop / setColor / … dual-write sparse + fat DTO so
 * project serialization and legacy readers stay consistent.
 * Reads: prefer ImageView::sessionAppearanceValue (sparse-first choke point) or
 * component accessors (crop(), color(), …). ImageItem is a render/hit-test proxy
 * — not a parallel live database.
 *
 * Project/clipboard boundary (Stage 4a): build DTO from appearanceValue /
 * sessionAppearanceValue (+ live pose when needed), never from a raw fat
 * pointer alone. Load always goes through setAppearance (dual-fills sparse).
 *
 * Entity key for content appearance: SessionImageId (IDENTITY.md).
 */
class ItemWorld
{
public:
    void bindAppearance(SessionAppearanceStore *store) { m_appearance = store; }
    void bindPathBook(PathItemStateBook *book) { m_pathBook = book; }
    void bindSizeBook(ImageSizeBook *book) { m_sizeBook = book; }

    bool hasAppearanceBound() const { return m_appearance != nullptr; }
    bool hasPathBookBound() const { return m_pathBook != nullptr; }
    bool hasSizeBookBound() const { return m_sizeBook != nullptr; }

    SessionAppearanceStore &appearance()
    {
        Q_ASSERT(m_appearance);
        return *m_appearance;
    }
    const SessionAppearanceStore &appearance() const
    {
        Q_ASSERT(m_appearance);
        return *m_appearance;
    }

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
     * Fat dual-write mirror pointer (Stage 4b will drop this as authority).
     * For store reads prefer appearanceValue / hasDurableAppearance — never
     * treat a null getAppearance as "no durable content".
     */
    const WorkspaceItemState *getAppearance(SessionImageId id) const
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return nullptr;
        }
        return m_appearance->get(id);
    }

    /** Fat DTO row present only — use hasDurableAppearance for store-read gates. */
    bool hasAppearance(SessionImageId id) const
    {
        return getAppearance(id) != nullptr;
    }

    /**
     * True when any persistent table has a row for @p id (fat DTO and/or sparse
     * Crop / ContentBake / Color / Placement). Prefer this over hasAppearance
     * alone when deciding whether sessionAppearanceValue is meaningful — sparse
     * dual-write lag must not look like "no appearance".
     */
    bool hasDurableAppearance(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return false;
        }
        return hasAppearance(id) || hasCrop(id) || hasContentBake(id)
            || hasColor(id) || hasPlacement(id);
    }

    /**
     * Store-read authority: fat DTO with sparse Crop/ContentBake/Color/Attention/
     * Placement overlaid when present (same policy as ImageView::sessionAppearanceValue).
     * Prefer this (or sessionAppearanceValue) over dereferencing getAppearance().
     */
    WorkspaceItemState appearanceValue(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        WorkspaceItemState s;
        if (const WorkspaceItemState *p = getAppearance(id)) {
            s = *p;
        }
        // Always surface the lookup key (sparse-only or partial dual-write).
        s.sessionId = id;
        // Sparse tables win when a partial component write ran without a full
        // setAppearance refresh of every fat field.
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
        s.colorAdjust = color(id).grade;
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

    void setAppearance(SessionImageId id, const WorkspaceItemState &state)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        WorkspaceItemState s = state;
        s.sessionId = id;
        m_appearance->set(id, s);
        syncComponentsFromState(id, s);
    }

    void removeAppearance(SessionImageId id)
    {
        if (id == kInvalidSessionImageId) {
            return;
        }
        if (m_appearance) {
            m_appearance->remove(id);
        }
        m_crops.remove(id);
        m_attentions.remove(id);
        m_contentBakes.remove(id);
        m_colors.remove(id);
        m_placements.remove(id);
    }

    /** Clear DTO store and sparse component tables. */
    void clearAppearance()
    {
        if (m_appearance) {
            m_appearance->clear();
        }
        m_crops.clear();
        m_attentions.clear();
        m_contentBakes.clear();
        m_colors.clear();
        m_placements.clear();
    }

    /** Sparse crop table (Stage 1). Empty crop ⇒ absent. */
    ItemComponents::Crop crop(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        const auto it = m_crops.constFind(id);
        if (it != m_crops.cend()) {
            return it.value();
        }
        // Fallback when DTO was written without going through setAppearance.
        if (const WorkspaceItemState *s = getAppearance(id)) {
            return ItemComponents::cropFromState(*s);
        }
        return {};
    }

    void setCrop(SessionImageId id, const ItemComponents::Crop &c)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        if (c.isEmpty()) {
            m_crops.remove(id);
        } else {
            m_crops.insert(id, c);
        }
        WorkspaceItemState s = dtoForWrite(id);
        ItemComponents::applyCropToState(s, c);
        m_appearance->set(id, s);
    }

    bool hasCrop(SessionImageId id) const { return !crop(id).isEmpty(); }

    /** Sparse attention table (Stage 1). Empty points ⇒ absent. */
    ItemComponents::Attention attention(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        const auto it = m_attentions.constFind(id);
        if (it != m_attentions.cend()) {
            return it.value();
        }
        if (const WorkspaceItemState *s = getAppearance(id)) {
            return ItemComponents::attentionFromState(*s);
        }
        return {};
    }

    void setAttention(SessionImageId id, const ItemComponents::Attention &a)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        if (a.isEmpty()) {
            m_attentions.remove(id);
        } else {
            m_attentions.insert(id, a);
        }
        WorkspaceItemState s = dtoForWrite(id);
        ItemComponents::applyAttentionToState(s, a);
        m_appearance->set(id, s);
    }

    bool hasAttention(SessionImageId id) const { return !attention(id).isEmpty(); }

    /** Sparse content-bake table (Stage 1). Identity bake ⇒ absent. */
    ItemComponents::ContentBake contentBake(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        const auto it = m_contentBakes.constFind(id);
        if (it != m_contentBakes.cend()) {
            return it.value();
        }
        if (const WorkspaceItemState *s = getAppearance(id)) {
            return ItemComponents::contentBakeFromState(*s);
        }
        return {};
    }

    void setContentBake(SessionImageId id, const ItemComponents::ContentBake &b)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        if (b.isIdentity()) {
            m_contentBakes.remove(id);
        } else {
            m_contentBakes.insert(id, b);
        }
        WorkspaceItemState s = dtoForWrite(id);
        ItemComponents::applyContentBakeToState(s, b);
        m_appearance->set(id, s);
    }

    bool hasContentBake(SessionImageId id) const
    {
        return !contentBake(id).isIdentity();
    }

    /** Sparse color-grade table (Stage 1). Identity grade ⇒ absent. */
    ItemComponents::Color color(SessionImageId id) const
    {
        if (id == kInvalidSessionImageId) {
            return {};
        }
        const auto it = m_colors.constFind(id);
        if (it != m_colors.cend()) {
            return it.value();
        }
        if (const WorkspaceItemState *s = getAppearance(id)) {
            return ItemComponents::colorFromState(*s);
        }
        return {};
    }

    void setColor(SessionImageId id, const ItemComponents::Color &c)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        if (c.isIdentity()) {
            m_colors.remove(id);
        } else {
            m_colors.insert(id, c);
        }
        WorkspaceItemState s = dtoForWrite(id);
        ItemComponents::applyColorToState(s, c);
        m_appearance->set(id, s);
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
        const auto it = m_placements.constFind(id);
        if (it != m_placements.cend()) {
            return it.value();
        }
        if (const WorkspaceItemState *s = getAppearance(id)) {
            return ItemComponents::placementFromState(*s);
        }
        return {};
    }

    void setPlacement(SessionImageId id, const ItemComponents::Placement &pl)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        m_placements.insert(id, pl);
        WorkspaceItemState s = dtoForWrite(id);
        ItemComponents::applyPlacementToState(s, pl);
        m_appearance->set(id, s);
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
     * Path-keyed store for unbound tiles and orient/flip path hints.
     * IDENTITY: when @p state carries a bound SessionImageId, crop fields are
     * stripped before write — crop is id-keyed only (duplicates share a path).
     */
    void setPathState(const QString &path, const WorkspaceItemState &state)
    {
        if (!m_pathBook || path.isEmpty()) {
            return;
        }
        if (state.sessionId != kInvalidSessionImageId
            && (state.hasCrop || !state.cropRect.isEmpty())) {
            WorkspaceItemState s = state;
            s.hasCrop = false;
            s.cropRect = QRect();
            s.cropRotation = 0.0;
            s.cropSourceSize = QSize();
            m_pathBook->set(path, s);
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
    /** Fat DTO base for sparse dual-write: copy existing or empty, always stamp id. */
    WorkspaceItemState dtoForWrite(SessionImageId id) const
    {
        WorkspaceItemState s;
        if (m_appearance) {
            if (const WorkspaceItemState *cur = m_appearance->get(id)) {
                s = *cur;
            }
        }
        s.sessionId = id;
        return s;
    }

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
        // Placement: always dual-write (identity pose is still a placed item).
        m_placements.insert(id, ItemComponents::placementFromState(state));
    }

    // Linked stores (not owned). Fat DTO is a derived dual-write mirror until 4b.
    SessionAppearanceStore *m_appearance = nullptr; // derived mirror + legacy
    PathItemStateBook *m_pathBook = nullptr;        // persistent unbound only
    ImageSizeBook *m_sizeBook = nullptr;            // derived (host probe)

    // Sparse component tables — persistent (project/clipboard via appearanceValue).
    QHash<SessionImageId, ItemComponents::Crop> m_crops;
    QHash<SessionImageId, ItemComponents::Attention> m_attentions;
    QHash<SessionImageId, ItemComponents::ContentBake> m_contentBakes;
    QHash<SessionImageId, ItemComponents::Color> m_colors;
    // Workspace pose; optional per id on disk (hasWorkspacePose).
    QHash<SessionImageId, ItemComponents::Placement> m_placements;
};

#endif // ITEMWORLD_H
