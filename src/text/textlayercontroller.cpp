// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page text / link regions, search, rubber-band selection (owned by TextLayerController).

#include "text/textlayercontroller.h"
#include "content/contentxform.h"
#include "imageview.h"
#include "imageitem.h"
#include "text/textlayergeometry.h"
#include "text/textregionstyle.h"
#include "text/textsearchpolicy.h"
#include "text/textlayerresolve.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "session/sessionappearance.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QWidget>

TextLayerController::TextLayerController(ImageView *view)
    : QObject(view)
    , m_view(view)
{
}

void TextLayerController::paintRubberBandOverlay(QPainter &painter)
{
    if (m_session.isRubberbanding() && m_session.hasRubberRect()) {
        painter.save();
        QPen pen(QColor(40, 120, 220, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(QColor(60, 160, 255, 40));
        painter.drawRect(m_session.rubberRectRef().normalized());
        painter.restore();
    }
}

void TextLayerController::setShowRegions(bool on)
{
    if (!m_session.setShowRegions(on)) {
        return;
    }
    if (m_session.needsLayer()) {
        refresh();
    } else {
        m_session.resetLayerContent();
        m_session.clearSearchMatches();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void TextLayerController::refresh()
{
    m_session.resetLayerContent();
    m_session.clearSearchMatches();
    const QString path = m_view->hostImage().classicPath();
    if (path.isEmpty()) {
        emit layerChanged();
        return;
    }
    // Explicit refresh always loads (Text panel, OCR install, show-regions).
    const ThumtooCache::PageTextLayer layer =
        TextLayerResolve::load(path, m_session.layerPreferValue());
    m_session.setLayerContent(layer, path);
    restoreCurrentPageSelectionFromMulti();
    if (m_session.hasSearchQuery()) {
        recomputeSearchMatches();
    }
    emit layerChanged();
}


void TextLayerController::installLayer(const ThumtooCache::PageTextLayer &layer,
                                            const QString &path)
{
    m_session.resetLayerContent();
    m_session.clearSearchMatches();
    m_session.setLayerContent(layer, path);
    restoreCurrentPageSelectionFromMulti();
    if (m_session.hasSearchQuery()) {
        recomputeSearchMatches();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit layerChanged();
}

bool TextLayerController::applyOcrLayer(bool force, const QString &lang)
{
    m_session.resetLayerContent();
    m_session.clearSearchMatches();
    const QString path = m_view->hostImage().classicPath();
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return false;
    }
    Q_UNUSED(force);
    // Always re-run: a user-requested OCR replaces the stored layer.
    ThumtooCache::PageTextLayer layer =
        ThumtooCache::ensureOcrPageTextLayer(path, /*force=*/true, lang);
    if (layer.regions.isEmpty() && !layer.pageBounds.isValid()) {
        return false;
    }
    m_session.setLayerContent(layer, path);
    restoreCurrentPageSelectionFromMulti();
    if (m_session.hasSearchQuery()) {
        recomputeSearchMatches();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return m_session.hasRegions();
}


QRectF TextLayerController::currentPageCropInPageSpace() const
{
    if (!m_view) {
        return {};
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        return {};
    }
    const QString path = m_view->hostImage().classicPath();
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && m_view->isImageMode()) {
        sid = m_view->hostSessionId().currentIdValue();
    }
    WorkspaceItemState st;
    if (sid != kInvalidSessionImageId) {
        st = m_view->sessionAppearanceValue(sid);
    }
    if (!st.hasCrop || st.cropRect.isEmpty()) {
        return {};
    }

    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = item->imageSize();
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }

    const ContentXform::Value x = ContentXform::Value::fromState(st);
    const QRect cropOr = ContentXform::orientedCropRect(sourceSize, x);
    if (cropOr.width() < 1 || cropOr.height() < 1) {
        return {};
    }
    // Full crop window in display/crop-local space → source pixels.
    const QRectF sourceCrop = ContentXform::mapDisplayRectToSource(
        QRectF(0, 0, cropOr.width(), cropOr.height()), sourceSize, x);
    if (sourceCrop.isEmpty()) {
        return {};
    }

    QRectF pageBounds;
    if (m_session.pageBoundsValid()) {
        pageBounds = m_session.pageBounds();
    } else {
        const auto native = ThumtooCache::cachedPageTextLayer(path);
        if (native.pageBounds.isValid()) {
            pageBounds = native.pageBounds;
        } else {
            // Image OCR uses page box == full source raster (Y-down).
            pageBounds = QRectF(0, 0, sourceSize.width(), sourceSize.height());
        }
    }
    if (!pageBounds.isValid() || pageBounds.width() < 1 || pageBounds.height() < 1) {
        return {};
    }
    return ThumtooCache::imageRectToPageRect(sourceCrop, pageBounds, sourceSize, pageYUp());
}

void TextLayerController::setSearchFuzzy(bool on)
{
    if (!m_session.setSearchFuzzy(on)) {
        return;
    }
    if (m_session.hasSearchQuery()) {
        recomputeSearchMatches();
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

void TextLayerController::setLayerPrefer(TextLayerResolve::Prefer prefer)
{
    if (!m_session.setLayerPrefer(prefer)) {
        return;
    }
    if (m_session.needsLayer()) {
        refresh();
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

TextLayerResolve::Prefer TextLayerController::layerPrefer() const
{
    return m_session.layerPreferValue();
}

void TextLayerController::recomputeSearchMatches()
{
    m_session.clearSearchMatches();
    if (!m_session.hasSearchQuery() || !m_session.hasRegions()) {
        return;
    }
    QVector<QString> texts;
    QVector<QRectF> bboxes;
    QVector<int> blockIds;
    texts.reserve(m_session.regionCount());
    bboxes.reserve(m_session.regionCount());
    blockIds.reserve(m_session.regionCount());
    for (int i = 0; i < m_session.regionCount(); ++i) {
        const auto &r = m_session.regionAt(i);
        texts.append(r.text);
        bboxes.append(r.bbox);
        blockIds.append(r.blockId);
    }
    const QVector<TextSearchPolicy::SearchHit> hits = TextSearchPolicy::findHits(
        texts, bboxes, m_session.searchQueryRef(), m_session.isSearchFuzzy(),
        blockIds);
    m_session.setSearchMatches(hits);
}

int TextLayerController::setSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (m_session.searchQueryRef() == trimmed && m_session.hasRegions()) {
        return m_session.searchMatchesRef().size();
    }
    m_session.setSearchQuery(trimmed);
    if (!m_session.hasSearchQuery()) {
        m_session.clearSearchMatches();
        if (!m_session.showsRegions()) {
            m_session.resetLayerContent();
        }
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return 0;
    }
    if (!m_session.hasRegions()
        || m_session.layerPathRef() != m_view->hostImage().classicPath()) {
        refresh();
    } else {
        recomputeSearchMatches();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return m_session.searchMatchesRef().size();
}

bool TextLayerController::hasLayer() const
{
    return m_session.hasRegions()
        && m_session.layerPathRef() == m_view->hostImage().classicPath();
}

int TextLayerController::regionCount() const
{
    if (!hasLayer()) {
        return 0;
    }
    return m_session.regionCount();
}

bool TextLayerController::hitLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const
{
    if (pageOut) {
        *pageOut = 0;
    }
    if (uriOut) {
        uriOut->clear();
    }
    if (!m_view->isImageMode() || !PagePath::isPageRef(m_view->hostImage().classicPath())) {
        return false;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    if (!m_session.hasRegions()
        || m_session.layerPathRef() != m_view->hostImage().classicPath()) {
        return false;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0 || !m_session.pageBoundsValid()) {
        return false;
    }
    const QPointF scene = m_view->mapToScene(viewPos);
    const QPointF local = item->mapFromScene(scene);
    if (!item->contentRect().contains(local)) {
        return false;
    }
    const QPointF imgPt = local - item->offset();
    for (const ThumtooCache::TextRegion &r : m_session.regions()) {
        if (r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        const QRectF img = regionImageRect(r);
        if (img.contains(imgPt)) {
            if (pageOut) {
                *pageOut = r.linkPage;
            }
            if (uriOut) {
                *uriOut = r.linkUri;
            }
            return true;
        }
    }
    return false;
}

bool TextLayerController::pageYUp() const
{
    // Prefer the flag on the loaded layer (native + OCR after thumtoo TTL7).
    if (m_session.pageBoundsValid()) {
        return m_session.layerRef().pageYUp;
    }
    if (!m_view) {
        return true;
    }
    return ThumtooCache::pageSpaceYUpForPath(m_view->hostImage().classicPath());
}

QRectF TextLayerController::regionImageRect(const ThumtooCache::TextRegion &region) const
{
    // region.bbox is document page space (see docs/OCR_COORDINATES.md).
    // Live crop/orient/grade are applied here only — never baked into bbox.
    ImageItem *item = m_view->primaryItem();
    if (!item || !m_session.pageBoundsValid()) {
        return {};
    }

    const QString path = m_view->hostImage().classicPath();
    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        const QSize known = m_view->hostSizeBook().known(path);
        if (!known.isEmpty()) {
            sourceSize = known;
        }
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = item->imageSize();
        WorkspaceItemState stGuess;
        SessionImageId sidGuess = item->sessionId();
        if (sidGuess == kInvalidSessionImageId && m_view->isImageMode()) {
            sidGuess = m_view->hostSessionId().currentIdValue();
        }
        if (sidGuess != kInvalidSessionImageId) {
            stGuess = m_view->sessionAppearanceValue(sidGuess);
        }
        int turns = stGuess.contentQuarterTurns % 4;
        if (turns < 0) {
            turns += 4;
        }
        if (!stGuess.hasCrop && (turns % 2) != 0) {
            sourceSize.transpose();
        }
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }

    WorkspaceItemState st;
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && m_view->isImageMode()) {
        sid = m_view->hostSessionId().currentIdValue();
    }
    if (sid != kInvalidSessionImageId) {
        st = m_view->sessionAppearanceValue(sid);
    } else if (!path.isEmpty()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && stored.hasOrientContent()) {
            SessionAppearance::applyStoredContentAppearance(&st, stored, false);
        }
    }

    const bool pageYUpFlag = pageYUp();
    const QRectF inSource = ThumtooCache::pageRectToImageRect(
        region.bbox, m_session.pageBounds(), sourceSize, pageYUpFlag);
    if (inSource.isEmpty()) {
        return {};
    }

    // Same pipeline as materializeDisplay / OCR inverse: ContentXform only.
    // SessionAppearance::mapSourceRectToContentDisplay was a parallel path that
    // could disagree on crop scale (cropSourceSize / orientation) and left
    // boxes translated wrong after crop apply/reset.
    const ContentXform::Value x = ContentXform::Value::fromState(st);
    QRectF disp = ContentXform::mapSourceRectToDisplay(inSource, sourceSize, x);
    if (disp.isEmpty()) {
        return {};
    }

    // mapSourceRectToDisplay is in native layout pixels (layoutSize). The item
    // box may lag or differ (provisional / soft); stretch so boxes track the
    // painted contentRect after crop apply/reset.
    const QSize logical = ContentXform::layoutSize(sourceSize, x);
    const QSize itemSz = item->imageSize();
    if (logical.width() > 0 && logical.height() > 0
        && itemSz.width() > 0 && itemSz.height() > 0
        && (logical.width() != itemSz.width()
            || logical.height() != itemSz.height())) {
        disp = QRectF(
            disp.x() * qreal(itemSz.width()) / qreal(logical.width()),
            disp.y() * qreal(itemSz.height()) / qreal(logical.height()),
            disp.width() * qreal(itemSz.width()) / qreal(logical.width()),
            disp.height() * qreal(itemSz.height()) / qreal(logical.height()));
    }
    return disp;
}

QRectF TextLayerController::rubberBandImageRect() const
{
    ImageItem *item = m_view->primaryItem();
    if (!item || !m_session.hasRubberRect()) {
        return {};
    }
    const QRectF sceneRect = m_view->mapToScene(m_session.rubberRectRef()).boundingRect();
    const QRectF local = item->mapFromScene(sceneRect).boundingRect();
    return local.translated(-item->offset());
}


int TextLayerController::regionIndexAtViewPos(const QPoint &viewPos) const
{
    if (!m_view->isImageMode() || !hasLayer() || !m_session.pageBoundsValid()) {
        return -1;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        return -1;
    }
    const QPointF scene = m_view->mapToScene(viewPos);
    const QPointF local = item->mapFromScene(scene);
    if (!item->contentRect().contains(local)) {
        return -1;
    }
    const QPointF imgPt = local - item->offset();
    int best = -1;
    qreal bestArea = -1.0;
    for (int i = 0; i < m_session.regionCount(); ++i) {
        const auto &r = m_session.regionAt(i);
        if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        const QRectF img = regionImageRect(r);
        if (!img.contains(imgPt)) {
            continue;
        }
        const qreal area = img.width() * img.height();
        // Prefer the smallest containing region (tighter hit).
        if (best < 0 || area < bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

void TextLayerController::selectRegionAtViewPos(const QPoint &viewPos)
{
    const int idx = regionIndexAtViewPos(viewPos);
    if (idx < 0) {
        // Keep multi-page bag for other pages; clear only current page projection.
        setSelectedRegions({});
        return;
    }
    setSelectedRegions(QVector<int>{idx});
}

void TextLayerController::finishRubberBand()
{
    const QRect viewRect = m_session.rubberRectRef().normalized();
    const QPoint clickView = m_session.rubberOrigin;
    m_session.endRubber();
    m_session.clearSelectedRegions();
    // Click (tiny drag): select the text region under the pointer, if any.
    if (viewRect.width() < 4 || viewRect.height() < 4) {
        selectRegionAtViewPos(clickView);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    if (!m_session.hasRegions()
        || m_session.layerPathRef() != m_view->hostImage().classicPath()) {
        const bool hadShow = m_session.showsRegions();
        m_session.setShowRegions(true);
        refresh();
        m_session.setShowRegions(hadShow);
    }
    ImageItem *item = m_view->primaryItem();
    if (!item || !m_session.hasRegions() || !m_session.pageBoundsValid()) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    m_session.setRubberRect(viewRect);
    const QRectF imgRubber = rubberBandImageRect();
    m_session.clearRubberRect();
    if (imgRubber.isEmpty()) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }
    QVector<QRectF> regionRects(m_session.regionCount());
    for (int i = 0; i < m_session.regionCount(); ++i) {
        const auto &r = m_session.regionAt(i);
        if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        regionRects[i] = regionImageRect(r);
    }
    QVector<int> selected =
        TextLayerGeometry::indicesIntersecting(regionRects, imgRubber);
    QVector<int> selBlocks(m_session.regionCount(), -1);
    for (int i = 0; i < m_session.regionCount(); ++i) {
        selBlocks[i] = m_session.regionAt(i).blockId;
    }
    TextLayerGeometry::sortReadingOrder(&selected, regionRects, 4.0, &selBlocks);
    setSelectedRegions(selected);
}


void TextLayerController::setShowGlyphs(bool on)
{
    if (!m_session.setShowGlyphs(on)) {
        return;
    }
    if (m_session.needsLayer() && !m_session.hasRegions()) {
        refresh();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void TextLayerController::setHoverRegion(int regionIndex)
{
    if (!m_session.setHoverRegion(regionIndex)) {
        return;
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit hoverChanged(regionIndex);
}

SessionImageId TextLayerController::currentSessionId() const
{
    ImageItem *item = m_view->primaryItem();
    if (item && item->sessionId() != kInvalidSessionImageId) {
        return item->sessionId();
    }
    if (m_view->isImageMode()) {
        return m_view->hostSessionId().currentIdValue();
    }
    return kInvalidSessionImageId;
}

void TextLayerController::syncMultiSelectionFromCurrentPage(const QVector<int> &ids)
{
    const SessionImageId sid = currentSessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    QVector<QString> texts;
    texts.reserve(ids.size());
    for (int idx : ids) {
        if (idx >= 0 && idx < m_session.regionCount()) {
            texts.append(m_session.regionAt(idx).text);
        } else {
            texts.append(QString());
        }
    }
    m_session.multiSelection.setForSession(sid, ids, texts);
}

void TextLayerController::restoreCurrentPageSelectionFromMulti()
{
    const SessionImageId sid = currentSessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    const QVector<int> ids = m_session.multiSelection.regionIndicesFor(sid);
    if (m_session.selectedRegionsRef() == ids) {
        return;
    }
    m_session.setSelectedRegions(ids);
}

void TextLayerController::setSelectedRegions(const QVector<int> &ids)
{
    if (m_session.selectedRegionsRef() == ids) {
        // Still refresh multi snapshots if same indices (text may have loaded).
        syncMultiSelectionFromCurrentPage(ids);
        return;
    }
    m_session.setSelectedRegions(ids);
    syncMultiSelectionFromCurrentPage(ids);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit selectionChanged();
}

QString TextLayerController::selectedText() const
{
    // Prefer multi-page snapshots so copy survives page changes.
    if (!m_session.multiSelection.isEmpty()) {
        const QString multi = m_session.multiSelection.joinedText();
        if (!multi.isEmpty()) {
            return multi;
        }
    }
    QStringList lines;
    for (int idx : m_session.selectedRegionsRef()) {
        if (idx < 0 || idx >= m_session.regionCount()) {
            continue;
        }
        const QString &tx = m_session.regionAt(idx).text;
        if (!tx.isEmpty()) {
            lines.append(tx);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString TextLayerController::pageTextInReadingOrder() const
{
    if (!hasLayer()) {
        return QString();
    }
    const int n = m_session.regionCount();
    if (n <= 0) {
        return QString();
    }
    QVector<int> order;
    order.reserve(n);
    QVector<QRectF> rects;
    rects.reserve(n);
    QVector<int> blocks;
    blocks.reserve(n);
    for (int i = 0; i < n; ++i) {
        order.append(i);
        const auto &r = m_session.regionAt(i);
        rects.append(r.bbox);
        blocks.append(r.blockId);
    }
    TextLayerGeometry::sortReadingOrder(&order, rects, 4.0, &blocks);
    QStringList lines;
    for (int idx : order) {
        if (idx < 0 || idx >= n) {
            continue;
        }
        const QString &tx = m_session.regionAt(idx).text;
        if (!tx.isEmpty()) {
            lines.append(tx);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString TextLayerController::speakableText() const
{
    const QString sel = selectedText();
    if (!sel.isEmpty()) {
        return sel;
    }
    return pageTextInReadingOrder();
}

QVector<TextLayerController::SpeakSpan> TextLayerController::speakSpans() const
{
    QVector<SpeakSpan> spans;
    if (!hasLayer()) {
        return spans;
    }

    QVector<int> order;
    if (!m_session.multiSelection.isEmpty() || !m_session.selectedRegionsRef().isEmpty()) {
        // Selection path: multi-page uses current page projection only for highlight.
        order = m_session.selectedRegionsRef();
        if (order.isEmpty() && !m_session.multiSelection.isEmpty()) {
            order = m_session.multiSelection.regionIndicesFor(currentSessionId());
        }
    } else {
        const int n = m_session.regionCount();
        order.reserve(n);
        QVector<QRectF> rects;
        QVector<int> blocks;
        for (int i = 0; i < n; ++i) {
            order.append(i);
            const auto &r = m_session.regionAt(i);
            rects.append(r.bbox);
            blocks.append(r.blockId);
        }
        TextLayerGeometry::sortReadingOrder(&order, rects, 4.0, &blocks);
    }

    int cursor = 0;
    bool first = true;
    for (int idx : order) {
        if (idx < 0 || idx >= m_session.regionCount()) {
            continue;
        }
        const QString &tx = m_session.regionAt(idx).text;
        if (tx.isEmpty()) {
            continue;
        }
        if (!first) {
            ++cursor; // newline join separator in selectedText / pageTextInReadingOrder
        }
        first = false;
        SpeakSpan sp;
        sp.regionIndex = idx;
        sp.start = cursor;
        sp.end = cursor + tx.size();
        spans.append(sp);
        cursor = sp.end;
    }
    return spans;
}

void TextLayerController::setSpeakingHighlight(const QVector<int> &regionIndices, double progress)
{
    m_speakingRegions = regionIndices;
    m_speakingProgress = qBound(0.0, progress, 1.0);
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void TextLayerController::clearSpeakingHighlight()
{
    if (m_speakingRegions.isEmpty() && m_speakingProgress == 0.0) {
        return;
    }
    m_speakingRegions.clear();
    m_speakingProgress = 0.0;
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void TextLayerController::clearSelection()
{
    if (!m_session.hasSelection() && !m_session.isRubberbanding()) {
        return;
    }
    m_session.clearSelection();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit selectionChanged();
}

bool TextLayerController::copySelectedText()
{
    const QString text = selectedText();
    if (text.isEmpty()) {
        return false;
    }
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        return false;
    }
    clip->setText(text);
    return true;
}

bool TextLayerController::tryMousePressRubber(QMouseEvent *event)
{
    if (!m_view->isImageMode() || m_view->hostCrop().active()
        || m_view->hostAttention().active()
        || event->button() != Qt::LeftButton
        || (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier))) {
        return false;
    }
    // Select tool: left-drag rubber-band. Pan tool: keep Shift+drag as text select.
    const bool selectTool = m_view->currentTool() == Tool::Select;
    const bool shiftSelect = event->modifiers() & Qt::ShiftModifier;
    if (!selectTool && !shiftSelect) {
        return false;
    }
    // Only when a text/OCR layer is present (avoid eating edge-nav clicks).
    if (!m_session.hasRegions()) {
        return false;
    }
    m_session.beginRubber(event->pos());
    m_session.clearSelectedRegions();
    m_view->setCursor(Qt::CrossCursor);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    event->accept();
    return true;
}

bool TextLayerController::tryMouseMoveRubber(QMouseEvent *event)
{
    if (!m_session.isRubberbanding() || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    m_session.updateRubber(event->pos());
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    event->accept();
    return true;
}

bool TextLayerController::tryMouseReleaseRubber(QMouseEvent *event)
{
    if (!m_session.isRubberbanding() || event->button() != Qt::LeftButton) {
        return false;
    }
    m_session.updateRubber(event->pos());
    finishRubberBand();
    m_view->unsetCursor();
    event->accept();
    return true;
}


bool TextLayerController::tryMousePressLink(QMouseEvent *event)
{
    if (!m_view->isImageMode() || m_view->hostCrop().active()
        || m_view->hostAttention().active()
        || event->button() != Qt::LeftButton
        || event->modifiers() != Qt::NoModifier
        || !PagePath::isPageRef(m_view->hostImage().classicPath())) {
        return false;
    }
    if (!session().hasLayerRegions()
        || session().layerPathRef() != m_view->hostImage().classicPath()) {
        const bool hadShow = session().showsRegions();
        session().setShowRegions(true);
        refresh();
        session().setShowRegions(hadShow);
    }
    int page = 0;
    QString uri;
    if (!hitLinkAt(event->pos(), &page, &uri)) {
        return false;
    }
    emit m_view->linkActivated(page, uri);
    event->accept();
    return true;
}


void TextLayerController::updateMouseMoveLinkHover(QMouseEvent *event)
{
    // Link hover: pointing hand + status tip (Image mode page docs).
    if (m_view->isImageMode() && !m_view->hostCrop().active()
        && !m_view->hostAttention().active() && !session().isRubberbanding()
        && !m_view->hostChrome().isPanning() && event->buttons() == Qt::NoButton
        && PagePath::isPageRef(m_view->hostImage().classicPath())) {
        if (!session().hasLayerRegions()
            || session().layerPathRef() != m_view->hostImage().classicPath()) {
            const ThumtooCache::PageTextLayer cached =
                ThumtooCache::cachedPageTextLayer(m_view->hostImage().classicPath());
            if (!cached.regions.isEmpty()) {
                session().setLayerContent(cached, m_view->hostImage().classicPath());
            }
        }
        int page = 0;
        QString uri;
        QString tip;
        if (hitLinkAt(event->pos(), &page, &uri)) {
            m_view->setCursor(Qt::PointingHandCursor);
            if (page > 0) {
                tip = m_view->tr("Link → page %1").arg(page);
            }
            if (!uri.isEmpty()) {
                tip = tip.isEmpty() ? uri : (tip + QStringLiteral(" · ") + uri);
            }
            if (tip.isEmpty()) {
                tip = m_view->tr("Link");
            }
        } else if (m_view->hostHoverEdge() == ImageView::EdgeZone::None) {
            m_view->setCursor(m_view->hostChrome().isImageModeLeftDragPan()
                                  ? Qt::OpenHandCursor
                                  : Qt::ArrowCursor);
        }
        if (session().setLinkHoverTip(tip)) {
            emit m_view->statusChanged();
        }
    } else if (session().hasLinkHoverTip() && event->buttons() == Qt::NoButton) {
        session().clearLinkHoverTip();
        emit m_view->statusChanged();
    }
}

void TextLayerController::paintSceneOverlays(QPainter *painter) const
{
    if (!painter || !m_view || !m_view->isImageMode()) {
        return;
    }
    // Paint when any overlay layer is active (not only region outlines / search).
    if (!m_session.hasRegions()
        || !(m_session.showsRegions() || m_session.showsGlyphs()
             || m_session.hasSearchMatches() || m_session.hasSelection()
             || m_session.hoverRegionIndex() >= 0
             || !m_speakingRegions.isEmpty())) {
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0 || !m_session.pageBoundsValid()) {
        return;
    }
    painter->save();
    // Search hits: filled yellow first (under outlines / glyphs / selection).
    if (m_session.hasSearchMatches()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 220, 40, 110));
        for (const TextSearchPolicy::SearchHit &hit : m_session.searchMatchesRef()) {
            if (hit.regionIndex < 0 || hit.regionIndex >= m_session.regionCount()) {
                continue;
            }
            const auto &r = m_session.regionAt(hit.regionIndex);
            QRectF img = regionImageRect(r);
            if (img.isEmpty()) {
                continue;
            }
            // LTR approximation: highlight the horizontal slice of the box
            // that likely holds the matched substring (uniform advance).
            const qreal a = qBound(0.0, hit.startFrac, 1.0);
            const qreal b = qBound(0.0, hit.endFrac, 1.0);
            if (b > a && (a > 0.0 || b < 1.0)) {
                const qreal x0 = img.left() + img.width() * a;
                const qreal x1 = img.left() + img.width() * b;
                img = QRectF(QPointF(x0, img.top()), QPointF(x1, img.bottom()));
            }
            const QRectF local = img.translated(item->offset());
            painter->drawPolygon(item->mapToScene(local));
        }
    }
    if (m_session.showsRegions()) {
        painter->setBrush(Qt::NoBrush);
        for (const ThumtooCache::TextRegion &r : m_session.regions()) {
            const QRectF img = regionImageRect(r);
            if (img.isEmpty()) {
                continue;
            }
            const QRectF local = img.translated(item->offset());
            const QPolygonF scenePoly = item->mapToScene(local);
            QPen pen(TextRegionStyle::outlineColor(r));
            pen.setCosmetic(true);
            pen.setWidthF(0);
            painter->setPen(pen);
            painter->drawPolygon(scenePoly);
        }
    }

    // Glyphs: recognized text stretched to the OCR/native bbox (readable fill).
    if (m_session.showsGlyphs()) {
        for (const ThumtooCache::TextRegion &r : m_session.regions()) {
            if (r.text.isEmpty()) {
                continue;
            }
            const QRectF img = regionImageRect(r);
            if (img.isEmpty() || img.height() < 2.0) {
                continue;
            }
            const QRectF local = img.translated(item->offset());
            const QPolygonF scenePoly = item->mapToScene(local);
            const QRectF sceneBox = scenePoly.boundingRect();
            if (sceneBox.height() < 1.0 || sceneBox.width() < 1.0) {
                continue;
            }
            painter->setPen(QPen(TextRegionStyle::outlineColor(r), 0));
            painter->setBrush(QColor(255, 252, 230, 220));
            painter->drawPolygon(scenePoly);

            // Measure at a stable pixel size, then non-uniform scale into the box
            // so long lines fill width and height tracks the region (not Qt wrap).
            QFont f = painter->font();
            f.setPixelSize(48); // reference size; scale maps into sceneBox
            f.setStyleStrategy(QFont::PreferDefault);
            const QFontMetricsF fm(f);
            const QString line = r.text.simplified();
            qreal advance = fm.horizontalAdvance(line);
            if (advance < 1.0) {
                advance = 1.0;
            }
            const qreal textH = qMax(qreal(1.0), fm.height());
            const qreal sx = sceneBox.width() / advance;
            const qreal sy = sceneBox.height() / textH;
            painter->save();
            painter->setFont(f);
            painter->setPen(QColor(20, 20, 20, 235));
            painter->setBrush(Qt::NoBrush);
            // Map reference text space → scene box (may shear slightly if poly
            // is rotated; boundingRect scale is good enough for Image mode).
            painter->translate(sceneBox.topLeft());
            painter->scale(sx, sy);
            painter->drawText(QPointF(0.0, fm.ascent()), line);
            painter->restore();
        }
    }

    // Selection / hover on top of glyphs so cyan stays readable over paper fill.
    if (m_session.hasSelection()) {
        painter->setPen(QPen(QColor(20, 90, 200, 230), 0));
        painter->setBrush(QColor(40, 140, 255, 110));
        for (int idxSel : m_session.selectedRegionsRef()) {
            if (idxSel < 0 || idxSel >= m_session.regionCount()) {
                continue;
            }
            const auto &r = m_session.regionAt(idxSel);
            const QRectF img = regionImageRect(r);
            if (img.isEmpty()) {
                continue;
            }
            const QRectF local = img.translated(item->offset());
            painter->drawPolygon(item->mapToScene(local));
        }
    }
    if (m_session.hoverRegionIndex() >= 0
        && m_session.hoverRegionIndex() < m_session.regionCount()) {
        const auto &r = m_session.regionAt(m_session.hoverRegionIndex());
        const QRectF img = regionImageRect(r);
        if (!img.isEmpty()) {
            painter->setPen(QPen(QColor(180, 100, 0, 220), 0));
            painter->setBrush(QColor(255, 160, 40, 80));
            const QRectF local = img.translated(item->offset());
            painter->drawPolygon(item->mapToScene(local));
        }
    }

    // TTS: spoken region(s) — green fill; progress clips the active box LTR.
    if (!m_speakingRegions.isEmpty()) {
        for (int i = 0; i < m_speakingRegions.size(); ++i) {
            const int idx = m_speakingRegions.at(i);
            if (idx < 0 || idx >= m_session.regionCount()) {
                continue;
            }
            const auto &r = m_session.regionAt(idx);
            QRectF img = regionImageRect(r);
            if (img.isEmpty()) {
                continue;
            }
            const bool isActive = (i == m_speakingRegions.size() - 1);
            if (isActive && m_speakingProgress > 0.0 && m_speakingProgress < 1.0) {
                const qreal x1 = img.left() + img.width() * m_speakingProgress;
                img = QRectF(QPointF(img.left(), img.top()),
                             QPointF(x1, img.bottom()));
            }
            painter->setPen(QPen(QColor(20, 140, 70, 230), 0));
            painter->setBrush(QColor(40, 200, 100, isActive ? 130 : 70));
            const QRectF local = img.translated(item->offset());
            painter->drawPolygon(item->mapToScene(local));
        }
    }

    painter->restore();
}

