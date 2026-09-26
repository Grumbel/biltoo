// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERCONTROLLER_H
#define TEXTLAYERCONTROLLER_H

#include "text/textlayersession.h"
#include "text/textlayerresolve.h"
#include "host/thumtoocache.h"

#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

class ImageView;
class QPainter;
class QMouseEvent;

/**
 * Page text / link overlay collaborator (search, rubber-band, hit-test).
 * Owns TextLayerSession; ImageView keeps thin public/shell forwards.
 * QObject signals bridge panel ↔ page without MainWindow glue spaghetti.
 */
class TextLayerController : public QObject
{
    Q_OBJECT
public:
    explicit TextLayerController(ImageView *view);

    TextLayerSession &session() { return m_session; }
    const TextLayerSession &session() const { return m_session; }

    void paintRubberBandOverlay(QPainter &painter);
    /**
     * Scene-space search hits, selection fill, and region/link outlines
     * (Image mode). Called from drawForeground before viewport overlays.
     */
    void paintSceneOverlays(QPainter *painter) const;
    void setShowRegions(bool on);
    void setShowGlyphs(bool on);
    void setHoverRegion(int regionIndex);
    /** Replace selection (panel or host); emits selectionChanged when changed. */
    void setSelectedRegions(const QVector<int> &ids);
    void refresh();
    /** Run OCR for the current page and install the OCR text layer. */
    bool applyOcrLayer(bool force = false, const QString &lang = QString());
    /** Install a pre-fetched layer (e.g. after worker OCR). */
    void installLayer(const ThumtooCache::PageTextLayer &layer, const QString &path);
    void setSearchFuzzy(bool on);
    void setLayerPrefer(TextLayerResolve::Prefer prefer);
    TextLayerResolve::Prefer layerPrefer() const;
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

signals:
    void layerChanged();
    void selectionChanged();
    void hoverChanged(int regionIndex);

private:
    bool pageYUp() const;
    QRectF rubberBandImageRect() const;
    void finishRubberBand();
    /** Tightest text region under @p viewPos, or -1. */
    int regionIndexAtViewPos(const QPoint &viewPos) const;
    void selectRegionAtViewPos(const QPoint &viewPos);

    ImageView *m_view = nullptr;
    TextLayerSession m_session;
};

#endif // TEXTLAYERCONTROLLER_H
