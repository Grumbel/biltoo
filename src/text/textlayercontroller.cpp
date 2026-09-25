// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page text / link regions, search, rubber-band selection (owned by TextLayerController).

#include "text/textlayercontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "text/textlayergeometry.h"
#include "text/textsearchpolicy.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "session/sessionappearance.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QWidget>

TextLayerController::TextLayerController(ImageView *view)
    : m_view(view)
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
    if (!m_session.needsLayer()) {
        return;
    }
    const QString path = m_view->hostImage().classicPath();
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return;
    }
    ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(path);
    if (layer.regions.isEmpty()) {
        layer = ThumtooCache::ensurePageTextLayer(path);
    }
    m_session.setLayerContent(layer, path);
    if (m_session.hasSearchQuery()) {
        recomputeSearchMatches();
    }
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

void TextLayerController::recomputeSearchMatches()
{
    m_session.clearSearchMatches();
    if (!m_session.hasSearchQuery() || !m_session.hasRegions()) {
        return;
    }
    QVector<QString> texts;
    QVector<QRectF> bboxes;
    texts.reserve(m_session.regionCount());
    bboxes.reserve(m_session.regionCount());
    for (int i = 0; i < m_session.regionCount(); ++i) {
        const auto &r = m_session.regionAt(i);
        texts.append(r.text);
        // Page-space bbox is enough for reading-order sort (same units).
        bboxes.append(r.bbox);
    }
    const QVector<TextSearchPolicy::SearchHit> hits = TextSearchPolicy::findHits(
        texts, bboxes, m_session.searchQueryRef(), m_session.isSearchFuzzy());
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
    const QString docPath = PagePath::documentFilePath(m_view->hostImage().classicPath());
    return PagePath::isDjvuFile(docPath);
}

QRectF TextLayerController::regionImageRect(const ThumtooCache::TextRegion &region) const
{
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

    return SessionAppearance::mapSourceRectToContentDisplay(inSource, sourceSize, st);
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

void TextLayerController::finishRubberBand()
{
    const QRect viewRect = m_session.rubberRectRef().normalized();
    m_session.endRubber();
    m_session.clearSelectedRegions();
    if (viewRect.width() < 4 || viewRect.height() < 4) {
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
    TextLayerGeometry::sortReadingOrder(&selected, regionRects);
    m_session.setSelectedRegions(selected);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

QString TextLayerController::selectedText() const
{
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

void TextLayerController::clearSelection()
{
    if (!m_session.hasSelection() && !m_session.isRubberbanding()) {
        return;
    }
    m_session.clearSelection();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
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
        || !(event->modifiers() & Qt::ShiftModifier)
        || (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier))
        || !PagePath::isPageRef(m_view->hostImage().classicPath())) {
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
    if (!m_session.hasRegions()
        || !(m_session.showsRegions() || m_session.hasSearchMatches())) {
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
    // Search hits: filled yellow first (under outlines / selection).
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
    // Rubber-band text selection (cyan).
    if (m_session.hasSelection()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(60, 160, 255, 100));
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
    if (m_session.showsRegions()) {
        painter->setBrush(Qt::NoBrush);
        for (const ThumtooCache::TextRegion &r : m_session.regions()) {
            const QRectF img = regionImageRect(r);
            if (img.isEmpty()) {
                continue;
            }
            const QRectF local = img.translated(item->offset());
            const QPolygonF scenePoly = item->mapToScene(local);
            if (r.role == ThumtooCache::TextRegion::Role::Link) {
                QPen pen(QColor(40, 180, 80, 200));
                pen.setCosmetic(true);
                pen.setWidthF(0);
                painter->setPen(pen);
            } else {
                QPen pen(QColor(220, 80, 40, 180));
                pen.setCosmetic(true);
                pen.setWidthF(0);
                painter->setPen(pen);
            }
            painter->drawPolygon(scenePoly);
        }
    }
    painter->restore();
}

