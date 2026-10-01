// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page text / link regions, search, rubber-band selection (owned by TextLayerController).

#include "text/textlayercontroller.h"
#include <cmath>
#include "session/sessiondocument.h"
#include "shell/chromecolors.h"
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
#include "display/displaypipelinecontroller.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QWidget>
#include "image/toolcursors.h"
#include <QCursor>

TextLayerController::TextLayerController(ImageView *view)
    : QObject(view)
    , m_view(view)
{
    if (m_view) {
        // Crop draft shows orient-only full frame; Applied ContentXform changes
        // without a text-layer install — repaint so regionImageRect re-maps.
        auto repaintText = [this]() {
            if (m_view && m_view->viewport()
                && (m_session.showsRegions() || m_session.hasSearchQuery()
                    || m_session.showsGlyphs())) {
                m_view->viewport()->update();
            }
        };
        connect(m_view, &ImageView::cropModeChanged, this, [repaintText](bool) { repaintText(); });
        // Content rotate/flip changes applied ContentXform without reinstalling the text layer.
        connect(m_view, &ImageView::sessionAppearanceChanged, this,
                [repaintText](SessionImageId, const QString &, const QImage &) { repaintText(); });
    }
}

void TextLayerController::paintRubberBandOverlay(QPainter &painter)
{
    if (m_session.isRubberbanding() && m_session.hasRubberRect()) {
        painter.save();
        QPen pen(ChromeColors::selectStroke(220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(ChromeColors::selectFill(40));
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
    m_memberLayers.clear();
    m_memberPaths.clear();
    const QString path = m_view->hostImage().classicPath();
    if (path.isEmpty()) {
        emit layerChanged();
        return;
    }
    // Explicit refresh always loads (Text panel, OCR install, show-regions).
    const ThumtooCache::PageTextLayer layer =
        TextLayerResolve::load(path, m_session.layerPreferValue());
    m_session.setLayerContent(layer, path);
    ensureMemberLayers();
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
    if (!m_session.hasSearchQuery()) {
        return;
    }
    ensureMemberLayers();
    auto hitsForLayer = [&](const ThumtooCache::PageTextLayer &layer) {
        QVector<QString> texts;
        QVector<QRectF> bboxes;
        QVector<int> blockIds;
        texts.reserve(layer.regions.size());
        bboxes.reserve(layer.regions.size());
        blockIds.reserve(layer.regions.size());
        for (const auto &r : layer.regions) {
            texts.append(r.text);
            bboxes.append(r.bbox);
            blockIds.append(r.blockId);
        }
        const bool ocrOrder = layer.source == ThumtooCache::TextLayerSource::Ocr;
        // Layer flag is authoritative (docs/OCR_COORDINATES.md).
        const bool yUp = layer.pageYUp;
        return TextSearchPolicy::findHits(
            texts, bboxes, m_session.searchQueryRef(), m_session.isSearchFuzzy(),
            blockIds, yUp, ocrOrder);
    };

    if (m_session.hasRegions()) {
        m_session.setSearchMatches(
            hitsForLayer(m_session.layerRef()));
    }

    if (!isMultiUnderlay()) {
        return;
    }
    ImageItem *primary = m_view->primaryItem();
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item == primary) {
            continue;
        }
        const ThumtooCache::PageTextLayer *layer = layerForItem(item);
        if (!layer || layer->regions.isEmpty()) {
            continue;
        }
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        const QString path = pathForItem(item);
        for (const TextSearchPolicy::SearchHit &hit : hitsForLayer(*layer)) {
            TextLayerSession::MemberSearchHit mh;
            mh.sessionId = sid;
            mh.hit = hit;
            m_session.memberSearchMatches.append(mh);
        }
    }
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

bool TextLayerController::isMultiUnderlay() const
{
    return m_view && m_view->isImageMode() && m_view->itemCount() > 1;
}

QString TextLayerController::pathForItem(ImageItem *item) const
{
    if (!item) {
        return {};
    }
    if (!item->path().isEmpty()) {
        return item->path();
    }
    return m_view ? m_view->hostImage().classicPath() : QString();
}

const ThumtooCache::PageTextLayer *TextLayerController::layerForItem(ImageItem *item) const
{
    if (!item || !m_view) {
        return nullptr;
    }
    const QString path = pathForItem(item);
    SessionImageId sid = item->sessionId();
    // Only fall back to the session cursor id for the classic/primary underlay.
    // Secondary spread pages must keep their own sid or we paint every page's
    // regions onto one item (and vice versa).
    if (sid == kInvalidSessionImageId && m_view->isImageMode()) {
        const bool isPrimary =
            item == m_view->primaryItem()
            || (!path.isEmpty() && path == m_view->hostImage().classicPath());
        if (isPrimary) {
            sid = m_view->hostSessionId().currentIdValue();
        }
    }
    if (sid != kInvalidSessionImageId) {
        auto it = m_memberLayers.constFind(sid);
        if (it != m_memberLayers.constEnd() && it->pageBounds.isValid()) {
            // Path must still match — sid reuse after membership change is rare
            // but wrong-path layers must not paint on the wrong underlay.
            if (m_memberPaths.value(sid).isEmpty()
                || m_memberPaths.value(sid) == path
                || path.isEmpty()) {
                return &(*it);
            }
        }
    }
    // Primary session layer only for the page that owns layerPath.
    if (m_session.pageBoundsValid()
        && !m_session.layerPathRef().isEmpty()
        && path == m_session.layerPathRef()) {
        return &m_session.layerRef();
    }
    return nullptr;
}

void TextLayerController::ensureMemberLayers()
{
    if (!m_view || !m_view->isImageMode()) {
        return;
    }
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        SessionImageId sid = item->sessionId();
        const QString path = pathForItem(item);
        if (path.isEmpty()) {
            continue;
        }
        if (sid == kInvalidSessionImageId) {
            const bool isPrimary =
                item == m_view->primaryItem()
                || path == m_view->hostImage().classicPath();
            if (isPrimary) {
                sid = m_view->hostSessionId().currentIdValue();
            }
        }
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        if (path == m_session.layerPathRef() && m_session.pageBoundsValid()) {
            m_memberLayers.insert(sid, m_session.layerRef());
            m_memberPaths.insert(sid, path);
            continue;
        }
        if (m_memberPaths.value(sid) == path && m_memberLayers.contains(sid)
            && m_memberLayers.value(sid).pageBounds.isValid()) {
            continue;
        }
        const ThumtooCache::PageTextLayer layer =
            TextLayerResolve::load(path, m_session.layerPreferValue());
        m_memberLayers.insert(sid, layer);
        m_memberPaths.insert(sid, path);
    }
}

QRectF TextLayerController::regionImageRect(const ThumtooCache::TextRegion &region) const
{
    ImageItem *item = m_view ? m_view->primaryItem() : nullptr;
    if (!item || !m_session.pageBoundsValid()) {
        return {};
    }
    return regionImageRectFor(item, m_view->hostImage().classicPath(),
                              m_session.layerRef(), region);
}

QRectF TextLayerController::regionImageRectFor(ImageItem *item, const QString &path,
                                               const ThumtooCache::PageTextLayer &layer,
                                               const ThumtooCache::TextRegion &region) const
{
    // region.bbox is document page space (see docs/OCR_COORDINATES.md).
    // Live crop/orient/grade are applied here only — never baked into bbox.
    if (!item || !layer.pageBounds.isValid() || path.isEmpty()) {
        return {};
    }

    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && m_view && m_view->isImageMode()) {
        sid = m_view->hostSessionId().currentIdValue();
    }

    const WorkspaceItemState want =
        m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(want);

    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        const QSize known = m_view->hostSizeBook().known(path);
        if (!known.isEmpty()) {
            sourceSize = known;
        }
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = item->imageSize();
        if (ContentXform::swapsAspect(x) && !x.hasCrop) {
            sourceSize.transpose();
        }
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }

    // Prefer the flag stored on the layer (OCR/native extractors set it).
    // Path fallback disagrees with older PDF OCR blobs that used page_y_up=true.
    const bool pageYUpFlag = layer.pageYUp;
    const QRectF inSource = ThumtooCache::pageRectToImageRect(
        region.bbox, layer.pageBounds, sourceSize, pageYUpFlag);
    if (inSource.isEmpty()) {
        return {};
    }

    QRectF disp = ContentXform::mapSourceRectToDisplay(inSource, sourceSize, x);
    if (disp.isEmpty()) {
        return {};
    }

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
    if (!m_view->isImageMode()) {
        return -1;
    }
    // Primary-page hit for panel / single-page; multi uses selectRegionAtViewPos.
    if (!hasLayer() || !m_session.pageBoundsValid()) {
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
        if (best < 0 || area < bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

void TextLayerController::selectRegionAtViewPos(const QPoint &viewPos)
{
    auto setCharBias = [this](SessionImageId sid, int idx, const QRectF &img,
                              const QPointF &imgPt, const QString &text) {
        m_speakRegionCharBiasSid = sid;
        m_speakRegionCharBiasIndex = idx;
        if (img.width() > 1.0 && !text.isEmpty()) {
            const qreal t = qBound(0.0, (imgPt.x() - img.left()) / img.width(), 1.0);
            m_speakRegionCharBias = int(std::lround(t * double(qMax(0, text.size() - 1))));
        } else {
            m_speakRegionCharBias = 0;
        }
    };

    if (!isMultiUnderlay()) {
        const int idx = regionIndexAtViewPos(viewPos);
        if (idx < 0) {
            m_speakRegionCharBias = -1;
            m_speakRegionCharBiasIndex = -1;
            m_speakRegionCharBiasSid = kInvalidSessionImageId;
            setSelectedRegions({});
            return;
        }
        // Approximate mid-box start for TTS from horizontal click fraction.
        if (m_view && m_view->primaryItem() && idx < m_session.regionCount()) {
            const auto &r = m_session.regionAt(idx);
            const QRectF img = regionImageRect(r);
            const QPointF scene = m_view->mapToScene(viewPos);
            const QPointF local = m_view->primaryItem()->mapFromScene(scene);
            const QPointF imgPt = local - m_view->primaryItem()->offset();
            setCharBias(currentSessionId(), idx, img, imgPt, r.text);
        }
        setSelectedRegions(QVector<int>{idx});
        return;
    }
    // Spread: hit-test every underlay; replace multi bag with the single hit.
    ensureMemberLayers();
    const QPointF scene = m_view->mapToScene(viewPos);
    m_session.multiSelection.clear();
    ImageItem *primary = m_view->primaryItem();
    QVector<int> primarySelected;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || !item->contentRect().contains(item->mapFromScene(scene))) {
            continue;
        }
        const ThumtooCache::PageTextLayer *layer = layerForItem(item);
        if (!layer || layer->regions.isEmpty()) {
            continue;
        }
        const QString path = pathForItem(item);
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            sid = m_view->hostSessionId().currentIdValue();
        }
        const QPointF imgPt = item->mapFromScene(scene) - item->offset();
        int best = -1;
        qreal bestArea = -1.0;
        for (int i = 0; i < layer->regions.size(); ++i) {
            const auto &r = layer->regions.at(i);
            if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
                continue;
            }
            const QRectF img = regionImageRectFor(item, path, *layer, r);
            if (!img.contains(imgPt)) {
                continue;
            }
            const qreal area = img.width() * img.height();
            if (best < 0 || area < bestArea) {
                best = i;
                bestArea = area;
            }
        }
        if (best < 0) {
            continue;
        }
        QVector<int> ids{best};
        QVector<QString> texts{layer->regions.at(best).text};
        {
            const auto &r = layer->regions.at(best);
            const QRectF img = regionImageRectFor(item, path, *layer, r);
            setCharBias(sid, best, img, imgPt, r.text);
        }
        m_session.multiSelection.setForSession(sid, ids, texts);
        if (item == primary) {
            primarySelected = ids;
        }
        break; // first hit in liveItems order
    }
    m_session.setSelectedRegions(primarySelected);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit selectionChanged();
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
    ensureMemberLayers();

    const QRectF sceneRubber = m_view->mapToScene(viewRect).boundingRect();
    if (sceneRubber.isEmpty()) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        return;
    }

    // Spread: classify hits per underlay; multiSelection keeps reading order by
    // liveItems order (left→right after layoutSpread).
    m_session.multiSelection.clear();
    QVector<int> primarySelected;
    ImageItem *primary = m_view->primaryItem();
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const ThumtooCache::PageTextLayer *layer = layerForItem(item);
        if (!layer || !layer->pageBounds.isValid() || layer->regions.isEmpty()) {
            continue;
        }
        const QString path = pathForItem(item);
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            sid = m_view->hostSessionId().currentIdValue();
        }
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        const QRectF local = item->mapFromScene(sceneRubber).boundingRect();
        const QRectF imgRubber = local.translated(-item->offset());
        if (imgRubber.isEmpty()) {
            continue;
        }
        const int n = layer->regions.size();
        QVector<QRectF> regionRects;
        regionRects.resize(n);
        QVector<int> selBlocks;
        selBlocks.resize(n);
        selBlocks.fill(-1);
        for (int i = 0; i < n; ++i) {
            const auto &r = layer->regions.at(i);
            if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
                continue;
            }
            regionRects[i] = regionImageRectFor(item, path, *layer, r);
            selBlocks[i] = r.blockId;
        }
        QVector<int> selected =
            TextLayerGeometry::indicesIntersecting(regionRects, imgRubber);
        const bool ocrOrder = layer->source == ThumtooCache::TextLayerSource::Ocr;
        TextLayerGeometry::sortReadingOrder(&selected, regionRects, 4.0, &selBlocks,
                                            /*pageYUp=*/false, ocrOrder);
        QVector<QString> texts;
        texts.reserve(selected.size());
        for (int idx : selected) {
            texts.append((idx >= 0 && idx < n) ? layer->regions.at(idx).text : QString());
        }
        m_session.multiSelection.setForSession(sid, selected, texts);
        if (item == primary) {
            primarySelected = selected;
        }
    }
    m_session.setSelectedRegions(primarySelected);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit selectionChanged();
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
    const bool ocrOrder = m_session.layerRef().source == ThumtooCache::TextLayerSource::Ocr;
    TextLayerGeometry::sortReadingOrder(&order, rects, 4.0, &blocks, pageYUp(), ocrOrder);
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

TextLayerController::SpeakPlan TextLayerController::buildSpeakPlan(bool pageOnly) const
{
    return buildSpeakPlan(pageOnly ? SpeakScope::FullPage : SpeakScope::SelectionOrPage);
}

TextLayerController::SpeakPlan TextLayerController::buildSpeakPlan(SpeakScope scope) const
{
    SpeakPlan plan;

    auto appendJoined = [&](SessionImageId sid, int idx, const QString &rawText, int block,
                            int &cursor, int &prevBlock, bool &havePrev) {
        const QString tx = rawText.trimmed();
        if (tx.isEmpty()) {
            return;
        }
        if (havePrev) {
            QString sep;
            if (prevBlock >= 0 && block >= 0 && prevBlock != block) {
                sep = QStringLiteral("\n\n");
            } else if (plan.text.endsWith(QLatin1Char('-'))
                       && !tx.isEmpty() && tx.at(0).isLetter()) {
                plan.text.chop(1);
                cursor = plan.text.size();
                if (!plan.spans.isEmpty()) {
                    SpeakSpan &prev = plan.spans.last();
                    if (prev.end > prev.start) {
                        prev.end = cursor;
                    }
                }
                sep.clear();
            } else {
                sep = QLatin1Char(' ');
            }
            if (!sep.isEmpty()) {
                plan.text += sep;
                cursor += sep.size();
            }
        }
        SpeakSpan sp;
        sp.sessionId = sid;
        sp.regionIndex = idx;
        sp.start = cursor;
        sp.end = cursor + tx.size();
        plan.spans.append(sp);
        plan.text += tx;
        cursor = sp.end;
        prevBlock = block;
        havePrev = true;
    };

    auto appendLayerOrder = [&](SessionImageId sid, const ThumtooCache::PageTextLayer &layer,
                                int &cursor, int &prevBlock, bool &havePrev) {
        const int n = layer.regions.size();
        if (n <= 0) {
            return;
        }
        QVector<int> order;
        order.reserve(n);
        QVector<QRectF> rects;
        QVector<int> blocks;
        for (int i = 0; i < n; ++i) {
            order.append(i);
            rects.append(layer.regions.at(i).bbox);
            blocks.append(layer.regions.at(i).blockId);
        }
        const bool ocrOrder = layer.source == ThumtooCache::TextLayerSource::Ocr;
        const bool yUp = layer.pageYUp;
        TextLayerGeometry::sortReadingOrder(&order, rects, 4.0, &blocks, yUp, ocrOrder);
        for (int idx : order) {
            if (idx < 0 || idx >= n) {
                continue;
            }
            const auto &r = layer.regions.at(idx);
            appendJoined(sid, idx, r.text, r.blockId, cursor, prevBlock, havePrev);
        }
        // Page break → paragraph break so the splitter does not glue last/first
        // sentences across pages into one utterance.
        if (havePrev && !plan.text.endsWith(QStringLiteral("\n\n"))) {
            plan.text += QStringLiteral("\n\n");
            cursor = plan.text.size();
            prevBlock = -999;
        }
    };

    int cursor = 0;
    int prevBlock = -999;
    bool havePrev = false;

    if (scope == SpeakScope::SelectionOrPage && !m_session.multiSelection.isEmpty()) {
        const_cast<TextLayerController *>(this)->ensureMemberLayers();
        for (const TextSelRef &ref : m_session.multiSelection.refs()) {
            if (ref.regionIndex < 0) {
                continue;
            }
            if (!ref.text.isEmpty()) {
                appendJoined(ref.sessionId, ref.regionIndex, ref.text, -1,
                             cursor, prevBlock, havePrev);
                continue;
            }
            if (!m_view) {
                continue;
            }
            for (ImageItem *item : m_view->liveItems()) {
                if (!item || item->sessionId() != ref.sessionId) {
                    continue;
                }
                const ThumtooCache::PageTextLayer *layer = layerForItem(item);
                if (!layer || ref.regionIndex >= layer->regions.size()) {
                    break;
                }
                const auto &r = layer->regions.at(ref.regionIndex);
                appendJoined(ref.sessionId, ref.regionIndex, r.text, r.blockId,
                             cursor, prevBlock, havePrev);
                break;
            }
        }
        if (!plan.text.isEmpty()) {
            return plan;
        }
    }

    if (scope == SpeakScope::FullDocument && m_view) {
        SessionDocument *doc = m_view->sessionDocument();
        if (doc && !doc->paths().isEmpty()) {
            const QStringList paths = doc->paths();
            const QVector<SessionImageId> ids = doc->ids();
            for (int i = 0; i < paths.size(); ++i) {
                const QString &path = paths.at(i);
                if (path.isEmpty()) {
                    continue;
                }
                SessionImageId sid = (i < ids.size()) ? ids.at(i) : kInvalidSessionImageId;
                ThumtooCache::PageTextLayer layer;
                if (sid != kInvalidSessionImageId && m_memberLayers.contains(sid)
                    && m_memberPaths.value(sid) == path) {
                    layer = m_memberLayers.value(sid);
                } else if (path == m_session.layerPathRef() && m_session.pageBoundsValid()) {
                    layer = m_session.layerRef();
                    if (sid != kInvalidSessionImageId) {
                        const_cast<TextLayerController *>(this)->m_memberLayers.insert(sid, layer);
                        const_cast<TextLayerController *>(this)->m_memberPaths.insert(sid, path);
                    }
                } else {
                    layer = TextLayerResolve::load(path, m_session.layerPreferValue());
                    if (sid != kInvalidSessionImageId && layer.pageBounds.isValid()) {
                        const_cast<TextLayerController *>(this)->m_memberLayers.insert(sid, layer);
                        const_cast<TextLayerController *>(this)->m_memberPaths.insert(sid, path);
                    }
                }
                if (layer.regions.isEmpty()) {
                    continue;
                }
                appendLayerOrder(sid, layer, cursor, prevBlock, havePrev);
            }
            // Trim trailing page-break padding.
            while (plan.text.endsWith(QLatin1Char('\n'))) {
                plan.text.chop(1);
            }
            if (!plan.spans.isEmpty()) {
                // cursor may sit past last span after padding; spans stay valid.
            }
            return plan;
        }
    }

    if (isMultiUnderlay()) {
        const_cast<TextLayerController *>(this)->ensureMemberLayers();
        for (ImageItem *item : m_view->liveItems()) {
            if (!item) {
                continue;
            }
            SessionImageId sid = item->sessionId();
            if (sid == kInvalidSessionImageId) {
                sid = m_view->hostSessionId().currentIdValue();
            }
            const ThumtooCache::PageTextLayer *layer = layerForItem(item);
            if (!layer) {
                continue;
            }
            appendLayerOrder(sid, *layer, cursor, prevBlock, havePrev);
        }
        while (plan.text.endsWith(QLatin1Char('\n'))) {
            plan.text.chop(1);
        }
        return plan;
    }

    if (!hasLayer()) {
        return plan;
    }

    QVector<int> order;
    if (scope == SpeakScope::SelectionOrPage
        && (!m_session.multiSelection.isEmpty() || !m_session.selectedRegionsRef().isEmpty())) {
        order = m_session.selectedRegionsRef();
        if (order.isEmpty() && !m_session.multiSelection.isEmpty()) {
            order = m_session.multiSelection.regionIndicesFor(currentSessionId());
        }
    }
    if (order.isEmpty()) {
        appendLayerOrder(currentSessionId(), m_session.layerRef(), cursor, prevBlock, havePrev);
    } else {
        for (int idx : order) {
            if (idx < 0 || idx >= m_session.regionCount()) {
                continue;
            }
            const auto &r = m_session.regionAt(idx);
            appendJoined(currentSessionId(), idx, r.text, r.blockId, cursor, prevBlock, havePrev);
        }
    }
    while (plan.text.endsWith(QLatin1Char('\n'))) {
        plan.text.chop(1);
    }
    return plan;
}

QString TextLayerController::speakableText() const
{
    // Prefer full document when a session is bound; selection is only an anchor.
    return buildSpeakPlan(SpeakScope::FullDocument).text;
}

QVector<TextLayerController::SpeakSpan> TextLayerController::speakSpans() const
{
    // Must match speakableText() / Speak offsets. pageOnly=false would rebuild a
    // selection-only plan whose start/end no longer match the playing audio.
    return buildSpeakPlan(SpeakScope::FullDocument).spans;
}


int TextLayerController::speakAnchorOffset(const SpeakPlan &plan) const
{
    if (plan.spans.isEmpty()) {
        return 0;
    }
    int anchor = plan.text.size();
    const auto &multi = m_session.multiSelection;
    const auto &sel = m_session.selectedRegionsRef();
    auto consider = [&](SessionImageId sid, int regionIndex) {
        for (const auto &sp : plan.spans) {
            if (sp.regionIndex != regionIndex) {
                continue;
            }
            if (sid != kInvalidSessionImageId && sp.sessionId != kInvalidSessionImageId
                && sid != sp.sessionId) {
                continue;
            }
            int off = sp.start;
            if (m_speakRegionCharBias >= 0
                && m_speakRegionCharBiasIndex == regionIndex
                && (m_speakRegionCharBiasSid == kInvalidSessionImageId
                    || m_speakRegionCharBiasSid == sp.sessionId
                    || sid == kInvalidSessionImageId
                    || sid == m_speakRegionCharBiasSid)) {
                const int spanLen = qMax(0, sp.end - sp.start);
                off = sp.start + qBound(0, m_speakRegionCharBias, qMax(0, spanLen - 1));
            }
            anchor = qMin(anchor, off);
            break;
        }
    };
    if (!multi.isEmpty()) {
        for (const TextSelRef &ref : multi.refs()) {
            consider(ref.sessionId, ref.regionIndex);
        }
    } else {
        const SessionImageId sid = currentSessionId();
        for (int ri : sel) {
            consider(sid, ri);
        }
    }
    if (anchor >= plan.text.size()) {
        return 0;
    }
    return anchor;
}

void TextLayerController::setSpeakingHighlight(const QVector<int> &regionIndices, double progress)
{
    setSpeakingHighlight(kInvalidSessionImageId, regionIndices, progress);
}

void TextLayerController::setSpeakingHighlight(SessionImageId sessionId,
                                               const QVector<int> &regionIndices,
                                               double progress)
{
    m_speakingSessionId = sessionId;
    m_speakingRegions = regionIndices;
    m_speakingProgress = qBound(0.0, progress, 1.0);
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void TextLayerController::clearSpeakingHighlight()
{
    if (m_speakingRegions.isEmpty() && m_speakingProgress == 0.0
        && m_speakingSessionId == kInvalidSessionImageId) {
        return;
    }
    m_speakingRegions.clear();
    m_speakingSessionId = kInvalidSessionImageId;
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
        if (isMultiUnderlay()) {
            ensureMemberLayers();
            bool any = false;
            for (const auto &layer : m_memberLayers) {
                if (!layer.regions.isEmpty()) {
                    any = true;
                    break;
                }
            }
            if (!any) {
                return false;
            }
        } else {
            return false;
        }
    }
    m_session.beginRubber(event->pos());
    m_session.clearSelectedRegions();
    m_view->applyToolCursor(ToolCursors::textSelect());
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
    m_view->restoreToolCursor();
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
        && !m_view->hostAttention().active() && !m_view->hostAnnot().isToolActive()
        && !session().isRubberbanding()
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
            m_view->applyToolCursor(QCursor(Qt::PointingHandCursor));
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
            m_view->restoreToolCursor();
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
    const bool wantPaint = m_session.showsRegions() || m_session.showsGlyphs()
        || m_session.hasSearchMatches() || !m_session.memberSearchMatches.isEmpty()
        || m_session.hasSelection()
        || m_session.hoverRegionIndex() >= 0 || !m_speakingRegions.isEmpty();
    if (!wantPaint) {
        return;
    }

    auto paintItemLayer = [&](ImageItem *item, const ThumtooCache::PageTextLayer &layer,
                              const QString &path, bool isPrimary) {
        if (!item || !layer.pageBounds.isValid()) {
            return;
        }
        const QSize sz = item->imageSize();
        if (sz.width() <= 0 || sz.height() <= 0) {
            return;
        }
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            sid = m_view->hostSessionId().currentIdValue();
        }

        if (m_session.showsRegions()) {
            for (const ThumtooCache::TextRegion &r : layer.regions) {
                if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
                    continue;
                }
                const QRectF img = regionImageRectFor(item, path, layer, r);
                if (img.isEmpty()) {
                    continue;
                }
                const QRectF local = img.translated(item->offset());
                const QPolygonF scenePoly = item->mapToScene(local);
                QPen pen(TextRegionStyle::outlineColor(r));
                pen.setCosmetic(true);
                pen.setWidthF(0);
                painter->setPen(pen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPolygon(scenePoly);
            }
        }

        // Selection from multi bag (spread) or primary selectedRegions.
        QVector<int> selIds;
        if (sid != kInvalidSessionImageId && !m_session.multiSelection.isEmpty()) {
            selIds = m_session.multiSelection.regionIndicesFor(sid);
        } else if (isPrimary) {
            selIds = m_session.selectedRegionsRef();
        }
        if (!selIds.isEmpty()) {
            painter->setPen(QPen(ChromeColors::selectStroke(230), 0));
            painter->setBrush(ChromeColors::selectFill(110));
            for (int idxSel : selIds) {
                if (idxSel < 0 || idxSel >= layer.regions.size()) {
                    continue;
                }
                const auto &r = layer.regions.at(idxSel);
                const QRectF img = regionImageRectFor(item, path, layer, r);
                if (img.isEmpty()) {
                    continue;
                }
                const QRectF local = img.translated(item->offset());
                painter->drawPolygon(item->mapToScene(local));
            }
        }

        if (m_session.showsGlyphs()) {
            for (const ThumtooCache::TextRegion &r : layer.regions) {
                if (r.text.isEmpty()) {
                    continue;
                }
                const QRectF img = regionImageRectFor(item, path, layer, r);
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
                QFont f = painter->font();
                f.setPixelSize(48);
                f.setStyleStrategy(QFont::PreferDefault);
                const QFontMetricsF fm(f);
                const QString line = r.text.simplified();
                qreal advance = fm.horizontalAdvance(line);
                if (advance < 1.0) {
                    advance = 1.0;
                }
                const qreal textH = qMax(qreal(1.0), fm.height());
                painter->save();
                painter->setFont(f);
                painter->setPen(QColor(20, 20, 20, 235));
                painter->setBrush(Qt::NoBrush);
                painter->translate(sceneBox.topLeft());
                painter->scale(sceneBox.width() / advance, sceneBox.height() / textH);
                painter->drawText(QPointF(0.0, fm.ascent()), line);
                painter->restore();
            }
        }
        // Secondary pages: search hits from memberSearchMatches.
        if (!isPrimary) {
            if (sid != kInvalidSessionImageId && !m_session.memberSearchMatches.isEmpty()) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(ChromeColors::searchFill(110));
                for (const TextLayerSession::MemberSearchHit &mh : m_session.memberSearchMatches) {
                    if (mh.sessionId != sid) {
                        continue;
                    }
                    if (mh.hit.regionIndex < 0 || mh.hit.regionIndex >= layer.regions.size()) {
                        continue;
                    }
                    const auto &r = layer.regions.at(mh.hit.regionIndex);
                    QRectF img = regionImageRectFor(item, path, layer, r);
                    if (img.isEmpty()) {
                        continue;
                    }
                    const qreal a = qBound(0.0, mh.hit.startFrac, 1.0);
                    const qreal b = qBound(0.0, mh.hit.endFrac, 1.0);
                    if (b > a && (a > 0.0 || b < 1.0)) {
                        img = QRectF(img.left() + img.width() * a, img.top(),
                                     img.width() * (b - a), img.height());
                    }
                    const QRectF local = img.translated(item->offset());
                    painter->drawPolygon(item->mapToScene(local));
                }
            }
        const bool speakHere = !m_speakingRegions.isEmpty()
            && (m_speakingSessionId == kInvalidSessionImageId
                || m_speakingSessionId == sid
                || (isPrimary && m_speakingSessionId == kInvalidSessionImageId));
        if (speakHere) {
            for (int i = 0; i < m_speakingRegions.size(); ++i) {
                const int idx = m_speakingRegions.at(i);
                if (idx < 0 || idx >= int(layer.regions.size())) {
                    continue;
                }
                const auto &r = layer.regions.at(idx);
                QRectF img = regionImageRectFor(item, path, layer, r);
                if (img.isEmpty()) {
                    continue;
                }
                const bool isActive = (i == m_speakingRegions.size() - 1);
                if (isActive && m_speakingProgress > 0.0 && m_speakingProgress < 1.0) {
                    const qreal x1 = img.left() + img.width() * m_speakingProgress;
                    img = QRectF(QPointF(img.left(), img.top()),
                                 QPointF(x1, img.bottom()));
                }
                painter->setPen(QPen(ChromeColors::activityStroke(230), 0));
                painter->setBrush(ChromeColors::activityFill(isActive ? 130 : 70));
                const QRectF local = img.translated(item->offset());
                painter->drawPolygon(item->mapToScene(local));
            }
        }
            return;
        }
        if (m_session.hasSearchMatches()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(ChromeColors::searchFill(110));
            for (const TextSearchPolicy::SearchHit &hit : m_session.searchMatchesRef()) {
                if (hit.regionIndex < 0 || hit.regionIndex >= layer.regions.size()) {
                    continue;
                }
                const auto &r = layer.regions.at(hit.regionIndex);
                QRectF img = regionImageRectFor(item, path, layer, r);
                if (img.isEmpty()) {
                    continue;
                }
                const qreal a = qBound(0.0, hit.startFrac, 1.0);
                const qreal b = qBound(0.0, hit.endFrac, 1.0);
                if (b > a && (a > 0.0 || b < 1.0)) {
                    img = QRectF(img.left() + img.width() * a, img.top(),
                                 img.width() * (b - a), img.height());
                }
                const QRectF local = img.translated(item->offset());
                painter->drawPolygon(item->mapToScene(local));
            }
        }
        if (m_session.hoverRegionIndex() >= 0
            && m_session.hoverRegionIndex() < layer.regions.size()) {
            const auto &r = layer.regions.at(m_session.hoverRegionIndex());
            const QRectF img = regionImageRectFor(item, path, layer, r);
            if (!img.isEmpty()) {
                painter->setPen(QPen(ChromeColors::viewStroke(230), 0));
                painter->setBrush(ChromeColors::viewFill(60));
                const QRectF local = img.translated(item->offset());
                painter->drawPolygon(item->mapToScene(local));
            }
        }
        const bool speakHere = !m_speakingRegions.isEmpty()
            && (m_speakingSessionId == kInvalidSessionImageId
                || m_speakingSessionId == sid
                || (isPrimary && m_speakingSessionId == kInvalidSessionImageId));
        if (speakHere) {
            for (int i = 0; i < m_speakingRegions.size(); ++i) {
                const int idx = m_speakingRegions.at(i);
                if (idx < 0 || idx >= int(layer.regions.size())) {
                    continue;
                }
                const auto &r = layer.regions.at(idx);
                QRectF img = regionImageRectFor(item, path, layer, r);
                if (img.isEmpty()) {
                    continue;
                }
                const bool isActive = (i == m_speakingRegions.size() - 1);
                if (isActive && m_speakingProgress > 0.0 && m_speakingProgress < 1.0) {
                    const qreal x1 = img.left() + img.width() * m_speakingProgress;
                    img = QRectF(QPointF(img.left(), img.top()),
                                 QPointF(x1, img.bottom()));
                }
                painter->setPen(QPen(ChromeColors::activityStroke(230), 0));
                painter->setBrush(ChromeColors::activityFill(isActive ? 130 : 70));
                const QRectF local = img.translated(item->offset());
                painter->drawPolygon(item->mapToScene(local));
            }
        }
    };

    painter->save();
    ImageItem *primary = m_view->primaryItem();
    if (isMultiUnderlay()) {
        for (ImageItem *item : m_view->liveItems()) {
            const ThumtooCache::PageTextLayer *layer = layerForItem(item);
            if (!layer) {
                continue;
            }
            paintItemLayer(item, *layer, pathForItem(item), item == primary);
        }
    } else if (primary && m_session.hasRegions()) {
        paintItemLayer(primary, m_session.layerRef(), m_view->hostImage().classicPath(), true);
    }
    painter->restore();
}


