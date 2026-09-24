// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPAPPEARANCECOMMAND_H
#define CROPAPPEARANCECOMMAND_H

#include "imageview_types.h"

#include <QImage>
#include <QUndoCommand>

class ImageItem;
class ImageView;

/**
 * Undo/redo one crop Apply appearance snapshot pair (enter → after).
 * Owned by QUndoStack; applies via CropController::applyCropAppearance.
 */
class CropAppearanceCommand : public QUndoCommand
{
public:
    CropAppearanceCommand(ImageView *view, ImageItem *item, const QImage &beforeSrc,
                          const QImage &afterSrc, const WorkspaceItemState &beforeSt,
                          const WorkspaceItemState &afterSt, const QString &text);

    void undo() override;
    void redo() override;

private:
    void apply(const QImage &src, const WorkspaceItemState &st);

    ImageView *m_view = nullptr;
    ImageItem *m_item = nullptr;
    QImage m_beforeSrc;
    QImage m_afterSrc;
    WorkspaceItemState m_beforeSt;
    WorkspaceItemState m_afterSt;
};

#endif // CROPAPPEARANCECOMMAND_H
