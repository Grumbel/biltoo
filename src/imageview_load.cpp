// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "imageitem.h"

int ImageView::pathOrderOccurrences(const QString &path) const
{
    // View-local multiplicity only (Gallery pack / LoadAdd).
    // clearPathOrder() zeros the book without wiping SessionDocument — consulting
    // the document here would recreate session tiles on a blank Workspace.
    return currentPackOrder().countPathOccurrences(path);
}

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

QSize ImageView::viewportWidgetSize() const
{
    if (const QWidget *vp = viewport()) {
        return vp->size();
    }
    return {};
}

qreal ImageView::prefetchDevicePixelRatio() const
{
    if (const QWidget *vp = viewport()) {
        return vp->devicePixelRatioF();
    }
    return 1.0;
}

bool ImageView::tilePrefetchNavHot() const
{
    return m_slideshow.hud().isNavHot();
}

