// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "imageitem.h"

void ImageView::setImageModeSoftProvider(ImageModeSoftProvider provider)
{
    m_imageModeSoftProvider = std::move(provider);
}


bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    for (const ImageItem *ii : m_items) {
        if (ii && ii->path() == path) {
            return true;
        }
    }
    return false;
}




