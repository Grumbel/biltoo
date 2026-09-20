// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMWORLD_H
#define ITEMWORLD_H

#include "imagesizebook.h"
#include "pathitemstatebook.h"
#include "sessionappearance.h"

#include <QString>

/**
 * Stage 0 facade over the per-item stores (Phase 7 / REFACTOR.md).
 *
 * No storage of its own — non-owning pointers to the live SessionAppearanceStore
 * (SessionDocument), PathItemStateBook, and ImageSizeBook. Later stages move
 * tables *behind* this type; call sites should prefer ItemWorld accessors so
 * those moves stay internal.
 *
 * Entity key for content appearance: SessionImageId (IDENTITY.md).
 * Path remains decode source + Workspace unbound placement key only.
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

    /** Id-keyed content appearance (crop / orient / grade). */
    const WorkspaceItemState *getAppearance(SessionImageId id) const
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return nullptr;
        }
        return m_appearance->get(id);
    }

    void setAppearance(SessionImageId id, const WorkspaceItemState &state)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        m_appearance->set(id, state);
    }

    void removeAppearance(SessionImageId id)
    {
        if (!m_appearance || id == kInvalidSessionImageId) {
            return;
        }
        m_appearance->remove(id);
    }

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

private:
    SessionAppearanceStore *m_appearance = nullptr;
    PathItemStateBook *m_pathBook = nullptr;
    ImageSizeBook *m_sizeBook = nullptr;
};

#endif // ITEMWORLD_H
