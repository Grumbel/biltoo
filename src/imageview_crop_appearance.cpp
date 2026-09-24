// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop appearance store/restore/apply — thin routers to CropController.

#include "imageview.h"

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

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    m_cropCtrl.applyCropAppearance(item, src, state);
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                        ImageItem *item, const QImage &preferredDisplay,
                                        bool hasCrop)
{
    m_cropCtrl.emitCropApplyAppearance(sid, path, item, preferredDisplay, hasCrop);
}
