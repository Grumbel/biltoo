// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Rematerialize host forwards — implementation on DisplayPipelineController.

#include "imageview.h"
#include "imageitem.h"
#include "session/sessionappearance.h"

void ImageView::attachDisplaySample(ImageItem *item, const QImage &display,
                                      const WorkspaceItemState &want,
                                      SessionAppearance::PixelKind kind)
{
    m_displayPipeline.attachDisplaySample(item, display, want, kind);
}

void ImageView::rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want)
{
    m_displayPipeline.rematerializeItemContent(item, want);
}

void ImageView::applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &wantIn)
{
    m_displayPipeline.applyContentLayoutSize(item, wantIn);
}

void ImageView::scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                                 const WorkspaceItemState &want)
{
    m_displayPipeline.scheduleAsyncHostRematerialize(path, sid, want);
}

void ImageView::clearStaleAppliedFingerprintIfNeeded(ImageItem *item)
{
    m_displayPipeline.clearStaleAppliedFingerprintIfNeeded(item);
}

void ImageView::rematerializeGalleryItemFromStore(ImageItem *item)
{
    m_displayPipeline.rematerializeGalleryItemFromStore(item);
}
