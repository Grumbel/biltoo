// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERSESSION_H
#define TEXTLAYERSESSION_H

#include "thumtoocache.h"
#include "viewtransform.h"

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
        endRubber();
        selectedRegions.clear();
    }

    /** Start rubber-band region select at @p origin (viewport). */
    void beginRubber(const QPoint &origin)
    {
        rubberbanding = true;
        rubberOrigin = origin;
        rubberRect = QRect(origin, QSize());
    }

    /** Update rubber rect from origin to @p pos. */
    void updateRubber(const QPoint &pos)
    {
        rubberRect = ViewTransform::rubberRect(rubberOrigin, pos);
    }

    /** End rubber-band gesture (keeps selectedRegions). */
    void endRubber()
    {
        rubberbanding = false;
        rubberOrigin = {};
        rubberRect = {};
    }

    void clearLayer()
    {
        layer = {};
        layerPath.clear();
        clearSearch();
        clearSelection();
        linkHoverTip.clear();
    }

    /** @return true when region overlay visibility changed. */
    bool setShowRegions(bool on)
    {
        if (showRegions == on) {
            return false;
        }
        showRegions = on;
        return true;
    }

    void setLayer(const ThumtooCache::PageTextLayer &l, const QString &path)
    {
        layer = l;
        layerPath = path;
    }

    void clearLayerPath() { layerPath.clear(); }

    void setLinkHoverTip(const QString &tip) { linkHoverTip = tip; }

    void clearLinkHoverTip() { linkHoverTip.clear(); }

    void setSearchFuzzy(bool on) { searchFuzzy = on; }

    void setSearchQuery(const QString &q) { searchQuery = q; }

    void setLayerContent(const ThumtooCache::PageTextLayer &l, const QString &path)
    {
        layer = l;
        layerPath = path;
    }

    void resetLayerContent()
    {
        layer = {};
        layerPath.clear();
    }
};

#endif // TEXTLAYERSESSION_H
