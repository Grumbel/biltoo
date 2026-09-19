// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipelinecontroller.h"

#include "tile_load_coordinator.h"

DisplayPipelineController::DisplayPipelineController(ImageView *view)
    : m_view(view)
{
}
