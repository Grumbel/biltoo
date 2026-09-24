// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Content bake host forwards — implementation on DisplayPipelineController.

#include "imageview.h"
#include "imageitem.h"

void ImageView::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    m_displayPipeline.bakeItemRotate90(item, quarterTurns);
}

void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    m_displayPipeline.bakeItemFlip(item, horizontal, vertical);
}
