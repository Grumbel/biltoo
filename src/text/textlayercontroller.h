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
    /** @p force is ignored; OCR always re-runs. Kept for call-site compatibility. */
    bool applyOcrLayer(bool force = true, const QString &lang = QString());
    /** Session crop mapped into page space for OCR (empty if no crop). */
    QRectF currentPageCropInPageSpace() const;
    /** True when page Y increases upward (PDF/DjVu); false for EPUB Y-down. */
    bool pageYUp() const;
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
    /** All region texts on the current page in reading order (for export/copy). */
    QString pageTextInReadingOrder() const;

    /**
     * TTS intermediate stream: regions joined into continuous prose with a
     * span map back to boxes. Does not mutate regions (no layout merge).
     * Join: space within the same blockId; paragraph break (\n\n) between
     * blocks; space when blockId is unknown (OCR-friendly continuous speech).
     */
    struct SpeakSpan {
        int regionIndex = -1;
        int start = 0; ///< inclusive offset into SpeakPlan::text
        int end = 0;   ///< exclusive
    };
    struct SpeakPlan {
        QString text;
        QVector<SpeakSpan> spans;
    };
    /**
     * @p pageOnly true: always full-page reading order (selection is only an
     * anchor for start offset, not the spoken range).
     * false: selection if any, else full page.
     */
    SpeakPlan buildSpeakPlan(bool pageOnly = false) const;
    /**
     * Full-page SpeakPlan text (same as buildSpeakPlan(true)).
     * Selection does not shrink this — Speak uses the full page; selection is
     * only a start anchor.
     */
    QString speakableText() const;
    /** Spans into speakableText() — always full-page plan offsets. */
    QVector<SpeakSpan> speakSpans() const;

    /** Highlight region(s) currently being spoken; progress 0..1 within the active span. */
    void setSpeakingHighlight(const QVector<int> &regionIndices, double progress);
    void clearSpeakingHighlight();
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
    SessionImageId currentSessionId() const;
    void syncMultiSelectionFromCurrentPage(const QVector<int> &ids);
    void restoreCurrentPageSelectionFromMulti();

    QRectF rubberBandImageRect() const;
    void finishRubberBand();
    /** Tightest text region under @p viewPos, or -1. */
    int regionIndexAtViewPos(const QPoint &viewPos) const;
    void selectRegionAtViewPos(const QPoint &viewPos);

    ImageView *m_view = nullptr;
    QVector<int> m_speakingRegions;
    double m_speakingProgress = 0.0;
    TextLayerSession m_session;
};

#endif // TEXTLAYERCONTROLLER_H
