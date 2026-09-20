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
 * Facade over per-item stores (Phase 7 / REFACTOR.md).
 *
 * Stage 0: non-owning pointers to SessionAppearanceStore, PathItemStateBook,
 * ImageSizeBook.
 *
 * Stage 1: owned sparse tables (Crop, Attention, ContentBake, Color) dual-written
 * with the fat WorkspaceItemState DTO in the appearance store. Component
 * accessors are the preferred API for new code; setAppearance/getAppearance
 * keep DTO round-trips working for project file and undo.
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

    /** Id-keyed content appearance (DTO; dual-writes crop/attention tables). */
    const WorkspaceItemState *getAppearance(SessionImageId id) const
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return nullptr;
        }
        return m_appearance->get(id);
    }

    bool hasAppearance(SessionImageId id) const
    {
        return getAppearance(id) != nullptr;
    }

    WorkspaceItemState appearanceValue(SessionImageId id) const
    {
        if (const WorkspaceItemState *p = getAppearance(id)) {
            return *p;
        }
        return {};
    }

    void setAppearance(SessionImageId id, const WorkspaceItemState &state)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        m_appearance->set(id, state);
        syncComponentsFromState(id, state);
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
        WorkspaceItemState s;
        if (const WorkspaceItemState *cur = m_appearance->get(id)) {
            s = *cur;
        }
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
        WorkspaceItemState s;
        if (const WorkspaceItemState *cur = m_appearance->get(id)) {
            s = *cur;
        }
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
        WorkspaceItemState s;
        if (const WorkspaceItemState *cur = m_appearance->get(id)) {
            s = *cur;
        }
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
        WorkspaceItemState s;
        if (const WorkspaceItemState *cur = m_appearance->get(id)) {
            s = *cur;
        }
        ItemComponents::applyColorToState(s, c);
        m_appearance->set(id, s);
    }

    bool hasColor(SessionImageId id) const { return !color(id).isIdentity(); }

    /** Path-keyed placement / unbound fallback (not identity). */
    const WorkspaceItemState *getPathState(const QString &path) const
    {
        if (!m_pathBook || path.isEmpty()) {
            return nullptr;
        }
        return m_pathBook->get(path);
    }

    void setPathState(const QString &path, const WorkspaceItemState &state)
    {
        if (!m_pathBook || path.isEmpty()) {
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
    }

    SessionAppearanceStore *m_appearance = nullptr;
    PathItemStateBook *m_pathBook = nullptr;
    ImageSizeBook *m_sizeBook = nullptr;
    QHash<SessionImageId, ItemComponents::Crop> m_crops;
    QHash<SessionImageId, ItemComponents::Attention> m_attentions;
    QHash<SessionImageId, ItemComponents::ContentBake> m_contentBakes;
    QHash<SessionImageId, ItemComponents::Color> m_colors;
};

#endif // ITEMWORLD_H
