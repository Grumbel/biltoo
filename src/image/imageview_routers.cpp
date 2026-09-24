// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with image/ ownership.

#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"
#include <QScrollBar>

// --- from src/imageview_transform_actions.cpp ---

// --- from src/imageview_appearance_commit.cpp ---
void ImageView::commitItemSessionEdit(ImageItem *item)
{
    m_image.commitItemSessionEdit(item);
}



void ImageView::propagateSessionAppearanceToViews(ImageItem *item)
{
    m_image.propagateSessionAppearanceToViews(item);
}







// --- content appearance targets (from transform) ---



// --- from src/imageview_color_grade.cpp ---
void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_image.scheduleColorAdjustCommit(sid, path);
}


// --- from imageview_framing_image.cpp ---
void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(static_cast<const QGraphicsItem *>(item),
                      ViewTransform::kEnsureVisibleMargin,
                      ViewTransform::kEnsureVisibleMargin);
    }
}

qreal ImageView::viewScale() const
{
    return ViewTransform::scaleFrom(transform());
}

void ImageView::refreshScrollBarGeometry()
{
    m_shell.refreshScrollBarGeometry();
}

