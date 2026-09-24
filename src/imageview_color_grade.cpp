// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Interactive colour grade + deferred commit — thin routers to ImageController.

#include "imageview.h"
#include "session/sessionappearance.h"

void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_image.scheduleColorAdjustCommit(sid, path);
}

void ImageView::flushColorAdjustCommit()
{
    m_image.flushColorAdjustCommit();
}

void ImageView::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    m_image.setTargetColorAdjustments(adj);
}
