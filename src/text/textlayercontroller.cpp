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
    const QString qn = TextSearchPolicy::normalizeForSearch(m_session.searchQueryRef());
    const QString qa = TextSearchPolicy::alnumOnly(m_session.searchQueryRef());
    for (int i = 0; i < m_session.regionCount(); ++i) {
        const auto &r = m_session.regionAt(i);
        if (r.text.isEmpty()) {
            continue;
        }
        if (TextSearchPolicy::regionMatchesQuery(r.text, qn, qa, m_session.isSearchFuzzy())) {
            m_session.addSearchMatch(i);
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
