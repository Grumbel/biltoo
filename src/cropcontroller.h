// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPCONTROLLER_H
#define CROPCONTROLLER_H

#include "cropsession.h"

class ImageView;

/**
 * Crop-mode collaborator for ImageView (Phase 6 Tier 2a).
 *
 * Owns CropSession draft state. Enter/apply/leave orchestration remains on
 * ImageView until Tier 2b moves methods onto this type with a narrow host.
 */
class CropController
{
public:
    explicit CropController(ImageView *view);

    ImageView *view() const { return m_view; }

    CropSession &session() { return m_crop; }
    const CropSession &session() const { return m_crop; }

    bool active() const { return m_crop.active(); }

private:
    ImageView *m_view = nullptr; // not owned
    CropSession m_crop;
};

#endif // CROPCONTROLLER_H
