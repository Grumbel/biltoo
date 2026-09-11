// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"
#include "archivepath.h"
#include "pagepath.h"
#include "imagecache.h"
#include "imageloader.h"
#include "thumtoocache.h"
#include "sessionappearance.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPointer>
#include <QPainter>
#include <QPaintDevice>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QThreadPool>
#include <QScrollBar>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

// ---------------------------------------------------------------------------
// Filmstrip layout — see docs/FILMSTRIP_LAYOUT.md (authoritative).
//
// thumbSize = logical length of the image on the strip *cross-axis*.
// Crop: square thumbSize². Letterbox: cross-axis edge = thumbSize, other edge
// from aspect (landscape wider on a horizontal bar). Layout uses logical sizes
// only; decode ladder edge is for sharpness, never for sizeHint.
// ---------------------------------------------------------------------------

ThumbnailDelegate::ThumbnailDelegate(int thumbSize, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_thumbSize(thumbSize)
{
}

void ThumbnailDelegate::setThumbSize(int pixels)
{
    m_thumbSize = pixels;
}

int ThumbnailDelegate::cellPad() const
{
    // Cross-axis margin (top/bottom on a horizontal bar, left/right on vertical).
    // Absolute filmstrip logical pixels — tracks thumbSize so large thumbs keep
    // a visible margin against the bar edge.
    return qBound(2, m_thumbSize / 24, 6);
}

int ThumbnailDelegate::flowPad() const
{
    // Half of cellPad on each flow-axis side so adjacent cells contribute
    // ~cellPad of empty space between image contents (plus item spacing 0/1).
    return cellPad() / 2;
}

int ThumbnailDelegate::labelBandHeightForFont(const QFont &font)
{
    return QFontMetrics(font).height() + kLabelGap;
}

int ThumbnailDelegate::labelBandHeight(const QFont &font) const
{
    return m_labelsVisible ? labelBandHeightForFont(font) : 0;
}

void ThumbnailDelegate::setLabelsVisible(bool on)
{
    m_labelsVisible = on;
}

QSize ThumbnailDelegate::cellSize(const QFont &font) const
{
    // Crop cells: same orientation-aware pad model as letterbox.
    const int labelH = labelBandHeight(font);
    const int cross = cellPad();
    const int flow = flowPad();
    Qt::Orientation orient = Qt::Horizontal;
    if (const auto *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        orient = bar->barOrientation();
    }
    if (orient == Qt::Horizontal) {
        return QSize(m_thumbSize + 2 * flow, cross + m_thumbSize + cross + labelH);
    }
    return QSize(cross + m_thumbSize + cross, flow + m_thumbSize + flow + labelH);
}

QSize ThumbnailDelegate::letterboxContentSize(QSize aspect) const
{
    // Logical content size: cross-axis = thumbSize, other edge from aspect.
    if (aspect.width() < 1 || aspect.height() < 1) {
        return QSize(m_thumbSize, m_thumbSize);
    }
    Qt::Orientation orient = Qt::Horizontal;
    if (const auto *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        orient = bar->barOrientation();
    }
    if (orient == Qt::Horizontal) {
        const int h = m_thumbSize;
        const int w = qMax(1, int(qRound(qreal(m_thumbSize) * qreal(aspect.width())
                                         / qreal(aspect.height()))));
        return QSize(w, h);
    }
    const int w = m_thumbSize;
    const int h = qMax(1, int(qRound(qreal(m_thumbSize) * qreal(aspect.height())
                                     / qreal(aspect.width()))));
    return QSize(w, h);
}

QSize ThumbnailDelegate::cellSizeForContent(const QFont &font, QSize contentAspect) const
{
    // Hug logical letterbox content. Cross-axis uses full cellPad (matches bar
    // edge margin); flow-axis uses flowPad so inter-image gap ≈ cellPad.
    const QSize content = letterboxContentSize(contentAspect);
    const int labelH = labelBandHeight(font);
    const int cross = cellPad();
    const int flow = flowPad();
    Qt::Orientation orient = Qt::Horizontal;
    if (const auto *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        orient = bar->barOrientation();
    }
    if (orient == Qt::Horizontal) {
        return QSize(content.width() + 2 * flow,
                     cross + content.height() + cross + labelH);
    }
    return QSize(cross + content.width() + cross,
                 flow + content.height() + flow + labelH);
}

QSize ThumbnailDelegate::provisionalContentSize() const
{
    // Square placeholder: same cross-axis as letterbox (thumbSize). A tall
    // provisional (e.g. 3:4) made narrow cells; when landscape content loaded
    // paint intersected into that narrow slot and thumbs looked tiny.
    return QSize(1, 1);
}

QSize ThumbnailDelegate::logicalContentSize(const QModelIndex &index) const
{
    // Single source for paint + sizeHint (see docs/FILMSTRIP_LAYOUT.md).
    bool crop = false;
    if (const auto *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        crop = bar->cropToSquare();
    }
    if (crop) {
        return QSize(m_thumbSize, m_thumbSize);
    }
    // Prefer stored logical size (already at thumbSize cross-axis). Re-run
    // letterboxContentSize so a thumbSize change still scales correctly when
    // only the aspect ratio in the role is reliable.
    const QSize role = index.data(ThumbContentSizeRole).toSize();
    if (role.width() > 0 && role.height() > 0) {
        return letterboxContentSize(role);
    }
    const QPixmap pm = qvariant_cast<QPixmap>(index.data(ThumbPixmapRole));
    if (!pm.isNull() && pm.width() > 0 && pm.height() > 0) {
        return letterboxContentSize(pm.size());
    }
    return letterboxContentSize(provisionalContentSize());
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    bool crop = false;
    if (const auto *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        crop = bar->cropToSquare();
    }
    if (crop) {
        return cellSize(option.font);
    }
    return cellSizeForContent(option.font, logicalContentSize(index));
}

void ThumbnailDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect cell = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // Full-cell chrome (stable; does not shrink to letterboxed image).
    if (selected) {
        painter->fillRect(cell, option.palette.brush(QPalette::Highlight));
    } else if (hovered) {
        QColor c = option.palette.color(QPalette::Highlight);
        c.setAlpha(48);
        painter->fillRect(cell, c);
    }

    const QFontMetrics fm(option.font);
    const int labelBand = labelBandHeight(option.font);
    const int cross = cellPad();
    const int flow = flowPad();
    Qt::Orientation orient = Qt::Horizontal;
    if (const auto *barOrient = qobject_cast<const ThumbnailBar *>(parent())) {
        orient = barOrient->barOrientation();
    }

    // Prepared pixmap (correct aspect). Never QIcon::pixmap(w,h) — stretches.
    QPixmap pm = qvariant_cast<QPixmap>(index.data(ThumbPixmapRole));
    if (pm.isNull()) {
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QList<QSize> sizes = icon.availableSizes();
            if (!sizes.isEmpty()) {
                pm = icon.pixmap(sizes.constFirst());
            }
        }
    }

    // Image area: full cellPad on cross-axis, flowPad on flow-axis (label under).
    QRect inner;
    if (orient == Qt::Horizontal) {
        inner = cell.adjusted(flow, cross, -flow, -(cross + labelBand));
    } else {
        inner = cell.adjusted(cross, flow, -cross, -(flow + labelBand));
    }

    QRect contentRect;
    if (!pm.isNull() && inner.width() > 0 && inner.height() > 0) {
        // Same logical size as sizeHint (logicalContentSize).
        const QSize contentSz = logicalContentSize(index);
        const QSize fitted = contentSz.scaled(inner.size(), Qt::KeepAspectRatio);
        contentRect = QRect(
            inner.x() + (inner.width() - fitted.width()) / 2,
            inner.y() + (inner.height() - fitted.height()) / 2,
            qMax(1, fitted.width()),
            qMax(1, fitted.height()));
        painter->drawPixmap(contentRect, pm);
    }

    // Hairline on the image bounds (not the full cell).
    if (!pm.isNull() && contentRect.width() > 0 && contentRect.height() > 0) {
        painter->setPen(QPen(QColor(0, 0, 0), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(contentRect.adjusted(0, 0, -1, -1));
    } else if (pm.isNull()) {
        // Loading / pending placeholder — subtle frame + busy mark.
        const QRect slot = inner;
        painter->setPen(QPen(QColor(128, 128, 128, 160), 1, Qt::DashLine));
        painter->setBrush(QColor(0, 0, 0, 40));
        painter->drawRect(slot.adjusted(0, 0, -1, -1));
        const ThumbnailBar *bar = qobject_cast<const ThumbnailBar *>(parent());
        if (bar && bar->isRowLoading(index.row())) {
            const int s = qBound(6, qMin(slot.width(), slot.height()) / 4, 18);
            const QRect pip(slot.center().x() - s / 2,
                            slot.center().y() - s / 2, s, s);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(180, 180, 180, 200));
            painter->drawEllipse(pip);
        }
    }

    const QString text = index.data(Qt::DisplayRole).toString();
    if (m_labelsVisible && !text.isEmpty() && labelBand > 0) {
        // Label uses flow-axis side inset on horizontal bars; cross on vertical.
        const int labelInset = (orient == Qt::Horizontal) ? flow : cross;
        const QRect textRect(cell.left() + labelInset,
                             cell.bottom() - labelBand + kLabelGap,
                             qMax(1, cell.width() - 2 * labelInset),
                             fm.height());
        const QColor textColor = selected
            ? option.palette.color(QPalette::HighlightedText)
            : option.palette.color(QPalette::Text);
        painter->setPen(textColor);
        painter->setFont(option.font);
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                          fm.elidedText(text, Qt::ElideMiddle, textRect.width()));
    }


    // Workspace membership: dog-ear fold on the top-right of the thumbnail.
    // Resolve ThumbnailBar via the delegate parent (always the bar). Do not rely
    // on option.widget->parentWidget(): option.widget is often the list itself,
    // so parentWidget() is the splitter and the cast fails — badge never draws.
    const ThumbnailBar *bar = qobject_cast<const ThumbnailBar *>(parent());
    if (!bar && option.widget) {
        bar = qobject_cast<const ThumbnailBar *>(option.widget);
        if (!bar) {
            bar = qobject_cast<const ThumbnailBar *>(option.widget->parentWidget());
        }
    }
    if (bar && bar->isOnCanvas(index.row()) && contentRect.width() > 8) {
            const int fold = qBound(10, contentRect.width() / 4, 28);
            const QPoint topRight(contentRect.right() + 1, contentRect.top());
            const QPoint left(topRight.x() - fold, topRight.y());
            const QPoint bottom(topRight.x(), topRight.y() + fold);

            // Cover the corner of the image with the folded page face.
            QPainterPath face;
            face.moveTo(left);
            face.lineTo(topRight);
            face.lineTo(bottom);
            face.closeSubpath();
            painter->setPen(Qt::NoPen);
            // Accent that stays visible on both light and dark thumbnails.
            painter->setBrush(QColor(53, 132, 228));
            painter->drawPath(face);

            // Underside of the fold (slightly darker triangle along the diagonal).
            const QPoint mid((left.x() + bottom.x()) / 2, (left.y() + bottom.y()) / 2);
            QPainterPath under;
            under.moveTo(left);
            under.lineTo(mid);
            under.lineTo(bottom);
            under.closeSubpath();
            painter->setBrush(QColor(30, 90, 180));
            painter->drawPath(under);

            // Soft edge so the fold sits on the image.
            painter->setPen(QPen(QColor(0, 0, 0, 90), 1.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(left, bottom);
    }

    painter->restore();
}

// ---------------------------------------------------------------------------
// ThumbnailBar
// ---------------------------------------------------------------------------

ThumbnailBar::ThumbnailBar(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListWidget::IconMode);
    setResizeMode(QListWidget::Adjust);
    setMovement(QListWidget::Static);
    // Final spacing set in applyThumbMetrics once delegate exists (cellPad % 2).
    setSpacing(0);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::ClickFocus);
    setWordWrap(false);
    setTextElideMode(Qt::ElideMiddle);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
    setStatusTip(tr("Drag thumbnails onto the Workspace canvas to place them; "
                    "double-click or Enter opens the image"));
    setToolTip(tr("Drag to Workspace · double-click opens image"));

    QFont captionFont = font();
    if (captionFont.pointSizeF() > 0) {
        captionFont.setPointSizeF(qMax(8.0, captionFont.pointSizeF() - 1.0));
    } else if (captionFont.pixelSize() > 0) {
        captionFont.setPixelSize(qMax(10, captionFont.pixelSize() - 1));
    }
    setFont(captionFont);

    m_delegate = new ThumbnailDelegate(m_thumbSize, this);
    setItemDelegate(m_delegate);

    qRegisterMetaType<QImage>("QImage");

    applyOrientation();
    applyThumbMetrics();

    connect(this, &QListWidget::itemActivated, this, &ThumbnailBar::onItemActivated);
    connect(this, &QListWidget::currentRowChanged, this, &ThumbnailBar::onCurrentRowChanged);
    if (horizontalScrollBar()) {
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
                &ThumbnailBar::scheduleVisibleThumbnailLoads);
    }
    if (verticalScrollBar()) {
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
                &ThumbnailBar::scheduleVisibleThumbnailLoads);
    }

    // When thumtoo finishes a ladder level, only fill filmstrip rows that were
    // still waiting for a first thumb. Gallery/Image ladder growth for the same
    // path must not rewrite an already-settled filmstrip icon (selection focus
    // upgrades soft ladder and would otherwise "sharpen" the strip on click).
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::sizeReady, this,
            [this](const QString &path, const QSize &size) {
                if (path.isEmpty() || m_cropToSquare || !m_delegate
                    || !size.isValid() || size.width() < 1 || size.height() < 1) {
                    return;
                }
                bool any = false;
                for (int i = 0; i < m_files.size(); ++i) {
                    if (m_files.at(i) != path) {
                        continue;
                    }
                    QListWidgetItem *it = item(i);
                    if (!it) {
                        continue;
                    }
                    // Loaded thumb already fixed aspect from pixels.
                    if (it->data(ThumbnailDelegate::ThumbLoadedRole).toBool()) {
                        continue;
                    }
                    applyNativeAspect(it, size);
                    any = true;
                }
                if (any) {
                    doItemsLayout();
                    updateCenteringMargins();
                }
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            [this](const QString &path, int /*maxEdge*/, const QImage &) {
                if (path.isEmpty() || m_files.isEmpty()) {
                    return;
                }
                const int decodeSize = filmstripDecodeEdge();
                const quint64 gen = m_generation.load();
                for (int i = 0; i < m_files.size(); ++i) {
                    if (m_files.at(i) != path) {
                        continue;
                    }
                    if (i < m_sessionIds.size()) {
                        const SessionImageId sid = m_sessionIds.at(i);
                        if (sid != kInvalidSessionImageId
                            && m_sessionIdImageOverrides.contains(sid)) {
                            continue;
                        }
                    }
                    const bool wasAwaiting = m_thumbAwaitLadder.contains(i);
                    int haveEdge = 0;
                    if (QListWidgetItem *it = item(i)) {
                        haveEdge =
                            it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
                    }
                    // Soft stand-in still upgrades when display edge is higher.
                    if (haveEdge >= decodeSize * 9 / 10 && !wasAwaiting) {
                        continue;
                    }
                    m_thumbAwaitLadder.remove(i);
                    m_thumbLoadScheduled.remove(i);
                    const QPointer<ThumbnailBar> guard(this);
                    QThreadPool::globalInstance()->start([guard, i, path, gen, decodeSize]() {
                        ThumbnailBar *bar = guard.data();
                        if (!bar || gen != bar->m_generation.load()) {
                            return;
                        }
                        const QImage image = bar->makeThumbnail(path, decodeSize);
                        bar = guard.data();
                        if (!bar || gen != bar->m_generation.load()) {
                            return;
                        }
                        QMetaObject::invokeMethod(bar, [guard, i, path, gen, image]() {
                            ThumbnailBar *const host = guard.data();
                            if (!host || gen != host->m_generation.load()) {
                                return;
                            }
                            if (i < 0 || i >= host->m_files.size()
                                || host->m_files.at(i) != path) {
                                return;
                            }
                            if (image.isNull()) {
                                // Ladder finished but still undecodable — settle so we
                                // do not re-enter scheduleVisible → pool forever.
                                host->m_thumbFailed.insert(i);
                                host->m_thumbAwaitLadder.remove(i);
                                host->m_thumbLoadScheduled.remove(i);
                                host->viewport()->update();
                                emit host->loadsChanged();
                                return;
                            }
                            host->m_thumbFailed.remove(i);
                            host->setThumbnailIcon(i, image);
                            emit host->loadsChanged();
                        }, Qt::QueuedConnection);
                    });
                }
            });
}

ThumbnailBar::~ThumbnailBar()
{
    // cancelPendingLoads() emits loadsChanged → MainWindow::updateStatus.
    // At shutdown MainWindow is already being destroyed (we are a child of the
    // central splitter); delivering that slot asserts under Qt 6.11.
    blockSignals(true);
    cancelPendingLoads();
}

int ThumbnailBar::labelBandHeight() const
{
    if (!m_labelsVisible) {
        return 0;
    }
    return ThumbnailDelegate::labelBandHeightForFont(font());
}

int ThumbnailBar::extentForThumbSize(int thumbSize)
{
    // Approximate for callers without a live widget (default app font).
    const int pad = qBound(1, thumbSize / 20, 6);
    return 2 * pad + thumbSize
        + ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
}

int ThumbnailBar::thumbSizeForExtent(int extent)
{
    const int label = ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
    // Inverse of extentForThumbSize with pad ≈ thumb/16 — iterate a step.
    int thumb = extent - label;
    for (int i = 0; i < 3; ++i) {
        const int pad = qBound(1, thumb / 20, 6);
        thumb = qBound(kMinThumbSize, extent - label - 2 * pad, kMaxThumbSize);
    }
    return thumb;
}

int ThumbnailBar::thumbSizeFromBarExtent(int extent) const
{
    if (m_orientation == Qt::Horizontal) {
        // extent is bar height = cell height = pads + thumb + labelBand
        const int pad = m_delegate ? m_delegate->cellPad() : qBound(1, m_thumbSize / 16, 6);
        return qBound(kMinThumbSize,
                     extent - labelBandHeight() - 2 * pad,
                     kMaxThumbSize);
    }
    // Vertical bar: extent is bar width ≈ cell width = thumb + 2*pad
    return qBound(kMinThumbSize,
                 extent - 2 * (m_delegate ? m_delegate->cellPad() : 4),
                 kMaxThumbSize);
}

void ThumbnailBar::applyOrientation()
{
    if (m_orientation == Qt::Horizontal) {
        setFlow(QListWidget::LeftToRight);
        setWrapping(false);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        setFlow(QListWidget::TopToBottom);
        setWrapping(false);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    updateCenteringMargins();
}

void ThumbnailBar::updateCenteringMargins()
{
    if (m_centeringGuard) {
        return;
    }
    m_centeringGuard = true;

    // Reset then measure natural layout
    setViewportMargins(0, 0, 0, 0);
    doItemsLayout();

    if (count() == 0) {
        m_centeringGuard = false;
        return;
    }

    QRect bounds;
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            bounds |= visualItemRect(it);
        }
    }

    int marginLeft = 0;
    int marginTop = 0;
    if (m_orientation == Qt::Horizontal) {
        const int avail = viewport()->width();
        if (bounds.width() > 0 && bounds.width() < avail) {
            marginLeft = (avail - bounds.width()) / 2 - bounds.left();
            marginLeft = qMax(0, marginLeft);
        }
    } else {
        const int avail = viewport()->height();
        if (bounds.height() > 0 && bounds.height() < avail) {
            marginTop = (avail - bounds.height()) / 2 - bounds.top();
            marginTop = qMax(0, marginTop);
        }
    }

    if (marginLeft > 0 || marginTop > 0) {
        setViewportMargins(marginLeft, marginTop, 0, 0);
    }

    m_centeringGuard = false;
}


void ThumbnailBar::setBarOrientation(Qt::Orientation orientation)
{
    if (m_orientation == orientation) {
        return;
    }
    m_orientation = orientation;
    applyOrientation();
    applyThumbMetrics();
}

void ThumbnailBar::applyThumbMetrics()
{
    // iconSize is only a floor for IconMode chrome. Letterbox cell width comes
    // exclusively from sizeHint / ThumbContentSizeRole — never inflate iconSize
    // or portrait cells pick up a wide minimum and look over-padded.
    setIconSize(QSize(m_thumbSize, m_thumbSize));
    if (m_delegate) {
        m_delegate->setThumbSize(m_thumbSize);
    }

    const QSize squareCell = m_delegate ? m_delegate->cellSize(font())
                                        : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight());
    // Crop mode: uniform squares. Letterbox: no gridSize — QListWidget IconMode
    // uses sizeHint per item (gridSize would force equal square slots and large
    // gaps between portrait pages).
    setUniformItemSizes(m_cropToSquare);
    if (m_cropToSquare) {
        setGridSize(squareCell);
    } else {
        setGridSize(QSize());
    }

    const int label = labelBandHeight();
    const int cross = m_delegate ? m_delegate->cellPad() : qBound(2, m_thumbSize / 24, 6);
    const int flow = m_delegate ? m_delegate->flowPad() : cross / 2;
    // Inter-image gap = 2·flowPad + spacing ≈ cellPad (matches cross-axis margin).
    setSpacing(cross - 2 * flow);
    if (m_orientation == Qt::Horizontal) {
        // Cross-axis extent = cross pads + thumbSize + label.
        setMinimumHeight(kMinThumbSize + label + 2 * cross);
        setMaximumHeight(kMaxThumbSize + label + 2 * cross);
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        setMinimumWidth(kMinThumbSize + 2 * cross);
        setMaximumWidth(kMaxThumbSize + 2 * cross);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }

    // Recompute logical content + sizeHint from current aspect at new thumbSize.
    refreshAllItemGeometry();
}

QSize ThumbnailBar::sizeHint() const
{
    const QSize cell = m_delegate ? m_delegate->cellSize(font())
                                  : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight());
    if (m_orientation == Qt::Horizontal) {
        return QSize(400, cell.height());
    }
    return QSize(cell.width(), 400);
}

QSize ThumbnailBar::minimumSizeHint() const
{
    if (m_orientation == Qt::Horizontal) {
        const int pad = m_delegate ? m_delegate->cellPad() : 4;
        return QSize(200, kMinThumbSize + labelBandHeight() + 2 * pad);
    }
    const int pad = m_delegate ? m_delegate->cellPad() : 4;
    return QSize(kMinThumbSize + 2 * pad, 200);
}

void ThumbnailBar::setThumbSize(int pixels)
{
    const int clamped = qBound(kMinThumbSize, pixels, kMaxThumbSize);
    if (clamped == m_thumbSize) {
        return;
    }
    // Dragging the dock/splitter fires resize every pixel. Keep scroll anchored
    // and only *geometry*-update immediately; debounced reload when sharper
    // pixels are needed (full invalidate on every step wiped in-flight thumbs).
    const ScrollAnchor anchor = captureScrollAnchor();
    m_thumbSize = clamped;
    applyThumbMetrics();
    refreshAllItemGeometry();
    restoreScrollAnchor(anchor);
    if (thumbDecodePixels() > m_decodedSize && !m_files.isEmpty()) {
        scheduleDebouncedThumbReload();
    }
}

ThumbnailBar::ScrollAnchor ThumbnailBar::captureScrollAnchor() const
{
    ScrollAnchor a;
    if (!viewport() || count() <= 0) {
        return a;
    }
    // Always the item under the *viewport centre* — resize must keep that
    // image centred, not selection or the strip start.
    const QPoint centre = viewport()->rect().center();
    QModelIndex idx = indexAt(centre);
    if (!idx.isValid()) {
        // Gaps between letterbox cells: probe a few offsets around centre.
        const QPoint probes[] = {
            centre,
            centre + QPoint(-8, 0),
            centre + QPoint(8, 0),
            centre + QPoint(0, -8),
            centre + QPoint(0, 8),
        };
        for (const QPoint &pt : probes) {
            idx = indexAt(pt);
            if (idx.isValid()) {
                break;
            }
        }
    }
    if (!idx.isValid()) {
        return a;
    }
    a.row = idx.row();
    a.offsetInViewport = 0; // unused — restore centres the row
    a.valid = true;
    return a;
}

void ThumbnailBar::restoreScrollAnchor(const ScrollAnchor &anchor)
{
    if (!anchor.valid || anchor.row < 0 || anchor.row >= count() || !item(anchor.row)) {
        return;
    }
    QScrollBar *bar = (m_orientation == Qt::Horizontal) ? horizontalScrollBar()
                                                          : verticalScrollBar();
    if (!bar || !viewport()) {
        return;
    }
    // Centre the anchored row in the viewport (flow axis).
    scrollToItem(item(anchor.row), QAbstractItemView::PositionAtCenter);
    // PositionAtCenter can be approximate with variable letterbox sizeHints —
    // fine-tune so the item's centre matches the viewport centre.
    const QRect vr = visualItemRect(item(anchor.row));
    if (!vr.isValid()) {
        return;
    }
    const QRect vp = viewport()->rect();
    if (m_orientation == Qt::Horizontal) {
        const int itemMid = vr.left() + vr.width() / 2;
        const int viewMid = vp.width() / 2;
        const int delta = itemMid - viewMid;
        if (delta != 0) {
            bar->setValue(bar->value() + delta);
        }
    } else {
        const int itemMid = vr.top() + vr.height() / 2;
        const int viewMid = vp.height() / 2;
        const int delta = itemMid - viewMid;
        if (delta != 0) {
            bar->setValue(bar->value() + delta);
        }
    }
}

void ThumbnailBar::scheduleDebouncedThumbReload()
{
    if (!m_thumbSizeReloadTimer) {
        m_thumbSizeReloadTimer = new QTimer(this);
        m_thumbSizeReloadTimer->setSingleShot(true);
        connect(m_thumbSizeReloadTimer, &QTimer::timeout, this, [this]() {
            if (m_files.isEmpty()) {
                return;
            }
            if (thumbDecodePixels() <= m_decodedSize) {
                return;
            }
            scheduleThumbnailLoads();
        });
    }
    // ~1 frame of continuous drag still coalesces; long enough to ride out
    // splitter move bursts without feeling laggy after release.
    m_thumbSizeReloadTimer->start(120);
}

void ThumbnailBar::setLabelsVisible(bool on)
{
    if (m_labelsVisible == on) {
        return;
    }
    m_labelsVisible = on;
    if (m_delegate) {
        m_delegate->setLabelsVisible(on);
    }
    applyThumbMetrics();
    updateGeometry();
}

int ThumbnailBar::pendingLoadCount() const
{
    return m_thumbLoadScheduled.size() + m_thumbAwaitLadder.size();
}

bool ThumbnailBar::isRowLoading(int row) const
{
    return m_thumbLoadScheduled.contains(row) || m_thumbAwaitLadder.contains(row);
}

void ThumbnailBar::cancelPendingLoads()
{
    m_thumbLoadScheduled.clear();
    m_thumbAwaitLadder.clear();
    m_thumbFailed.clear();
    ++m_generation;
    emit loadsChanged();
}

void ThumbnailBar::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    updateCenteringMargins();
    const int extent = (m_orientation == Qt::Horizontal) ? height() : width();
    const int newSize = thumbSizeFromBarExtent(extent);
    if (newSize != m_thumbSize) {
        setThumbSize(newSize);
    }
}

int ThumbnailBar::thumbDecodePixels() const
{
    // Decode at device pixels so HiDPI does not show a half-size centered icon.
    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    return qMax(1, qRound(m_thumbSize * dpr));
}

int ThumbnailBar::filmstripDecodeEdge() const
{
    // Sharpness only — never a layout size. Logical cells use thumbSize.
    // Power-of-two ladder step for thumbSize×DPR — no soft-max clamp. Soft
    // ladder is a placeholder until this edge is installed (ThumbDecodeEdgeRole).
    return ThumtooCache::ceilLadderEdge(thumbDecodePixels());
}


void ThumbnailBar::changeEvent(QEvent *event)
{
    QListWidget::changeEvent(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event && event->type() == QEvent::DevicePixelRatioChange) {
        if (!m_files.isEmpty()) {
            scheduleThumbnailLoads();
        }
    }
#else
    Q_UNUSED(event);
#endif
}

void ThumbnailBar::setThumbnailIcon(int row, const QImage &image)
{
    if (row < 0 || row >= count() || image.isNull()) {
        return;
    }
    QListWidgetItem *it = item(row);
    if (!it || !m_delegate) {
        return;
    }
    // Decode-edge pixmap for sharpness; layout uses aspect only.
    const QPixmap pm = QPixmap::fromImage(image);
    it->setData(ThumbnailDelegate::ThumbPixmapRole, pm);
    it->setIcon(QIcon(pm));

    // Logical content at thumbSize (crop = square; letterbox = cross-axis fit).
    // image.size() is aspect only — never use decode pixels as layout size.
    const QSize content = m_cropToSquare
        ? QSize(m_thumbSize, m_thumbSize)
        : m_delegate->letterboxContentSize(image.size());
    it->setData(ThumbnailDelegate::ThumbContentSizeRole, content);
    it->setData(ThumbnailDelegate::ThumbLoadedRole, true);
    it->setData(ThumbnailDelegate::ThumbDecodeEdgeRole,
                qMax(image.width(), image.height()));

    const QSize hint = m_cropToSquare
        ? m_delegate->cellSize(font())
        : m_delegate->cellSizeForContent(font(), content);
    it->setSizeHint(hint);

    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
        qWarning().noquote()
            << QStringLiteral("[filmstrip] icon row=%1 img=%2x%3 content=%4x%5 hint=%6x%7 crop=%8")
                   .arg(row)
                   .arg(image.width()).arg(image.height())
                   .arg(content.width()).arg(content.height())
                   .arg(hint.width()).arg(hint.height())
                   .arg(m_cropToSquare);
    }

    const QModelIndex idx = indexFromItem(it);
    if (idx.isValid()) {
        dataChanged(idx, idx, {Qt::DecorationRole, Qt::SizeHintRole,
                               ThumbnailDelegate::ThumbContentSizeRole,
                               ThumbnailDelegate::ThumbLoadedRole,
                               ThumbnailDelegate::ThumbPixmapRole});
    }
    // Layout immediately so the next paint gets option.rect matching sizeHint.
    // Debounce only centering margins (cheaper secondary pass).
    doItemsLayout();
    scheduleLayoutRefresh();
    if (viewport()) {
        viewport()->update(visualItemRect(it));
    }
}

void ThumbnailBar::scheduleLayoutRefresh()
{
    if (!m_layoutRefreshTimer) {
        m_layoutRefreshTimer = new QTimer(this);
        m_layoutRefreshTimer->setSingleShot(true);
        m_layoutRefreshTimer->setInterval(32);
        connect(m_layoutRefreshTimer, &QTimer::timeout, this, [this]() {
            doItemsLayout();
            updateCenteringMargins();
            if (viewport()) {
                viewport()->update();
            }
        });
    }
    m_layoutRefreshTimer->start();
}

QImage ThumbnailBar::prepareThumbnailFromImage(const QImage &image, int maxSize) const
{
    if (image.isNull() || maxSize < 1) {
        return QImage();
    }
    if (m_cropToSquare) {
        // Center-crop to square, then scale to maxSize². Aspect stays 1:1.
        const int side = qMin(image.width(), image.height());
        if (side <= 0) {
            return QImage();
        }
        const int x = (image.width() - side) / 2;
        const int y = (image.height() - side) / 2;
        QImage square = image.copy(x, y, side, side);
        return square.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Letterbox: longest edge → maxSize, aspect preserved. Layout uses
    // letterboxContentSize(aspect) at thumbSize — not these pixel dimensions.
    return image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void ThumbnailBar::setCropToSquare(bool on)
{
    if (m_cropToSquare == on) {
        return;
    }
    m_cropToSquare = on;
    applyThumbMetrics();
    // scheduleThumbnailLoads → invalidateThumbPixels bumps generation + clears.
    scheduleThumbnailLoads();
}

void ThumbnailBar::setStripBackground(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    // Filmstrip body = canvas colour; scrollbars = application window chrome
    // (not the canvas colour).
    const QColor chrome = QApplication::palette().color(QPalette::Window);
    const QColor mid = QApplication::palette().color(QPalette::Mid);
    const QColor text = (color.lightness() < 128) ? QColor(Qt::white) : QColor(Qt::black);

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, color);
    pal.setColor(QPalette::Window, color);
    pal.setColor(QPalette::Text, text);
    setPalette(pal);
    if (viewport()) {
        viewport()->setAutoFillBackground(true);
        QPalette vp = viewport()->palette();
        vp.setColor(QPalette::Base, color);
        vp.setColor(QPalette::Window, color);
        viewport()->setPalette(vp);
    }

    setStyleSheet(QStringLiteral(
        "ThumbnailBar { background-color: %1; border: none; }"
        "ThumbnailBar::item { background: transparent; }"
        "QScrollBar:horizontal {"
        "  background: %2; height: 12px; margin: 0;"
        "}"
        "QScrollBar:vertical {"
        "  background: %2; width: 12px; margin: 0;"
        "}"
        "QScrollBar::handle:horizontal, QScrollBar::handle:vertical {"
        "  background: %3; border-radius: 3px;"
        "  min-width: 20px; min-height: 20px;"
        "}"
        "QScrollBar::add-line, QScrollBar::sub-line {"
        "  width: 0; height: 0; background: none;"
        "}"
        "QScrollBar::add-page, QScrollBar::sub-page {"
        "  background: %2;"
        "}"
    ).arg(color.name(QColor::HexRgb),
          chrome.name(QColor::HexRgb),
          mid.name(QColor::HexRgb)));

    if (viewport()) {
        viewport()->update();
    }
}

QImage ThumbnailBar::makeThumbnail(const QString &path, int maxSize) const
{
    // Prefer a ready cache hit already large enough for the cell.
    // On miss, decode synchronously here: filmstrip jobs already run on the
    // thread pool. ImageCache::ensure() / loadThumbnailCached() only *schedule*
    // a decode and often return null; combined with m_thumbLoadScheduled that
    // left blank cells that never retried after the cache filled.
    QImage image = ImageCache::get(path, maxSize);
    if (image.isNull()) {
        image = ImageLoader::loadThumbnail(path, maxSize);
        if (!image.isNull()) {
            ImageCache::put(path, image);
        } else {
            // Any smaller mid-flight frame is better than an empty cell.
            image = ImageCache::get(path);
        }
    }
    if (image.isNull()) {
        return {};
    }
    // Durable content appearance (XDG state) — filmstrip owns its own bake.
    // Do not rely on Gallery soft install emitting overrides (that coupled
    // selection to the strip). Session-id overrides still win in the scheduler.
    ThumtooCache::StoredContentAppearance stored;
    if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
        WorkspaceItemState st;
        st.contentHFlip = stored.contentHFlip;
        st.contentVFlip = stored.contentVFlip;
        st.contentQuarterTurns = stored.contentQuarterTurns;
        st.hasCrop = stored.hasCrop;
        st.cropRect = stored.cropRect;
        st.cropSourceSize = stored.cropSourceSize;
        st.cropRotation = stored.cropRotation;
        if (stored.hasGrade) {
            st.colorAdjust.brightness = stored.gradeBrightness;
            st.colorAdjust.contrast =
                stored.gradeContrast == 0 ? 100 : stored.gradeContrast;
            st.colorAdjust.saturation =
                stored.gradeSaturation == 0 ? 100 : stored.gradeSaturation;
            st.colorAdjust.hue = stored.gradeHue;
            st.colorAdjust.gamma = stored.gradeGamma <= 0
                ? 1.0
                : (stored.gradeGamma / 100.0);
            st.colorAdjust.invert = stored.gradeInvert;
        }
        image = SessionAppearance::applyContentToImage(
            image, st, SessionAppearance::PixelKind::SoftPreview);
    }
    return prepareThumbnailFromImage(image, maxSize);
}

void ThumbnailBar::setSessionImageOverride(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    // With a session-id list, path-wide paint is forbidden — it crops every
    // filmstrip row that shares the file path (IDENTITY.md).
    bool anyBound = false;
    for (SessionImageId id : m_sessionIds) {
        if (id != kInvalidSessionImageId) {
            anyBound = true;
            break;
        }
    }
    if (anyBound) {
        qCritical("ThumbnailBar: ignoring path-only override for %s (session has ids — use id overload)",
                  qPrintable(path));
        return;
    }
    m_sessionImageOverrides.insert(path, image);
    const QImage thumb = prepareThumbnailFromImage(image, filmstripDecodeEdge());
    if (thumb.isNull()) {
        return;
    }
    for (int row = 0; row < m_files.size(); ++row) {
        if (m_files.at(row) == path) {
            setThumbnailIcon(row, thumb);
        }
    }
}

void ThumbnailBar::setSessionIds(const QVector<SessionImageId> &ids)
{
    m_sessionIds = ids;
    {
        QSet<SessionImageId> seen;
        for (int i = 0; i < m_sessionIds.size(); ++i) {
            const SessionImageId id = m_sessionIds.at(i);
            if (id == kInvalidSessionImageId) {
                continue;
            }
            if (seen.contains(id)) {
                qCritical("ThumbnailBar::setSessionIds: duplicate SessionImageId %lld at row %d",
                          static_cast<long long>(id), i);
            } else {
                seen.insert(id);
            }
        }
    }
    // Stamp identity on each row so drag/mime never depends on array index alone.
    for (int row = 0; row < count(); ++row) {
        if (QListWidgetItem *it = item(row)) {
            const SessionImageId sid = (row < m_sessionIds.size()) ? m_sessionIds.at(row)
                                                                  : kInvalidSessionImageId;
            it->setData(RoleSessionId, QVariant::fromValue(static_cast<qint64>(sid)));
        }
    }
    // Drop overrides for session images that are no longer in the strip.
    if (!m_sessionIdImageOverrides.isEmpty()) {
        QHash<SessionImageId, QImage> kept;
        for (SessionImageId id : ids) {
            auto it = m_sessionIdImageOverrides.constFind(id);
            if (it != m_sessionIdImageOverrides.cend()) {
                kept.insert(id, it.value());
            }
        }
        m_sessionIdImageOverrides.swap(kept);
    }
    // Re-apply icons now that row ↔ id alignment is known.
    for (int row = 0; row < m_sessionIds.size() && row < m_files.size(); ++row) {
        const SessionImageId sid = m_sessionIds.at(row);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        auto it = m_sessionIdImageOverrides.constFind(sid);
        if (it == m_sessionIdImageOverrides.cend()) {
            continue;
        }
        const QImage thumb = prepareThumbnailFromImage(it.value(), filmstripDecodeEdge());
        if (!thumb.isNull()) {
            setThumbnailIcon(row, thumb);
        }
    }
}

void ThumbnailBar::setSessionImageOverride(SessionImageId sessionId, const QString &path,
                                           const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    if (sessionId == kInvalidSessionImageId) {
        // Unbound tiles only. Path-wide update is last resort (IDENTITY.md).
        setSessionImageOverride(path, image);
        return;
    }
    m_sessionIdImageOverrides.insert(sessionId, image);
    const QImage thumb = prepareThumbnailFromImage(image, filmstripDecodeEdge());
    if (thumb.isNull()) {
        return;
    }
    for (int row = 0; row < m_sessionIds.size() && row < m_files.size(); ++row) {
        if (m_sessionIds.at(row) == sessionId) {
            setThumbnailIcon(row, thumb);
            return;
        }
    }
    // Id not in the strip yet — keep override for when the row appears.
    // Do not paint every path-matching row (leaks crops across duplicates).
    Q_UNUSED(path);
}

void ThumbnailBar::setOnCanvasIndices(const QSet<int> &indices)
{
    if (m_onCanvasIndices == indices) {
        return;
    }
    m_onCanvasIndices = indices;
    viewport()->update();
}


void ThumbnailBar::showEvent(QShowEvent *event)
{
    QListWidget::showEvent(event);
    scheduleVisibleThumbnailLoads();
}

void ThumbnailBar::invalidateThumbPixels()
{
    // Drop prepared pixels so scheduleVisible reloads. Rows/paths stay.
    // Bump generation so in-flight pool jobs cannot reinstall after a mode/size
    // change that already cleared the strip.
    ++m_generation;
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setIcon(QIcon());
            it->setData(ThumbnailDelegate::ThumbPixmapRole, QVariant());
            it->setData(ThumbnailDelegate::ThumbLoadedRole, false);
            it->setData(ThumbnailDelegate::ThumbDecodeEdgeRole, 0);
            if (m_delegate) {
                if (m_cropToSquare) {
                    it->setData(ThumbnailDelegate::ThumbContentSizeRole,
                                QSize(m_thumbSize, m_thumbSize));
                    it->setSizeHint(m_delegate->cellSize(font()));
                } else {
                    const QSize prov = m_delegate->provisionalContentSize();
                    it->setData(ThumbnailDelegate::ThumbContentSizeRole,
                                m_delegate->letterboxContentSize(prov));
                    it->setSizeHint(m_delegate->cellSizeForContent(font(), prov));
                }
            }
        }
    }
    m_thumbLoadScheduled.clear();
    m_thumbAwaitLadder.clear();
    m_thumbFailed.clear();
}

void ThumbnailBar::refreshAllItemGeometry()
{
    if (!m_delegate) {
        return;
    }
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (!it) {
            continue;
        }
        if (m_cropToSquare) {
            it->setData(ThumbnailDelegate::ThumbContentSizeRole,
                        QSize(m_thumbSize, m_thumbSize));
            it->setSizeHint(m_delegate->cellSize(font()));
            continue;
        }
        // Prefer pixmap aspect when loaded — role may be provisional square
        // from before decode or after thumbSize-only refresh of empty cells.
        QSize aspect;
        const QPixmap pm = qvariant_cast<QPixmap>(
            it->data(ThumbnailDelegate::ThumbPixmapRole));
        if (!pm.isNull() && pm.width() > 0 && pm.height() > 0) {
            aspect = pm.size();
        } else {
            aspect = it->data(ThumbnailDelegate::ThumbContentSizeRole).toSize();
            if (aspect.width() < 1 || aspect.height() < 1) {
                aspect = m_delegate->provisionalContentSize();
            }
        }
        const QSize content = m_delegate->letterboxContentSize(aspect);
        it->setData(ThumbnailDelegate::ThumbContentSizeRole, content);
        it->setSizeHint(m_delegate->cellSizeForContent(font(), content));
    }
    doItemsLayout();
    updateCenteringMargins();
    if (viewport()) {
        viewport()->update();
    }
}

void ThumbnailBar::scheduleThumbnailLoads()
{
    invalidateThumbPixels();
    scheduleVisibleThumbnailLoads();
    emit loadsChanged();
}

void ThumbnailBar::scheduleVisibleThumbnailLoads()
{
    if (m_files.isEmpty()) {
        return;
    }
    const quint64 gen = m_generation.load();
    // Decode at (or just above) visual demand; soft ladder max 512.
    const int decodeSize = filmstripDecodeEdge();
    m_decodedSize = decodeSize;

    const int n = m_files.size();
    // Visible range from the viewport (scroll position), NOT currentRow.
    // Selection-centred scheduling never loaded thumbs the user scrolled to
    // without clicking (currentRow stayed put).
    int lo = 0;
    int hi = n;
    if (viewport() && n > 0) {
        const QRect vr = viewport()->rect();
        const int pad = qMax(m_thumbSize, 32);
        const QRect expanded = vr.adjusted(-pad, -pad, pad, pad);
        int minRow = n;
        int maxRow = -1;
        const QPoint samples[] = {
            expanded.topLeft(),
            expanded.topRight(),
            expanded.bottomLeft(),
            expanded.bottomRight(),
            expanded.center(),
            QPoint(expanded.center().x(), expanded.top()),
            QPoint(expanded.center().x(), expanded.bottom()),
            QPoint(expanded.left(), expanded.center().y()),
            QPoint(expanded.right(), expanded.center().y()),
        };
        for (const QPoint &pt : samples) {
            const QModelIndex idx = indexAt(pt);
            if (!idx.isValid()) {
                continue;
            }
            minRow = qMin(minRow, idx.row());
            maxRow = qMax(maxRow, idx.row());
        }
        // Fallback: scan items whose visual rect intersects the viewport.
        if (maxRow < 0) {
            for (int i = 0; i < n; ++i) {
                QListWidgetItem *it = item(i);
                if (!it) {
                    continue;
                }
                if (visualItemRect(it).intersects(expanded)) {
                    minRow = qMin(minRow, i);
                    maxRow = qMax(maxRow, i);
                }
            }
        }
        if (maxRow >= 0) {
            // Extra overscan in index space (~half a screen of cells).
            // Letterbox cells are often wider than thumbSize; under-counting
            // across left off-screen neighbours unloaded until scroll.
            const int cell = qMax(1, m_thumbSize + 8);
            const int across = qMax(1, viewport()->width() / cell);
            const int down = qMax(1, viewport()->height() / cell);
            const int over = qMax(16, across * down * 2);
            lo = qMax(0, minRow - over);
            hi = qMin(n, maxRow + over + 1);
        } else {
            // No geometry yet — seed from selection or start.
            int focus = currentRow();
            if (focus < 0) {
                focus = 0;
            }
            lo = qMax(0, focus - 24);
            hi = qMin(n, focus + 25);
        }
    }

    // Pool jobs only — thumtoo pixel concurrency is separate (kMaxConcurrentPixelJobs).
    // Default 24: 12 left gaps when soft misses parked many rows in AwaitLadder
    // and only a narrow visible band was filled before the concurrent cap.
    static const int kMaxConcurrentThumbLoads = []() {
        int v = 24;
        if (const char *e = std::getenv("BILTOO_FILMSTRIP_THUMB_LOADS")) {
            const int parsed = QString::fromLocal8Bit(e).toInt();
            if (parsed >= 1 && parsed <= 64) {
                v = parsed;
            }
        }
        return v;
    }();
    int inFlight = 0;
    for (int idx : m_thumbLoadScheduled) {
        Q_UNUSED(idx);
        ++inFlight;
    }
    for (int i = lo; i < hi; ++i) {
        if (m_thumbLoadScheduled.contains(i) || m_thumbAwaitLadder.contains(i)
            || m_thumbFailed.contains(i)) {
            continue;
        }
        // Soft placeholder is fine until long edge meets the display ladder step.
        if (QListWidgetItem *it = item(i)) {
            const int haveEdge =
                it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
            if (haveEdge >= decodeSize * 9 / 10) {
                continue;
            }
        }
        if (inFlight >= kMaxConcurrentThumbLoads) {
            break;
        }
        const QString path = m_files.at(i);
        // Prefer per-session-image override (stable id). Path-level override is
        // legacy only for unbound rows — never paint a path crop onto a bound
        // duplicate (drag-drop / Duplicate produced the wrong thumbnail).
        SessionImageId sid = kInvalidSessionImageId;
        if (i < m_sessionIds.size()) {
            sid = m_sessionIds.at(i);
            if (sid != kInvalidSessionImageId
                && m_sessionIdImageOverrides.contains(sid)) {
                const QImage thumb = prepareThumbnailFromImage(
                    m_sessionIdImageOverrides.value(sid), decodeSize);
                if (!thumb.isNull()) {
                    setThumbnailIcon(i, thumb);
                }
                continue;
            }
        }
        if (sid == kInvalidSessionImageId && m_sessionImageOverrides.contains(path)) {
            const QImage thumb = prepareThumbnailFromImage(m_sessionImageOverrides.value(path),
                                                          decodeSize);
            if (!thumb.isNull()) {
                setThumbnailIcon(i, thumb);
            }
            continue;
        }
        m_thumbLoadScheduled.insert(i);
        ++inFlight;
        const QPointer<ThumbnailBar> guard(this);
        QThreadPool::globalInstance()->start([guard, i, path, gen, decodeSize]() {
            ThumbnailBar *bar = guard.data();
            if (!bar || gen != bar->m_generation.load()) {
                return;
            }
            const char *dbg = std::getenv("THUMTOO_DEBUG");
            if (!dbg || dbg[0] == '\0' || dbg[0] == '0') {
                dbg = std::getenv("BILTOO_THUMTOO_DEBUG");
            }
            if (dbg && dbg[0] != '\0' && dbg[0] != '0') {
                fprintf(stderr, "biltoo/filmstrip: makeThumbnail row=%d edge=%d path=%s\n",
                        i, decodeSize, qPrintable(path));
            }
            const QImage image = bar->makeThumbnail(path, decodeSize);
            bar = guard.data();
            if (!bar || gen != bar->m_generation.load()) {
                return;
            }
            if (image.isNull()) {
                // Soft miss (e.g. archive ladder pending): free the slot and wait
                // for ladderReady instead of leaving the row permanently scheduled.
                QMetaObject::invokeMethod(bar, [guard, i, gen]() {
                    ThumbnailBar *const host = guard.data();
                    if (!host || gen != host->m_generation.load()) {
                        return;
                    }
                    host->m_thumbLoadScheduled.remove(i);
                    if (ThumtooCache::isAvailable()) {
                        host->m_thumbAwaitLadder.insert(i);
                    } else {
                        host->m_thumbFailed.insert(i);
                    }
                    host->viewport()->update();
                    emit host->loadsChanged();
                }, Qt::QueuedConnection);
                return;
            }
            // A crop may have landed while this job ran — do not clobber it.
            // Path override only protects unbound rows; bound rows use id map.
            if (i < bar->m_sessionIds.size()) {
                const SessionImageId rowId = bar->m_sessionIds.at(i);
                if (rowId != kInvalidSessionImageId) {
                    if (bar->m_sessionIdImageOverrides.contains(rowId)) {
                        return;
                    }
                } else if (bar->m_sessionImageOverrides.contains(path)) {
                    return;
                }
            } else if (bar->m_sessionImageOverrides.contains(path)) {
                return;
            }
            // Re-check generation + path on the GUI thread: cancelPendingLoads /
            // setFiles may have rebuilt the list between pool completion and
            // this queued call (History session switch showed old thumbs).
            QMetaObject::invokeMethod(bar, [guard, i, path, gen, image]() {
                ThumbnailBar *const host = guard.data();
                if (!host || gen != host->m_generation.load()) {
                    return;
                }
                if (i < 0 || i >= host->m_files.size() || host->m_files.at(i) != path) {
                    return;
                }
                host->m_thumbLoadScheduled.remove(i);
                host->m_thumbFailed.remove(i);
                host->setThumbnailIcon(i, image);
                emit host->loadsChanged();
                // Free slot may allow more visible rows to start.
                host->scheduleVisibleThumbnailLoads();
            }, Qt::QueuedConnection);
        });
    }
    emit loadsChanged();
}

void ThumbnailBar::clearPressState()
{
    m_pressActive = false;
    m_pressItem = nullptr;
    m_dragStarted = false;
}


void ThumbnailBar::applyNativeAspect(QListWidgetItem *item, const QSize &native)
{
    if (!item || !m_delegate || m_cropToSquare) {
        return;
    }
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        return;
    }
    const QSize content = m_delegate->letterboxContentSize(native);
    item->setData(ThumbnailDelegate::ThumbContentSizeRole, content);
    item->setSizeHint(m_delegate->cellSizeForContent(font(), content));
}

void ThumbnailBar::primeGeometryFromCache()
{
    if (!m_delegate || m_cropToSquare || m_files.isEmpty()) {
        return;
    }
    bool any = false;
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (!it || i >= m_files.size()) {
            continue;
        }
        // Already have a real thumb — setThumbnailIcon owns aspect.
        if (it->data(ThumbnailDelegate::ThumbLoadedRole).toBool()) {
            continue;
        }
        const QString &path = m_files.at(i);
        if (path.isEmpty()) {
            continue;
        }
        if (const QSize cached = ThumtooCache::cachedSize(path);
            cached.isValid() && cached.width() > 0 && cached.height() > 0) {
            applyNativeAspect(it, cached);
            any = true;
        } else if (ThumtooCache::isAvailable() && !ThumtooCache::isUnsupported(path)) {
            ThumtooCache::scheduleProbe(path);
        }
    }
    if (any) {
        doItemsLayout();
        updateCenteringMargins();
    }
}

void ThumbnailBar::setSession(const QStringList &files, const QVector<SessionImageId> &ids)
{
    // Install ids first so scheduleThumbnailLoads (from setFiles) sees the
    // correct row ↔ SessionImageId alignment. Callers that did setFiles then
    // setSessionIds painted path overrides / stale id overrides onto the wrong
    // cells after drag-drop and Duplicate.
    m_sessionIds = ids;
    if (!m_sessionIdImageOverrides.isEmpty()) {
        QHash<SessionImageId, QImage> kept;
        for (SessionImageId id : ids) {
            auto it = m_sessionIdImageOverrides.constFind(id);
            if (it != m_sessionIdImageOverrides.cend()) {
                kept.insert(id, it.value());
            }
        }
        m_sessionIdImageOverrides.swap(kept);
    }
    setFiles(files);
}

void ThumbnailBar::setFiles(const QStringList &files)
{
    // Keep per-session-id appearance overrides across list rebuilds. setSessionIds
    // prunes ids that left the session; clearing here made Duplicate show the
    // raw on-disk file for cropped/rotated slots.
    cancelPendingLoads();
    clearPressState();
    clear();
    m_files = files;
    // Drop overrides for paths no longer in the session.
    {
        QHash<QString, QImage> kept;
        for (const QString &path : m_files) {
            auto it = m_sessionImageOverrides.constFind(path);
            if (it != m_sessionImageOverrides.cend()) {
                kept.insert(path, it.value());
            }
        }
        m_sessionImageOverrides.swap(kept);
    }

    // Provisional square only when size is unknown. Prefer durable cache size
    // (same path as Gallery::primeGalleryGeometryFromCache) so PDF pages layout
    // with real aspect before ladder pixels arrive.
    const QSize provCell = (m_delegate && !m_cropToSquare)
        ? m_delegate->cellSizeForContent(font(), m_delegate->provisionalContentSize())
        : (m_delegate ? m_delegate->cellSize(font())
                      : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight()));
    // Bulk insert: avoid per-item repaints (large archives can have thousands of rows).
    setUpdatesEnabled(false);
    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files.at(i);
        auto *item = new QListWidgetItem(this);
        item->setText(PagePath::displayName(path));
        item->setToolTip(path);
        item->setData(RolePath, path);
        const SessionImageId sid = (i < m_sessionIds.size()) ? m_sessionIds.at(i)
                                                            : kInvalidSessionImageId;
        item->setData(RoleSessionId, QVariant::fromValue(static_cast<qint64>(sid)));
        // No theme placeholder — empty icon shows loading chrome and allows
        // scheduleVisibleThumbnailLoads to pick the row up (non-null icons were
        // treated as already loaded).
        item->setData(ThumbnailDelegate::ThumbLoadedRole, false);
        QSize native;
        if (!m_cropToSquare && !path.isEmpty()) {
            native = ThumtooCache::cachedSize(path);
        }
        if (m_delegate && !m_cropToSquare && native.isValid()
            && native.width() > 0 && native.height() > 0) {
            const QSize content = m_delegate->letterboxContentSize(native);
            item->setData(ThumbnailDelegate::ThumbContentSizeRole, content);
            item->setSizeHint(m_delegate->cellSizeForContent(font(), content));
        } else if (m_delegate && !m_cropToSquare) {
            const QSize prov = m_delegate->letterboxContentSize(
                m_delegate->provisionalContentSize());
            item->setData(ThumbnailDelegate::ThumbContentSizeRole, prov);
            item->setSizeHint(provCell);
            if (!path.isEmpty() && ThumtooCache::isAvailable()
                && !ThumtooCache::isUnsupported(path)) {
                ThumtooCache::scheduleProbe(path);
            }
        } else if (m_delegate) {
            item->setData(ThumbnailDelegate::ThumbContentSizeRole,
                          QSize(m_thumbSize, m_thumbSize));
            item->setSizeHint(provCell);
        } else {
            item->setSizeHint(provCell);
        }
    }
    setUpdatesEnabled(true);

    if (m_multiSelect) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }

    if (count() > 0 && !m_multiSelect) {
        setCurrentRow(0);
    }

    scheduleThumbnailLoads();
    updateCenteringMargins();
    // Layout/visibility may not be final during setFiles — kick again next tick.
    QTimer::singleShot(0, this, [this]() {
        // Re-read cache in case prepare finished between loop and now.
        primeGeometryFromCache();
        scheduleVisibleThumbnailLoads();
    });
    QTimer::singleShot(100, this, [this]() {
        scheduleVisibleThumbnailLoads();
    });
}

void ThumbnailBar::setCurrentIndex(int index)
{
    if (index >= 0 && index < count()) {
        setCurrentRow(index);
        scrollToItem(item(index), QAbstractItemView::EnsureVisible);
    }
}

int ThumbnailBar::currentIndex() const
{
    return currentRow();
}

void ThumbnailBar::setMultiSelectEnabled(bool on)
{
    if (m_multiSelect == on) {
        return;
    }
    clearPressState();
    m_multiSelect = on;

    const bool blocked = blockSignals(true);
    if (on) {
        // MultiSelection flag only; selection is driven by mouse handlers
        // (no rubber-band). Click toggles, Shift+click ranges, Ctrl+click toggles.
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
        clearSelection();
        m_selectionAnchor = -1;
    } else {
        setSelectionMode(QAbstractItemView::SingleSelection);
        clearSelection();
    }
    blockSignals(blocked);
}

QList<int> ThumbnailBar::selectedIndices() const
{
    QList<int> rows;
    const QList<QListWidgetItem *> items = selectedItems();
    rows.reserve(items.size());
    for (QListWidgetItem *it : items) {
        rows.append(row(it));
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void ThumbnailBar::setSelectedIndices(const QList<int> &indices)
{
    const bool blocked = blockSignals(true);
    clearSelection();
    m_selectionAnchor = indices.isEmpty() ? -1 : indices.last();
    for (int idx : indices) {
        if (idx >= 0 && idx < count()) {
            item(idx)->setSelected(true);
        }
    }
    blockSignals(blocked);
}




void ThumbnailBar::onItemActivated(QListWidgetItem *item)
{
    if (m_multiSelect) {
        return;
    }
    if (item) {
        emit indexActivated(row(item));
    }
}

void ThumbnailBar::onCurrentRowChanged(int row)
{
    scheduleVisibleThumbnailLoads();
    if (m_multiSelect) {
        return;
    }
    if (row >= 0) {
        emit indexActivated(row);
    }
}

void ThumbnailBar::requestRemoveSelection()
{
    QList<int> indices = selectedIndices();
    if (indices.isEmpty()) {
        const int r = currentRow();
        if (r >= 0) {
            indices.append(r);
        }
    }
    if (!indices.isEmpty()) {
        emit removeIndicesRequested(indices);
    }
}

void ThumbnailBar::selectAllThumbs()
{
    if (count() == 0) {
        return;
    }
    // Multi-select is native in workspace mode; Image mode stays single-select
    // for navigation — Select All still useful before bulk remove. Prefer
    // ExtendedSelection so Qt keeps a current item without leaving the bar
    // stuck in permanent MultiSelection.
    const bool wasSingle = (!m_multiSelect
                            && selectionMode() == QAbstractItemView::SingleSelection);
    if (wasSingle) {
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setSelectionRectVisible(false);
    }
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSelected(true);
        }
    }
    m_selectionAnchor = 0;
    if (m_multiSelect) {
        emit workspaceSelectionChanged();
    }
}

void ThumbnailBar::selectNoneThumbs()
{
    clearSelection();
    m_selectionAnchor = -1;
    if (!m_multiSelect) {
        setSelectionMode(QAbstractItemView::SingleSelection);
    }
    if (m_multiSelect) {
        emit workspaceSelectionChanged();
    }
}

void ThumbnailBar::invertThumbSelection()
{
    if (count() == 0) {
        return;
    }
    const bool wasSingle = (!m_multiSelect
                            && selectionMode() == QAbstractItemView::SingleSelection);
    if (wasSingle) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSelected(!it->isSelected());
        }
    }
    if (m_multiSelect) {
        emit workspaceSelectionChanged();
    }
}

void ThumbnailBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        requestRemoveSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAllThumbs();
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

void ThumbnailBar::contextMenuEvent(QContextMenuEvent *event)
{
    QListWidgetItem *hit = itemAt(event->pos());
    // Right-click on an already-selected thumb must keep the multi-selection
    // (including ExtendedSelection used for Ctrl/Shift in Image/Gallery).
    // Only change selection when the hit item is outside the current selection.
    if (hit && !hit->isSelected()) {
        if (m_multiSelect
            || selectionMode() != QAbstractItemView::SingleSelection) {
            // Outside the selection: exclusive select the hit (standard).
            clearSelection();
            hit->setSelected(true);
            setCurrentItem(hit);
        } else {
            setCurrentItem(hit);
        }
    }

    QList<int> indices = selectedIndices();
    if (indices.isEmpty() && hit) {
        indices.append(row(hit));
    }

    const int total = count();
    const int selectedCount = indices.size();

    QMenu menu(this);

    QAction *selectAllAct = menu.addAction(tr("Select &All"));
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    selectAllAct->setEnabled(total > 0 && selectedCount < total);

    QAction *selectNoneAct = menu.addAction(tr("Select &None"));
    selectNoneAct->setEnabled(selectedCount > 0);

    QAction *invertAct = menu.addAction(tr("&Invert Selection"));
    invertAct->setEnabled(total > 0);

    menu.addSeparator();

    QAction *removeAct = menu.addAction(tr("&Remove from Session"));
    removeAct->setShortcut(QKeySequence::Delete);
    removeAct->setEnabled(selectedCount > 0);

    QAction *removeOthersAct = menu.addAction(tr("Remove &Others from Session"));
    removeOthersAct->setEnabled(selectedCount > 0 && selectedCount < total);

    QAction *removeAllAct = menu.addAction(tr("Remove A&ll from Session"));
    removeAllAct->setEnabled(total > 0);

    menu.addSeparator();

    QAction *copyPathsAct = menu.addAction(tr("&Copy Path(s)"));
    copyPathsAct->setEnabled(selectedCount > 0 || hit != nullptr);

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) {
        event->accept();
        return;
    }

    if (chosen == selectAllAct) {
        selectAllThumbs();
    } else if (chosen == selectNoneAct) {
        selectNoneThumbs();
    } else if (chosen == invertAct) {
        invertThumbSelection();
    } else if (chosen == removeAct && selectedCount > 0) {
        emit removeIndicesRequested(indices);
    } else if (chosen == removeOthersAct && selectedCount > 0) {
        QList<int> others;
        for (int i = 0; i < total; ++i) {
            if (!indices.contains(i)) {
                others.append(i);
            }
        }
        if (!others.isEmpty()) {
            emit removeIndicesRequested(others);
        }
    } else if (chosen == removeAllAct && total > 0) {
        QList<int> all;
        all.reserve(total);
        for (int i = 0; i < total; ++i) {
            all.append(i);
        }
        emit removeIndicesRequested(all);
    } else if (chosen == copyPathsAct) {
        QStringList paths;
        if (selectedCount > 0) {
            for (int idx : indices) {
                if (QListWidgetItem *it = item(idx)) {
                    paths.append(it->data(RolePath).toString());
                }
            }
        } else if (hit) {
            paths.append(hit->data(RolePath).toString());
        }
        if (!paths.isEmpty()) {
            QGuiApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        }
    }
    event->accept();
}

QStringList ThumbnailBar::mimeTypes() const
{
    return {QStringLiteral("text/uri-list"),
            QStringLiteral("application/x-biltoo-paths"),
            QStringLiteral("application/x-biltoo-session-ids")};
}

QMimeData *ThumbnailBar::mimeData(const QList<QListWidgetItem *> items) const
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QByteArray idPayload;
    QStringList fullPaths;
    urls.reserve(items.size());
    fullPaths.reserve(items.size());
    for (QListWidgetItem *it : items) {
        if (!it) {
            continue;
        }
        const QString path = it->data(RolePath).toString();
        if (path.isEmpty()) {
            continue;
        }
        fullPaths.append(path);
        // Local filesystem files: standard file URLs for external targets.
        // Archive member refs must NOT use the container file URL — expandPaths
        // would then unpack every member and the drop would place the whole
        // archive instead of the selected thumbs only.
        if (!ArchivePath::isArchiveRef(path)) {
            urls.append(QUrl::fromLocalFile(path));
        }
        // Prefer SessionImageId stamped on the item (setFiles / setSessionIds).
        SessionImageId sid = kInvalidSessionImageId;
        const QVariant sidVar = it->data(RoleSessionId);
        if (sidVar.isValid()) {
            sid = static_cast<SessionImageId>(sidVar.toLongLong());
        } else {
            const int r = row(it);
            if (r >= 0 && r < m_sessionIds.size()) {
                sid = m_sessionIds.at(r);
            }
        }
        if (!idPayload.isEmpty()) {
            idPayload.append(',');
        }
        idPayload.append(QByteArray::number(static_cast<qint64>(sid)));
    }
    // Internal path list (newline-separated UTF-8). Preferred on drop so
    // selection size is exact — including //archive: members.
    if (!fullPaths.isEmpty()) {
        mime->setData(QStringLiteral("application/x-biltoo-paths"),
                      fullPaths.join(QLatin1Char('\n')).toUtf8());
    }
    if (!urls.isEmpty()) {
        mime->setUrls(urls);
    } else if (!fullPaths.isEmpty()) {
        // Keep hasUrls() true for generic acceptors: opaque about: URLs are
        // ignored by extractLocalImagePaths / expandPaths.
        QList<QUrl> placeholders;
        for (int i = 0; i < fullPaths.size(); ++i) {
            placeholders.append(QUrl(QStringLiteral("about:biltoo-session/%1").arg(i)));
        }
        mime->setUrls(placeholders);
    }
    if (!idPayload.isEmpty()) {
        mime->setData(QStringLiteral("application/x-biltoo-session-ids"), idPayload);
    }
    return mime;
}

Qt::DropActions ThumbnailBar::supportedDragActions() const
{
    return Qt::CopyAction | Qt::LinkAction;
}

void ThumbnailBar::startFileDrag(const QList<QListWidgetItem *> &items)
{
    if (items.isEmpty()) {
        return;
    }
    QMimeData *mime = mimeData(items);
    if (!mime
        || (mime->urls().isEmpty()
            && mime->data(QStringLiteral("application/x-biltoo-paths")).isEmpty())) {
        delete mime;
        return;
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    if (QListWidgetItem *first = items.first()) {
        QPixmap pix = qvariant_cast<QPixmap>(
            first->data(ThumbnailDelegate::ThumbPixmapRole));
        if (pix.isNull()) {
            const QIcon icon = first->icon();
            const QList<QSize> sizes = icon.availableSizes();
            if (!sizes.isEmpty()) {
                pix = icon.pixmap(sizes.constFirst());
            }
        }
        if (!pix.isNull()) {
            // Keep aspect for the drag preview (iconSize() is square).
            const QSize target = iconSize();
            if (target.width() > 0 && target.height() > 0) {
                pix = pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            drag->setPixmap(pix);
            drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
        }
    }

    drag->exec(supportedDragActions(), Qt::CopyAction);
}

void ThumbnailBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        m_pressActive = false;
        QListWidget::mousePressEvent(event);
        return;
    }

    QListWidgetItem *hit = itemAt(event->pos());
    m_pressPos = event->pos();
    m_pressItem = hit;
    m_pressActive = true;
    m_dragStarted = false;
    m_pressModifiers = event->modifiers();

    // Image / Gallery session strip: Ctrl/Shift multi-select for bulk session
    // ops (remove, etc.). Does not enter Workspace — that is explicit (mode
    // toggle mode, or drag onto the canvas).
    if (!m_multiSelect) {
        if (hit && (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier
                                          | Qt::MetaModifier))) {
            if (selectionMode() == QAbstractItemView::SingleSelection) {
                setSelectionMode(QAbstractItemView::ExtendedSelection);
                setSelectionRectVisible(false);
            }
            QListWidget::mousePressEvent(event);
            event->accept();
            return;
        }
        // Single click navigates the session (list selection)
        QListWidget::mousePressEvent(event);
        return;
    }

    // Workspace mode: selection is normal multi-select (applied on release if
    // the gesture is not a drag). Canvas membership is drag-drop only.
    // Do not change selection on press — that fought drag-and-drop.
    event->accept();
}

void ThumbnailBar::mouseMoveEvent(QMouseEvent *event)
{
    // Drop stale press pointers if the model was rebuilt under us (setFiles,
    // removeIndices, etc.). QListWidgetItem* is not stable across clear().
    if (m_pressItem && row(m_pressItem) < 0) {
        clearPressState();
    }

    if (m_pressActive && !m_dragStarted && (event->buttons() & Qt::LeftButton)
        && m_pressItem) {
        const int dist = (event->pos() - m_pressPos).manhattanLength();
        if (dist >= QApplication::startDragDistance()) {
            // AUDIT M19: drag selected thumbs as files in every mode (no dead gesture).
            m_dragStarted = true;
            // Payload is the pressed thumb, or the current multi-selection when
            // the press is on a selected cell in Workspace multi-select.
            // Never ship the whole filmstrip because of a stale Select-All /
            // mirrored canvas selection after entering Workspace.
            QList<QListWidgetItem *> items;
            if (m_multiSelect && m_pressItem->isSelected()) {
                items = selectedItems();
                // Guard: a full-strip selection is almost always accidental
                // residual state — drag only the pressed row unless the user
                // held Ctrl/Shift (explicit multi intent).
                if (items.size() == count() && count() > 1
                    && !(m_pressModifiers & (Qt::ControlModifier | Qt::ShiftModifier
                                            | Qt::MetaModifier))) {
                    items = {m_pressItem};
                }
            }
            if (items.isEmpty()) {
                items = {m_pressItem};
            }
            startFileDrag(items);
            clearPressState();
            event->accept();
            return;
        }
    }
    if (!m_multiSelect) {
        QListWidget::mouseMoveEvent(event);
    } else {
        event->accept();
    }
}

void ThumbnailBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_multiSelect && event->button() == Qt::LeftButton
        && m_pressActive && !m_dragStarted) {
        // Normal multi-select (selection only — not canvas membership).
        QListWidgetItem *hit = m_pressItem;
        if (hit && row(hit) >= 0) {
            const int r = row(hit);
            const bool ctrl = m_pressModifiers & Qt::ControlModifier;
            const bool shift = m_pressModifiers & Qt::ShiftModifier;

            if (shift && m_selectionAnchor >= 0 && m_selectionAnchor < count()) {
                const int lo = qMin(m_selectionAnchor, r);
                const int hi = qMax(m_selectionAnchor, r);
                if (!ctrl) {
                    for (int i = 0; i < count(); ++i) {
                        if (QListWidgetItem *it = item(i)) {
                            it->setSelected(i >= lo && i <= hi);
                        }
                    }
                } else {
                    for (int i = lo; i <= hi; ++i) {
                        if (QListWidgetItem *it = item(i)) {
                            it->setSelected(true);
                        }
                    }
                }
            } else if (ctrl) {
                hit->setSelected(!hit->isSelected());
                m_selectionAnchor = r;
            } else {
                // Exclusive select
                for (int i = 0; i < count(); ++i) {
                    if (QListWidgetItem *it = item(i)) {
                        it->setSelected(it == hit);
                    }
                }
                m_selectionAnchor = r;
            }
            emit workspaceSelectionChanged();
        } else if (!hit) {
            // Empty space: clear selection
            clearSelection();
            m_selectionAnchor = -1;
            emit workspaceSelectionChanged();
        }
    }

    clearPressState();
    if (!m_multiSelect) {
        QListWidget::mouseReleaseEvent(event);
    } else {
        event->accept();
    }
}

void ThumbnailBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QListWidget::mouseDoubleClickEvent(event);
        return;
    }
    QListWidgetItem *hit = itemAt(event->pos());
    if (!hit) {
        event->accept();
        return;
    }
    // Open / navigate only — never toggle Workspace canvas membership.
    // Place on Workspace via drag-drop (or explicit Workspace selection).
    emit indexActivated(row(hit));
    event->accept();
}
