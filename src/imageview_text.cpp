// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page text / link regions, search, rubber-band selection (TextLayerSession).

#include "imageview.h"
#include "imageitem.h"
#include "textlayersession.h"
#include "textlayergeometry.h"
#include "textsearchpolicy.h"
#include "thumtoocache.h"
#include "contentxform.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QMouseEvent>

void ImageView::paintTextRubberBandOverlay(QPainter &painter)
{
    if (m_textLayer.isRubberbanding() && m_textLayer.hasRubberRect()) {
        painter.save();
        QPen pen(QColor(40, 120, 220, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(QColor(60, 160, 255, 40));
        painter.drawRect(m_textLayer.rubberRectRef().normalized());
        painter.restore();
    }

}


void ImageView::setShowTextRegions(bool on)
{
    if (!m_textLayer.setShowRegions(on)) {
        return;
    }
    if (m_textLayer.needsLayer()) {
        refreshTextLayer();
    } else {
        m_textLayer.resetLayerContent();
        m_textLayer.clearSearchMatches();
    }
    viewport()->update();
}


void ImageView::refreshTextLayer()
{
    m_textLayer.resetLayerContent();
    m_textLayer.clearSearchMatches();
    if (!m_textLayer.needsLayer()) {
        return;
    }
    // Search/outlines need a page ref; allow extract outside pure Image mode
    // (e.g. user opened Find while still on a page path).
    const QString path = classicPath();
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return;
    }
    // Prefer cache; ensure may do source I/O (acceptable for toggle / Find).
    ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(path);
    if (layer.regions.isEmpty()) {
        layer = ThumtooCache::ensurePageTextLayer(path);
    }
    m_textLayer.setLayerContent(layer, path);
    if (m_textLayer.hasSearchQuery()) {
        recomputeTextSearchMatches();
    }
}


bool ImageView::textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy)
{
    return TextSearchPolicy::matches(regionText, query, fuzzy);
}


void ImageView::setTextSearchFuzzy(bool on)
{
    if (!m_textLayer.setSearchFuzzy(on)) {
        return;
    }
    if (m_textLayer.hasSearchQuery()) {
        recomputeTextSearchMatches();
        viewport()->update();
    }
}


void ImageView::recomputeTextSearchMatches()
{
    m_textLayer.clearSearchMatches();
    if (!m_textLayer.hasSearchQuery() || !m_textLayer.hasRegions()) {
        return;
    }
    const QString qn = TextSearchPolicy::normalizeForSearch(m_textLayer.searchQueryRef());
    const QString qa = TextSearchPolicy::alnumOnly(m_textLayer.searchQueryRef());
    for (int i = 0; i < m_textLayer.regionCount(); ++i) {
        const auto &r = m_textLayer.regionAt(i);
        if (r.text.isEmpty()) {
            continue;
        }
        if (TextSearchPolicy::regionMatchesQuery(r.text, qn, qa, m_textLayer.isSearchFuzzy())) {
            m_textLayer.addSearchMatch(i);
        }
    }
}


int ImageView::setTextSearchQuery(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (m_textLayer.searchQueryRef() == trimmed && m_textLayer.hasRegions()) {
        return m_textLayer.searchMatchesRef().size();
    }
    m_textLayer.setSearchQuery(trimmed);
    if (!m_textLayer.hasSearchQuery()) {
        m_textLayer.clearSearchMatches();
        if (!m_textLayer.showsRegions()) {
            m_textLayer.resetLayerContent();
        }
        viewport()->update();
        return 0;
    }
    // Ensure layer is loaded for the current page.
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        refreshTextLayer();
    } else {
        recomputeTextSearchMatches();
    }
    viewport()->update();
    return m_textLayer.searchMatchesRef().size();
}


bool ImageView::hasTextLayer() const
{
    return m_textLayer.hasRegions() && m_textLayer.layerPathRef() == classicPath();
}


int ImageView::textLayerRegionCount() const
{
    if (!hasTextLayer()) {
        return 0;
    }
    return m_textLayer.regionCount();
}


bool ImageView::hitTextLinkAt(const QPoint &viewPos, int *pageOut, QString *uriOut) const
{
    if (pageOut) {
        *pageOut = 0;
    }
    if (uriOut) {
        uriOut->clear();
    }
    if (!isImageMode() || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    ImageItem *item = primaryItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        return false;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0 || !m_textLayer.pageBoundsValid()) {
        return false;
    }
    const QPointF scene = mapToScene(viewPos);
    const QPointF local = item->mapFromScene(scene);
    if (!item->contentRect().contains(local)) {
        return false;
    }
    const QPointF imgPt = local - item->offset();
    for (const ThumtooCache::TextRegion &r : m_textLayer.regions()) {
        if (r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        const QRectF img = textRegionImageRect(r);
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


bool ImageView::pageYUpForTextLayer() const
{
    const QString docPath = PagePath::documentFilePath(classicPath());
    return PagePath::isDjvuFile(docPath);
}


QRectF ImageView::textRegionImageRect(const ThumtooCache::TextRegion &region) const
{
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.pageBoundsValid()) {
        return {};
    }

    // Full unoriented page raster size — never recover from oriented imageSize().
    // Display size after 1–3 turns is swapped; after 4 turns QImage may drift by
    // a pixel; crop also changes imageSize(). Page text is authored against the
    // native page raster.
    const QString path = classicPath();
    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        const QSize known = m_sizeBook.known(path);
        if (!known.isEmpty()) {
            sourceSize = known;
        }
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        // Last resort: invert orientation from the live item (no crop only).
        sourceSize = item->imageSize();
        WorkspaceItemState stGuess;
        if (item->sessionId() != kInvalidSessionImageId) {
            stGuess = sessionAppearanceValue(item->sessionId());
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
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    if (sid != kInvalidSessionImageId) {
        st = sessionAppearanceValue(sid);
    }
    // Durable XDG row when session slot is empty (same as imageWithSessionAppearance).
    if (!SessionAppearance::hasContentAppearance(st) && !path.isEmpty()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && (stored.contentHFlip || stored.contentVFlip
                || stored.contentQuarterTurns != 0 || stored.hasCrop)) {
            st.contentHFlip = stored.contentHFlip;
            st.contentVFlip = stored.contentVFlip;
            st.contentQuarterTurns = stored.contentQuarterTurns;
            st.hasCrop = stored.hasCrop;
            st.cropRect = stored.cropRect;
            st.cropSourceSize = stored.cropSourceSize;
            st.cropRotation = stored.cropRotation;
        }
    }

    const bool pageYUp = pageYUpForTextLayer();
    const QRectF inSource = ThumtooCache::pageRectToImageRect(
        region.bbox, m_textLayer.pageBounds(), sourceSize, pageYUp);
    if (inSource.isEmpty()) {
        return {};
    }

    // Flip → CW quarter-turns → crop (see docs/CONTENT_COORDINATES.md).
    // Content geometry is intrinsic/logical (imageSize / offset / contentRect).
    // Soft samples are painted *into* that rect — never rescale text into the
    // soft pixmap pixel size (that made highlights tiny until full res arrived).
    return SessionAppearance::mapSourceRectToContentDisplay(inSource, sourceSize, st);
}


QRectF ImageView::textRubberBandImageRect() const
{
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.hasRubberRect()) {
        return {};
    }
    const QRectF sceneRect = mapToScene(m_textLayer.rubberRectRef()).boundingRect();
    // Map scene corners to item local, then subtract offset → image pixels.
    const QRectF local = item->mapFromScene(sceneRect).boundingRect();
    return local.translated(-item->offset());
}


void ImageView::finishTextRubberBand()
{
    const QRect viewRect = m_textLayer.rubberRectRef().normalized();
    m_textLayer.endRubber();
    m_textLayer.clearSelectedRegions();
    if (viewRect.width() < 4 || viewRect.height() < 4) {
        viewport()->update();
        return;
    }
    // Ensure text layer (refreshTextLayer skips when neither search nor outlines).
    if (!m_textLayer.hasRegions() || m_textLayer.layerPathRef() != classicPath()) {
        const bool hadShow = m_textLayer.showsRegions();
        m_textLayer.setShowRegions(true);
        refreshTextLayer();
        m_textLayer.setShowRegions(hadShow);
    }
    ImageItem *item = primaryItem();
    if (!item || !m_textLayer.hasRegions() || !m_textLayer.pageBoundsValid()) {
        viewport()->update();
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() <= 0 || sz.height() <= 0) {
        viewport()->update();
        return;
    }
    // Recompute rubber from stored origin - use viewRect mapped to image.
    m_textLayer.setRubberRect(viewRect);
    const QRectF imgRubber = textRubberBandImageRect();
    m_textLayer.clearRubberRect();
    if (imgRubber.isEmpty()) {
        viewport()->update();
        return;
    }
    QVector<QRectF> regionRects(m_textLayer.regionCount());
    for (int i = 0; i < m_textLayer.regionCount(); ++i) {
        const auto &r = m_textLayer.regionAt(i);
        if (r.text.isEmpty() && r.role != ThumtooCache::TextRegion::Role::Link) {
            continue;
        }
        regionRects[i] = textRegionImageRect(r);
    }
    QVector<int> selected =
        TextLayerGeometry::indicesIntersecting(regionRects, imgRubber);
    // Reading order: top-to-bottom, then left-to-right by image rect.
    TextLayerGeometry::sortReadingOrder(&selected, regionRects);
    m_textLayer.setSelectedRegions(selected);
    viewport()->update();
}


QString ImageView::selectedText() const
{
    QStringList lines;
    for (int idx : m_textLayer.selectedRegionsRef()) {
        if (idx < 0 || idx >= m_textLayer.regionCount()) {
            continue;
        }
        const QString &tx = m_textLayer.regionAt(idx).text;
        if (!tx.isEmpty()) {
            lines.append(tx);
        }
    }
    return lines.join(QLatin1Char('\n'));
}


void ImageView::clearTextSelection()
{
    if (!m_textLayer.hasSelection() && !m_textLayer.isRubberbanding()) {
        return;
    }
    m_textLayer.clearSelection();
    viewport()->update();
}


bool ImageView::copySelectedText()
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


// --- input try* (text rubber) from imageview_input.cpp ---

bool ImageView::tryMousePressTextRubber(QMouseEvent *event)
{
    if (!isImageMode() || m_cropCtrl.session().active() || m_attentionCtrl.session().active()
        || event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ShiftModifier)
        || (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier))
        || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    m_textLayer.beginRubber(event->pos());
    m_textLayer.clearSelectedRegions();
    setCursor(Qt::CrossCursor);
    viewport()->update();
    event->accept();
    return true;
}

bool ImageView::tryMouseMoveTextRubber(QMouseEvent *event)
{
    if (!m_textLayer.isRubberbanding() || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    m_textLayer.updateRubber(event->pos());
    viewport()->update();
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseTextRubber(QMouseEvent *event)
{
    if (!m_textLayer.isRubberbanding() || event->button() != Qt::LeftButton) {
        return false;
    }
    m_textLayer.updateRubber(event->pos());
    finishTextRubberBand();
    unsetCursor();
    event->accept();
    return true;
}
