// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with crop/ ownership.

#include "imageview.h"

// --- from src/imageview_crop_appearance.cpp ---
void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    m_cropCtrl.storeCropAppearance(item, sid, s);
}

bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    return m_cropCtrl.loadRestoreCropAppearance(item, app, sidOut);
}

void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    m_cropCtrl.restoreSessionCropAppearance(item);
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                        ImageItem *item, const QImage &preferredDisplay,
                                        bool hasCrop)
{
    m_cropCtrl.emitCropApplyAppearance(sid, path, item, preferredDisplay, hasCrop);
}

