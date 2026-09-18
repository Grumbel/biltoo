// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERSESSION_H
#define TEXTLAYERSESSION_H

#include "thumtoocache.h"

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

/**
 * PDF/page text layer overlay: search, rubber-band select, link hover tip.
 */
struct TextLayerSession {
    bool showRegions = false;
    ThumtooCache::PageTextLayer layer;
    QString layerPath;
    QString searchQuery;
    bool searchFuzzy = true;
    QVector<int> searchMatches;
    bool rubberbanding = false;
    QPoint rubberOrigin;
    QRect rubberRect;
    QVector<int> selectedRegions;
    QString linkHoverTip;

    bool needsLayer() const
    {
        return showRegions || !searchQuery.isEmpty();
    }

    void clearSearch()
    {
        searchQuery.clear();
        searchMatches.clear();
    }

    void clearSelection()
    {
        rubberbanding = false;
        rubberOrigin = {};
        rubberRect = {};
        selectedRegions.clear();
    }

    void clearLayer()
    {
        layer = {};
        layerPath.clear();
        clearSearch();
        clearSelection();
        linkHoverTip.clear();
    }
};

#endif // TEXTLAYERSESSION_H
