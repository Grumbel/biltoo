// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERCONTROLLER_H
#define TEXTLAYERCONTROLLER_H

#include "text/textlayersession.h"
#include "text/textlayerresolve.h"
#include "host/thumtoocache.h"

#include <QObject>
#include <QHash>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

class ImageView;
class ImageItem;
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
        SessionImageId sessionId = kInvalidSessionImageId;
        int regionIndex = -1;
        int start = 0; ///< inclusive offset into SpeakPlan::text
        int end = 0;   ///< exclusive
    };
    struct SpeakPlan {
        QString text;
        QVector<SpeakSpan> spans;
    };
    /** What buildSpeakPlan joins into continuous prose. */
    enum class SpeakScope {
        /** Selection regions only; if empty, falls back to FullPage. */
        SelectionOrPage = 0,
        /** Current page, or all live underlays when a spread is shown. */
        FullPage = 1,
        /**
         * Entire session document in session order (page breaks become
         * paragraph breaks). Missing text layers are skipped.
         */
        FullDocument = 2,
    };
    /**
     * Continuous prose + span map. Selection never shrinks FullPage /
     * FullDocument range — use selection only as a start anchor.
     * @p pageOnly true → FullPage (legacy); false → SelectionOrPage.
     */
    SpeakPlan buildSpeakPlan(bool pageOnly = false) const;
    SpeakPlan buildSpeakPlan(SpeakScope scope) const;
    /**
     * Full-document SpeakPlan text when a session document is bound;
     * otherwise FullPage. Selection does not shrink this.
     */
    QString speakableText() const;
    /** Spans matching speakableText(). */
    QVector<SpeakSpan> speakSpans() const;
    /**
     * UTF-16 offset into @p plan for the current text selection (earliest
     * selected region, plus optional mid-region bias from the last click).
     * Returns 0 when nothing is selected.
     */
    int speakAnchorOffset(const SpeakPlan &plan) const;

    /** Highlight region(s) currently being spoken; progress 0..1 within the active span. */
    void setSpeakingHighlight(const QVector<int> &regionIndices, double progress);
    /** Spread: highlight on a specific member (sid); empty sid → primary. */
    void setSpeakingHighlight(SessionImageId sessionId, const QVector<int> &regionIndices,
                              double progress);
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
    /** Spread: map using a specific underlay item + layer (docs/SPREAD.md P2). */
    QRectF regionImageRectFor(ImageItem *item, const QString &path,
                              const ThumtooCache::PageTextLayer &layer,
                              const ThumtooCache::TextRegion &region) const;
    /** Load cached text layers for all live Image underlays (spread members). */
    void ensureMemberLayers();
    QString pathForItem(ImageItem *item) const;
    /** Text layer for underlay item (member bag or primary if path matches). */
    const ThumtooCache::PageTextLayer *layerForItem(ImageItem *item) const;

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

    bool isMultiUnderlay() const;

    ImageView *m_view = nullptr;
    QVector<int> m_speakingRegions;
    SessionImageId m_speakingSessionId = kInvalidSessionImageId;
    double m_speakingProgress = 0.0;
    TextLayerSession m_session;
    /** Approximate char index within the last clicked region (-1 = region start). */
    int m_speakRegionCharBias = -1;
    SessionImageId m_speakRegionCharBiasSid = kInvalidSessionImageId;
    int m_speakRegionCharBiasIndex = -1;

    /** Spread member layers keyed by SessionImageId (primary also in m_session). */
    QHash<SessionImageId, ThumtooCache::PageTextLayer> m_memberLayers;
    QHash<SessionImageId, QString> m_memberPaths;
};

#endif // TEXTLAYERCONTROLLER_H
