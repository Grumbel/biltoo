// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page text / link regions — thin ImageView routers to TextLayerController.

#include "imageview.h"
#include "text/textsearchpolicy.h"

#include <QMouseEvent>

void ImageView::setShowTextRegions(bool on)
{
    m_textCtrl.setShowRegions(on);
}

void ImageView::refreshTextLayer()
{
    m_textCtrl.refresh();
}

bool ImageView::textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy)
{
    return TextSearchPolicy::matches(regionText, query, fuzzy);
}

void ImageView::setTextSearchFuzzy(bool on)
{
    m_textCtrl.setSearchFuzzy(on);
}

int ImageView::setTextSearchQuery(const QString &query)
{
    return m_textCtrl.setSearchQuery(query);
}

bool ImageView::hasTextLayer() const
{
    return m_textCtrl.hasLayer();
}

int ImageView::textLayerRegionCount() const
{
    return m_textCtrl.regionCount();
}

bool ImageView::hitTextLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const
{
    return m_textCtrl.hitLinkAt(viewPos, pageOut, uriOut);
}

QString ImageView::selectedText() const
{
    return m_textCtrl.selectedText();
}

void ImageView::clearTextSelection()
{
    m_textCtrl.clearSelection();
}

bool ImageView::copySelectedText()
{
    return m_textCtrl.copySelectedText();
}

