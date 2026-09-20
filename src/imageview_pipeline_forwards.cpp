// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView → DisplayPipelineController forwards (Tier 5).
// Product entry points that MainWindow / chrome still call on ImageView.

#include "imageview.h"

bool ImageView::loadImage(const QString &path)
{
    return m_displayPipeline.loadImage(path);
}
