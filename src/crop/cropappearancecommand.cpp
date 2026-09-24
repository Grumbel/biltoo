// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crop/cropappearancecommand.h"

#include "imageitem.h"
#include "imageview.h"

CropAppearanceCommand::CropAppearanceCommand(
    ImageView *view, ImageItem *item, const QImage &beforeSrc, const QImage &afterSrc,
    const WorkspaceItemState &beforeSt, const WorkspaceItemState &afterSt,
    const QString &text)
    : m_view(view)
    , m_item(item)
    , m_beforeSrc(beforeSrc)
    , m_afterSrc(afterSrc)
    , m_beforeSt(beforeSt)
    , m_afterSt(afterSt)
{
    setText(text);
}

void CropAppearanceCommand::undo()
{
    apply(m_beforeSrc, m_beforeSt);
}

void CropAppearanceCommand::redo()
{
    apply(m_afterSrc, m_afterSt);
}

void CropAppearanceCommand::apply(const QImage &src, const WorkspaceItemState &st)
{
    if (!m_view || !m_item) {
        return;
    }
    m_view->hostCrop().applyCropAppearance(m_item, src, st);
}
