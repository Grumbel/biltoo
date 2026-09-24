// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Content bake host forward — implementation on DisplayPipelineController.
// bakeItemRotate90 is pipeline-only (rotateContentByQuarterTurns calls it).

#include "imageview.h"
#include "imageitem.h"

void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    m_displayPipeline.bakeItemFlip(item, horizontal, vertical);
}
