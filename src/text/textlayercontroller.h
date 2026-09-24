// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERCONTROLLER_H
#define TEXTLAYERCONTROLLER_H

#include "text/textlayersession.h"
#include "host/thumtoocache.h"

#include <QPoint>
#include <QRectF>
#include <QString>

class ImageView;
class QPainter;
class QMouseEvent;

/**
 * Page text / link overlay collaborator (search, rubber-band, hit-test).
 * Owns TextLayerSession; ImageView keeps thin public/shell forwards.
 */
class TextLayerController
{
public:
    explicit TextLayerController(ImageView *view);

    TextLayerSession &session() { return m_session; }
    const TextLayerSession &session() const { return m_session; }

    void paintRubberBandOverlay(QPainter &painter);
    void setShowRegions(bool on);
    void refresh();
    void setSearchFuzzy(bool on);
    void recomputeSearchMatches();
    int setSearchQuery(const QString &query);
    bool hasLayer() const;
    int regionCount() const;
    bool hitLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const;
    QString selectedText() const;
    void clearSelection();
    bool copySelectedText();
    bool tryMousePressRubber(QMouseEvent *event);
    bool tryMouseMoveRubber(QMouseEvent *event);
    bool tryMouseReleaseRubber(QMouseEvent *event);
    /** Image-mode page-link activation (left click, no modifiers). */
    bool tryMousePressLink(QMouseEvent *event);
    /** Image-mode page-link hover tip + cursor (no-button move). */
    void updateMouseMoveLinkHover(QMouseEvent *event);
    /** Map a page text region bbox into content-display image coords. */
    QRectF regionImageRect(const ThumtooCache::TextRegion &region) const;

private:
    bool pageYUp() const;
    QRectF rubberBandImageRect() const;
    void finishRubberBand();

    ImageView *m_view = nullptr;
    TextLayerSession m_session;
};

#endif // TEXTLAYERCONTROLLER_H
