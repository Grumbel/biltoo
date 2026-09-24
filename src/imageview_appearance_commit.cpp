// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance commit, peer sync, copy, and content-reset.

#include "imageview.h"
#include "crop/cropsession.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "item/itemcomponents.h"





void ImageView::commitItemSessionEdit(ImageItem *item)
{
    m_image.commitItemSessionEdit(item);
}



void ImageView::propagateSessionAppearanceToViews(ImageItem *item)
{
    m_image.propagateSessionAppearanceToViews(item);
}



void ImageView::copySessionAppearance(SessionImageId fromId, SessionImageId toId)
{
    m_image.copySessionAppearance(fromId, toId);
}







// --- content appearance targets (from transform) ---

bool ImageView::targetHasContentAppearance() const
{
    return m_image.targetHasContentAppearance();
}



int ImageView::resetContentAppearanceForTargets()
{
    return m_image.resetContentAppearanceForTargets();
}


