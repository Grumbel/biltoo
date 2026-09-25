// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/thumbnailbar.h"
#include "view/viewtransform.h"
#include "shell/filmstripgeometry.h"
#include "display/displayquality.h"
#include "display/displaysurface.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "display/imagecache.h"
#include "host/imageloader.h"
#include "host/thumtoocache.h"
#include "session/sessionappearance.h"
#include "content/contentxform.h"

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
#include <QTimer>
#include <QScrollBar>
#include <QCoreApplication>
#include <QEventLoop>
#include "util/biltoo_thread.h"
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
    // Cross-axis margin (bar edge ↔ image). Fixed logical pixels — not a
    // fraction of thumbSize or of the image aspect, so landscape and portrait
    // get the same gap against the strip edge.
    return 8;
}

int ThumbnailDelegate::flowPad() const
{
    return FilmstripGeometry::flowPadFromCellPad(cellPad());
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
    return FilmstripGeometry::letterboxContentSize(m_thumbSize, aspect,
                                                     orient == Qt::Horizontal);
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

    // Prefer session override (crop/appearance) over any installed path thumb.
    QPixmap pm;
    if (const ThumbnailBar *bar = qobject_cast<const ThumbnailBar *>(parent())) {
        pm = bar->resolvedThumbPixmap(index.row());
    }
    if (pm.isNull()) {
        pm = qvariant_cast<QPixmap>(index.data(ThumbPixmapRole));
    }
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
        // sizeHint already = content + pads. Draw at that logical size — do not
        // re-letterbox into inner (that added aspect-dependent side gaps when
        // option.rect was wider than the content, e.g. IconMode iconSize floor).
        const QSize contentSz = logicalContentSize(index);
        const QSize fitted = FilmstripGeometry::fitContentInInner(contentSz, inner.size());
        const int cw = fitted.width();
        const int ch = fitted.height();
        contentRect = QRect(
            inner.x() + (inner.width() - cw) / 2,
            inner.y() + (inner.height() - ch) / 2,
            cw,
            ch);
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
            const int s = FilmstripGeometry::cornerBadgeSize(slot.width(), slot.height());
            const QRect pip(slot.center().x() - s / 2,
                            slot.center().y() - s / 2, s, s);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(180, 180, 180, 200));
            painter->drawEllipse(pip);
        }
    }

    const QString text = index.data(Qt::DisplayRole).toString();
    if (m_labelsVisible && !text.isEmpty() && labelBand > 0) {
        // Under the image: bottom cross/flow pad + label band (see cell sizeHint).
        // Centre the name in that *whole* empty strip — not only the labelBand
        // slice at the bottom (that left the glyph stuck low under a large pad).
        const int underPad = (orient == Qt::Horizontal) ? cross : flow;
        const int labelInset = (orient == Qt::Horizontal) ? flow : cross;
        const int zoneH = underPad + labelBand;
        const QRect textRect(cell.left() + labelInset,
                             cell.bottom() - zoneH,
                             ViewTransform::atLeast1(cell.width() - 2 * labelInset),
                             zoneH);
        const QColor textColor = selected
            ? option.palette.color(QPalette::HighlightedText)
            : option.palette.color(QPalette::Text);
        painter->setPen(textColor);
        painter->setFont(option.font);
        const QString elided = fm.elidedText(text, Qt::ElideMiddle, textRect.width());
        // Optical vertical centre: AlignVCenter uses line spacing and often sits
        // low in the em-box. Pin the baseline from ascent/descent of the elided
        // string so the ink is centred in the under-image zone.
        const QRect ink = fm.boundingRect(elided);
        const int inkH = qMax(1, ink.height());
        const int baseline =
            textRect.top() + (textRect.height() - inkH) / 2 - ink.top();
        const int x = textRect.left()
            + (textRect.width() - fm.horizontalAdvance(elided)) / 2;
        painter->drawText(x, baseline, elided);
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
            const int fold = FilmstripGeometry::membershipFoldPx(contentRect.width());
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

    // Content crop: yellow dog-ear on the bottom-right (mirrors Workspace blue
    // fold on the top-right).
    if (bar && bar->isSessionCropped(index.row()) && contentRect.width() > 8) {
        const int fold = FilmstripGeometry::membershipFoldPx(contentRect.width());
        const QPoint bottomRight(contentRect.right() + 1, contentRect.bottom() + 1);
        const QPoint left(bottomRight.x() - fold, bottomRight.y());
        const QPoint top(bottomRight.x(), bottomRight.y() - fold);

        QPainterPath face;
        face.moveTo(left);
        face.lineTo(bottomRight);
        face.lineTo(top);
        face.closeSubpath();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(242, 196, 40)); // amber / yellow
        painter->drawPath(face);

        const QPoint mid((left.x() + top.x()) / 2, (left.y() + top.y()) / 2);
        QPainterPath under;
        under.moveTo(left);
        under.lineTo(mid);
        under.lineTo(top);
        under.closeSubpath();
        painter->setBrush(QColor(200, 150, 20));
        painter->drawPath(under);

        painter->setPen(QPen(QColor(0, 0, 0, 90), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(left, top);
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
    // DragDrop (not DragOnly): internal session-row drops reorder the session.
    // Movement stays Static so QListWidget does not auto-shuffle items; host
    // owns order via reorderRowsRequested → SessionDocument::replaceAll.
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setAcceptDrops(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
    }
    setStatusTip(tr("Drag to reorder on the strip, or onto the Workspace canvas; "
                    "double-click or Enter opens the image"));
    setToolTip(tr("Drag to reorder · Workspace · double-click opens"));

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
    auto armScrollLoad = [this]() {
        if (!m_scrollLoadTimer) {
            m_scrollLoadTimer = new QTimer(this);
            m_scrollLoadTimer->setSingleShot(true);
            m_scrollLoadTimer->setInterval(80);
            connect(m_scrollLoadTimer, &QTimer::timeout, this, [this]() {
                // setInterest inside scheduleVisibleThumbnailLoads bumps epoch.
                scheduleVisibleThumbnailLoads();
            });
        }
        m_scrollLoadTimer->start();
    };
    if (horizontalScrollBar()) {
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [armScrollLoad](int) { armScrollLoad(); });
    }
    if (verticalScrollBar()) {
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
                [armScrollLoad](int) { armScrollLoad(); });
    }

    {
        auto *filmstripSurfaceTimer = new QTimer(this);
        filmstripSurfaceTimer->setInterval(1500);
        connect(filmstripSurfaceTimer, &QTimer::timeout, this, &ThumbnailBar::filmstripSurfaceTick);
        filmstripSurfaceTimer->start();
    }

    // When thumtoo finishes a ladder level, upgrade filmstrip rows still short
    // of the display edge (LQIP / soft stand-in). Settled when haveEdge >=
    // decodeSize, or soft plateau for soft-band demand (tier Soft, decode ≤ soft max).
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
                    // Always refresh aspect from ItemWorld/native (override is
                    // pixels only). Skip only the LQIP pixel install below.
                    applyLayoutAspect(it, i, size);
                    if (rowHasAppearanceOverride(i)) {
                        continue;
                    }
                    // Durable size even if LQIP already painted.
                    any = true;
                    // LQIP often arrives in the same sizeReady payload (ImageCache).
                    // Install only *after* aspect so cells are never sample-shaped.
                    if (const QImage lqip = ImageCache::get(path);
                        !lqip.isNull()
                        && ImageCache::longEdge(lqip)
                            <= DisplayQuality::kLqipMaxEdge) {
                        setThumbnailIcon(i, lqip);
                    }
                    // Do NOT scheduleFilmstripTilePixels here. Every sizeReady used
                    // to queue pyramid/synth for the whole session in parallel with
                    // the Gallery size gate (felt like "tiles during size query" and
                    // starved Store/CPU). Visible rows load tiles via
                    // scheduleVisibleThumbnailLoads after aspect is known.
                }
                if (any) {
                    // Debounce layout — full doItemsLayout per sizeReady froze the GUI
                    // on large sessions during the probe stream.
                    scheduleLayoutRefresh();
                    // Re-arm visible loads: rows that waited for size can proceed
                    // (LQIP + tiles for the viewport only, not the whole session).
                    // Do not restart an already-running timer — continuous sizeReady
                    // would starve loads the same way progressive pack was starved.
                    if (m_scrollLoadTimer) {
                        if (!m_scrollLoadTimer->isActive()) {
                            m_scrollLoadTimer->start();
                        }
                    } else {
                        scheduleVisibleThumbnailLoads();
                    }
                }
            });
    // Gallery may discover durable tiles first; PreferCache can settle short and
    // leave filmstrip on LQIP forever (g_pixelsSettled + climbPending → evaluate None).
    // Wake strip cells when the process memo flips to durable-yes.
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::durableTilesReady, this,
            [this](const QString &path) {
                if (path.isEmpty() || m_files.isEmpty() || m_visibleLoadsSuspended) {
                    return;
                }
                const int decodeSize = filmstripDecodeEdge();
                bool any = false;
                for (int i = 0; i < m_files.size(); ++i) {
                    if (m_files.at(i) != path) {
                        continue;
                    }
                    any = true;
                    m_thumbAwaitLadder.remove(i);
                    m_thumbLoadScheduled.remove(i);
                    m_thumbFailed.remove(i);
                }
                if (!any) {
                    return;
                }
                // Allow TileSynth/PreferCache to run again for strip edge.
                ThumtooCache::forgetPixelsSettled(path, decodeSize);
                scheduleFilmstripTilePixels(path, decodeSize);
                scheduleVisibleThumbnailLoads();
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            [this](const QString &path, int /*maxEdge*/, const QImage &ready) {
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
                    // Soft is only an underlay — keep climbing until strip edge.
                    if (haveEdge >= decodeSize && !wasAwaiting) {
                        continue;
                    }
                    m_thumbAwaitLadder.remove(i);
                    m_thumbLoadScheduled.remove(i);
                    const QPointer<ThumbnailBar> guard(this);
                    // Prefer the ladder sample just delivered — makeThumbnail may
                    // still see only LQIP if ImageCache put races this slot.
                    const QImage delivered = ready;
                    QThreadPool::globalInstance()->start([guard, i, path, gen, decodeSize,
                                                          delivered]() {
                        ThumbnailBar *bar = guard.data();
                        if (!bar || gen != bar->m_generation.load()) {
                            return;
                        }
                        const SessionImageId rowSid =
                            (i < bar->m_sessionIds.size()) ? bar->m_sessionIds.at(i)
                                                          : kInvalidSessionImageId;
                        QImage image = delivered;
                        if (image.isNull()
                            || qMax(image.width(), image.height()) < decodeSize) {
                            image = bar->makeThumbnail(path, decodeSize, rowSid);
                        } else {
                            // Ladder samples are unoriented host — apply ItemWorld
                            // (XDG fallback) content ops like makeThumbnail.
                            image = bar->applyStoredAppearanceToThumb(path, image, rowSid);
                            image = bar->prepareThumbnailFromImage(image, decodeSize);
                        }
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
                            // Crop/appearance may have landed while the ladder job ran.
                            if (i < host->m_sessionIds.size()) {
                                const SessionImageId rowId = host->m_sessionIds.at(i);
                                if (rowId != kInvalidSessionImageId
                                    && host->m_sessionIdImageOverrides.contains(rowId)) {
                                    host->m_thumbAwaitLadder.remove(i);
                                    host->m_thumbLoadScheduled.remove(i);
                                    emit host->loadsChanged();
                                    return;
                                }
                            }
                            if (host->m_sessionImageOverrides.contains(path)) {
                                host->m_thumbAwaitLadder.remove(i);
                                host->m_thumbLoadScheduled.remove(i);
                                emit host->loadsChanged();
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
                    }, -1);
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
    const int pad = FilmstripGeometry::cellPadFromThumb(thumbSize);
    return 2 * pad + thumbSize
        + ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
}

int ThumbnailBar::thumbSizeForExtent(int extent)
{
    const int label = ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
    // Inverse of extentForThumbSize with pad ≈ thumb/16 — iterate a step.
    int thumb = extent - label;
    for (int i = 0; i < 3; ++i) {
        const int pad = FilmstripGeometry::cellPadFromThumb(thumb);
        thumb = FilmstripGeometry::clampThumbSize(extent - label - 2 * pad, kMinThumbSize, kMaxThumbSize);
    }
    return thumb;
}

int ThumbnailBar::thumbSizeFromBarExtent(int extent) const
{
    if (m_orientation == Qt::Horizontal) {
        // extent is bar height = cell height = pads + thumb + labelBand
        const int pad = m_delegate ? m_delegate->cellPad() : FilmstripGeometry::cellPadFromThumbAlt(m_thumbSize);
        return FilmstripGeometry::clampThumbSize(extent - labelBandHeight() - 2 * pad, kMinThumbSize, kMaxThumbSize);
    }
    // Vertical bar: extent is bar width ≈ cell width = thumb + 2*pad
    return FilmstripGeometry::clampThumbSize(extent - 2 * (m_delegate ? m_delegate->cellPad() : 4), kMinThumbSize, kMaxThumbSize);
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
            marginLeft = ViewTransform::nonNeg(marginLeft);
        }
    } else {
        const int avail = viewport()->height();
        if (bounds.height() > 0 && bounds.height() < avail) {
            marginTop = (avail - bounds.height()) / 2 - bounds.top();
            marginTop = ViewTransform::nonNeg(marginTop);
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
    // Letterbox: sizeHint is authoritative. A square iconSize is a *minimum*
    // item size in IconMode and was widening portrait cells (extra L/R pad) or
    // otherwise fighting variable letterbox width — use 1×1 so layout hugs content.
    // Crop mode: uniform squares need a real iconSize for grid chrome.
    if (m_cropToSquare) {
        setIconSize(QSize(m_thumbSize, m_thumbSize));
    } else {
        setIconSize(QSize(1, 1));
    }
    if (m_delegate) {
        m_delegate->setThumbSize(m_thumbSize);
    }

    const QSize squareCell = m_delegate ? m_delegate->cellSize(font())
                                        : QSize(m_thumbSize + 8, m_thumbSize + labelBandHeight());
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
    const int cross = m_delegate ? m_delegate->cellPad() : 4;
    const int flow = m_delegate ? m_delegate->flowPad() : 2;
    // Inter-image gap = 2·flowPad + spacing ≈ cellPad (matches cross-axis margin).
    setSpacing(ViewTransform::nonNeg(cross - 2 * flow));
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
    const int clamped = FilmstripGeometry::clampThumbSize(pixels, kMinThumbSize, kMaxThumbSize);
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
    // Remaining unloaded work in the visible band + anything already claimed.
    // Counting only scheduled/await flickered as each slot finished before the
    // next row was claimed (status bar 1↔0).
    QSet<int> remaining = m_thumbLoadScheduled;
    remaining.unite(m_thumbAwaitLadder);
    const int n = m_files.size();
    if (n <= 0 || !viewport()) {
        return remaining.size();
    }
    const int decodeSize = filmstripDecodeEdge();
    const QRect vr = viewport()->rect().adjusted(-m_thumbSize, -m_thumbSize,
                                                   m_thumbSize, m_thumbSize);
    for (int i = 0; i < n; ++i) {
        if (remaining.contains(i) || m_thumbFailed.contains(i)) {
            continue;
        }
        QListWidgetItem *it = item(i);
        if (!it) {
            continue;
        }
        if (!visualItemRect(it).intersects(vr)) {
            continue;
        }
        const int haveEdge =
            it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
        if (haveEdge >= decodeSize) {
            continue;
        }
        if (it->data(ThumbnailDelegate::ThumbLoadedRole).toBool()
            && haveEdge > 0) {
            continue;
        }
        remaining.insert(i);
    }
    return remaining.size();
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
    return ViewTransform::atLeast1(qRound(m_thumbSize * dpr));
}

int ThumbnailBar::filmstripDecodeEdge() const
{
    // Sharpness only — never a layout size. Logical cells use thumbSize.
    // Floor at the first real soft ladder step (128). Tiny bars still need more
    // than LQIP (≤96); without a floor, ceil(24)→128 but LQIP was upscaled to
    // 128 and falsely settled as sharp.
    const int want = ThumtooCache::ceilLadderEdge(thumbDecodePixels());
    return qMax(ThumtooCache::kLadderEdges[0], want);
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
    // NEVER put filmstrip icons into ImageCache (path host). Appearance/crop
    // thumbs are not unoriented source — that poisoned Apply and second crop.

    // Session-id override owns the cell unless the installer is the override API.
    if (!m_allowOverrideIconInstall) {
        if (row < m_sessionIds.size()) {
            const SessionImageId sid = m_sessionIds.at(row);
            if (sid != kInvalidSessionImageId
                && m_sessionIdImageOverrides.contains(sid)) {
                if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
                    qWarning().noquote()
                        << QStringLiteral("[filmstrip] skip icon row=%1 sid=%2 (id override owns cell)")
                               .arg(row)
                               .arg(sid);
                }
                return;
            }
        }
        if (row < m_files.size()) {
            const QString path = m_files.at(row);
            if (!path.isEmpty() && m_sessionImageOverrides.contains(path)) {
                if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
                    qWarning().noquote()
                        << QStringLiteral("[filmstrip] skip icon row=%1 path override owns cell")
                               .arg(row);
                }
                return;
            }
        }
    }
    // Decode-edge pixmap for sharpness; layout uses aspect only.
    const int incomingEdge = qMax(image.width(), image.height());
    const int haveEdge = it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
    const bool loaded = it->data(ThumbnailDelegate::ThumbLoadedRole).toBool();
    // No-op if already settled at this edge (stops debug spam + layout thrash).
    // Override installs always replace — crop changes aspect at the same edge.
    if (loaded && haveEdge >= incomingEdge && haveEdge > 0
        && !m_allowOverrideIconInstall) {
        return;
    }
    const QPixmap pm = QPixmap::fromImage(image);
    it->setData(ThumbnailDelegate::ThumbPixmapRole, pm);
    it->setIcon(QIcon(pm));
    it->setData(ThumbnailDelegate::ThumbLoadedRole, true);
    it->setData(ThumbnailDelegate::ThumbDecodeEdgeRole, incomingEdge);

    // SIZE.md: cell geometry from layoutAspectForRow (ItemWorld + native), never
    // sample/override pixmap size. Override only chooses which pixels to paint.
    if (!m_cropToSquare) {
        applyLayoutAspect(it, row);
        const QSize content = it->data(ThumbnailDelegate::ThumbContentSizeRole).toSize();
        if (content.width() < 1 || content.height() < 1) {
            const QSize prev = content;
            Q_UNUSED(prev);
            const QSize aspect = layoutAspectForRow(row);
            if (aspect.width() > 0 && aspect.height() > 0) {
                applyLayoutAspect(it, row, aspect);
            } else {
                // Keep prior sizeHint if any; else provisional square.
                const QSize keep = it->sizeHint();
                if (keep.width() < 1 || keep.height() < 1) {
                    const QSize prov = m_delegate->letterboxContentSize(
                        m_delegate->provisionalContentSize());
                    it->setData(ThumbnailDelegate::ThumbContentSizeRole, prov);
                    it->setSizeHint(m_delegate->cellSizeForContent(font(), prov));
                }
            }
        }
    } else {
        const QSize content(m_thumbSize, m_thumbSize);
        it->setData(ThumbnailDelegate::ThumbContentSizeRole, content);
        it->setSizeHint(m_delegate->cellSize(font()));
    }

    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
        const QSize content =
            it->data(ThumbnailDelegate::ThumbContentSizeRole).toSize();
        const QSize hint = it->sizeHint();
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
    // Never upscale. Upscaling LQIP to filmstripDecodeEdge made ThumbDecodeEdgeRole
    // report the target edge while pixels stayed soft — filmstripSurfaceTick then
    // treated the cell as settled and tiny filmstrips stayed blurry forever.
    const int srcEdge = qMax(image.width(), image.height());
    if (m_cropToSquare) {
        const int side = qMin(image.width(), image.height());
        if (side <= 0) {
            return QImage();
        }
        const int x = (image.width() - side) / 2;
        const int y = (image.height() - side) / 2;
        QImage square = image.copy(x, y, side, side);
        if (side <= maxSize) {
            return square;
        }
        return square.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Letterbox: downscale longest edge to maxSize only. Layout uses
    // letterboxContentSize(aspect) at thumbSize — not these pixel dimensions.
    if (srcEdge <= maxSize) {
        return image;
    }
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

void ThumbnailBar::scheduleFilmstripTilePixels(const QString &path, int edge) const
{
    if (path.isEmpty() || edge <= 0 || !ThumtooCache::isAvailable()) {
        return;
    }
    // Warm host already covers strip edge — zero schedule work (surface tick
    // installs into the cell icon). Early-out must not skip that install path.
    if (ImageCache::longEdge(ImageCache::get(path)) >= edge) {
        return;
    }
    if (ThumtooCache::hasDurableTilesKnown(path)) {
        // PreferCache may have settled a short soft while Gallery already holds
        // tiles; clear settle so TileSynth can run for the strip edge again.
        ThumtooCache::forgetPixelsSettled(path, edge);
        (void)ThumtooCache::scheduleTileSynthOrPyramid(path, edge);
        return;
    }
    // Cold / memo stale: discover coverage on a worker (hasDurableTiles updates
    // the process memo). Pyramid completion does not emit ladderReady — without
    // rediscovery filmstrip stayed on LQIP forever after scheduleTilePyramid.
    const QString pathCopy = path;
    const int edgeCopy = edge;
    QThreadPool::globalInstance()->start([pathCopy, edgeCopy]() {
        if (ThumtooCache::hasDurableTiles(pathCopy)) {
            (void)ThumtooCache::scheduleTileSynthOrPyramid(pathCopy, edgeCopy);
            return;
        }
        (void)ThumtooCache::scheduleTilePyramid(pathCopy);
    });
}


QImage ThumbnailBar::applyStoredAppearanceToThumb(const QString &path, const QImage &src,
                                                  SessionImageId sessionId) const
{
    if (src.isNull() || path.isEmpty()) {
        return src;
    }
    WorkspaceItemState st;
    if (m_contentAppearanceProvider) {
        st = m_contentAppearanceProvider(sessionId, path);
    }
    // Fallback path XDG only for unbound rows (bound = ItemWorld only).
    if (sessionId == kInvalidSessionImageId
        && !SessionAppearance::hasContentAppearance(st)
        && st.colorAdjust.isIdentity()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
            SessionAppearance::applyStoredContentAppearance(&st, stored);
        }
    }
    const bool hasOrient = SessionAppearance::hasContentAppearance(st)
        || !st.colorAdjust.isIdentity();
    if (!hasOrient) {
        return src;
    }
    // Filmstrip is SoftPreview only. materializeDisplay asserts NOT GUI when
    // long edge > kGuiMaterializeMaxEdge (e.g. host cache holds full/overview
    // after crop apply). Clamp before bake so session-remove / setCurrentIndex
    // refresh cannot abort on the GUI thread.
    QImage host = src;
    const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
    if (ImageCache::longEdge(host) > maxGui) {
        host = ImageCache::clampToMaxEdge(host, maxGui);
    }
    if (host.isNull()) {
        return src;
    }
    return SessionAppearance::applyContentToImage(
        host, st, SessionAppearance::PixelKind::SoftPreview);
}

QImage ThumbnailBar::makeThumbnail(const QString &path, int maxSize,
                                   SessionImageId sessionId) const
{
    // Process-memory first. Soft / classic loadThumbnail encode is removed for
    // filmstrip — sharpness is tiles (TileSynth) after LQIP underlay.
    QImage image = ImageCache::get(path, maxSize);
    if (image.isNull()) {
        image = ImageCache::get(path);
    }
    // Worker-only Store LQIP when ImageCache is empty (GUI path is a no-op).
    // Without this the strip waited on tile pyramid with a blank cell even when
    // durable LQIP existed.
    if (image.isNull() && ThumtooCache::isAvailable()) {
        image = ThumtooCache::cachedLqipImage(path);
        if (!image.isNull()) {
            ImageCache::put(path, image);
        }
    }
    if (image.isNull()) {
        return {};
    }
    image = applyStoredAppearanceToThumb(path, image, sessionId);
    return prepareThumbnailFromImage(image, maxSize);
}

QImage ThumbnailBar::sampleForImageModePending(const QString &path, SessionImageId sid,
                                               bool *displayReadyOut) const
{
    if (displayReadyOut) {
        *displayReadyOut = false;
    }
    if (path.isEmpty()) {
        return {};
    }
    // Bound SessionImageId: never return content-baked override to Image
    // underlay (ECS_GUI_BYPASSES #1/#2). Override is filmstrip cell paint only;
    // Image soft always materializes host-raw + ItemWorld want.
    // Path-only override: unbound rows only.
    if (sid == kInvalidSessionImageId) {
        const auto it = m_sessionImageOverrides.constFind(path);
        if (it != m_sessionImageOverrides.cend() && !it.value().isNull()) {
            if (displayReadyOut) {
                *displayReadyOut = true;
            }
            return it.value();
        }
    }
    // Shared host cache (filmstrip makeThumbnail puts host-raw here).
    QImage host = ImageCache::get(path);
    // 4) Filmstrip cell pixmap — survives ImageCache LRU eviction; path-row
    //    icons from makeThumbnail are host-derived (not override).
    for (int row = 0; row < m_files.size(); ++row) {
        if (m_files.at(row) != path) {
            continue;
        }
        if (row < m_sessionIds.size()) {
            const SessionImageId rowId = m_sessionIds.at(row);
            if (rowId != kInvalidSessionImageId
                && m_sessionIdImageOverrides.contains(rowId)) {
                // Baked override — only usable when sid matches (handled above).
                continue;
            }
        }
        const QListWidgetItem *it = item(row);
        if (!it || !it->data(ThumbnailDelegate::ThumbLoadedRole).toBool()) {
            continue;
        }
        const QPixmap pm = it->data(ThumbnailDelegate::ThumbPixmapRole).value<QPixmap>();
        if (pm.isNull()) {
            continue;
        }
        const QImage icon = pm.toImage();
        if (icon.isNull()) {
            continue;
        }
        const int iconEdge = qMax(icon.width(), icon.height());
        const int hostEdge = ImageCache::longEdge(host);
        if (iconEdge > hostEdge) {
            // Re-seed host cache so Image mode and strip stay aligned.
            /* ImageCache host-raw only — filmstrip icon may be XDG-baked */
            // ImageCache::put(path, icon);
            host = icon;
        } else if (hostEdge <= 0) {
            /* ImageCache host-raw only — filmstrip icon may be XDG-baked */
            // ImageCache::put(path, icon);
            host = icon;
        }
        break;
    }
    return host;
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
            m_allowOverrideIconInstall = true;
            setThumbnailIcon(row, thumb);
            m_allowOverrideIconInstall = false;
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
        QSet<SessionImageId> stickyKept;
        for (SessionImageId id : kept.keys()) {
            if (m_sessionIdCropSticky.contains(id)) {
                stickyKept.insert(id);
            }
        }
        m_sessionIdCropSticky.swap(stickyKept);
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
            m_allowOverrideIconInstall = true;
            setThumbnailIcon(row, thumb);
            m_allowOverrideIconInstall = false;
        }
    }
}

void ThumbnailBar::setSessionImageOverride(SessionImageId sessionId, const QString &path,
                                           const QImage &image, bool fromCropApply,
                                           bool hasCrop)
{
    if (image.isNull()) {
        return;
    }
    if (sessionId == kInvalidSessionImageId) {
        // Unbound tiles only. Path-wide update is last resort (IDENTITY.md).
        setSessionImageOverride(path, image);
        return;
    }
    // Crop bake owns the cell until a later crop Apply/reset (fromCropApply).
    // Ordinary appearance emits after soft install must not demote the crop.
    if (!fromCropApply && m_sessionIdCropSticky.contains(sessionId)) {
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
            qWarning().noquote()
                << QStringLiteral(
                       "[filmstrip] skip appearance override sid=%1 (crop sticky) img=%2x%3")
                       .arg(sessionId)
                       .arg(image.width()).arg(image.height());
        }
        return;
    }
    if (fromCropApply) {
        if (hasCrop) {
            m_sessionIdCropSticky.insert(sessionId);
        } else {
            m_sessionIdCropSticky.remove(sessionId);
        }
    }
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FILMSTRIP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[filmstrip] override sid=%1 cropApply=%2 sticky=%3 img=%4x%5 path=%6")
                   .arg(sessionId)
                   .arg(fromCropApply ? 1 : 0)
                   .arg(m_sessionIdCropSticky.contains(sessionId) ? 1 : 0)
                   .arg(image.width()).arg(image.height())
                   .arg(path);
    }
    m_sessionIdImageOverrides.insert(sessionId, image);
    const QImage thumb = prepareThumbnailFromImage(image, filmstripDecodeEdge());
    if (thumb.isNull()) {
        return;
    }
    bool painted = false;
    for (int row = 0; row < m_sessionIds.size() && row < m_files.size(); ++row) {
        if (m_sessionIds.at(row) != sessionId) {
            continue;
        }
        if (QListWidgetItem *it = item(row)) {
            it->setData(ThumbnailDelegate::ThumbLoadedRole, false);
            it->setData(ThumbnailDelegate::ThumbDecodeEdgeRole, 0);
        }
        m_allowOverrideIconInstall = true;
        setThumbnailIcon(row, thumb);
        m_allowOverrideIconInstall = false;
        painted = true;
        break;
    }
    // Always repaint — paint() reads overrides live via resolvedThumbPixmap.
    if (viewport()) {
        viewport()->update();
    }
    if (!painted) {
        // Id not in the strip yet — keep override for when the row appears.
        Q_UNUSED(path);
    }
}


QPixmap ThumbnailBar::resolvedThumbPixmap(int row) const
{
    if (row < 0 || row >= count()) {
        return {};
    }
    // Session-id override wins (crop / orient bake).
    if (row < m_sessionIds.size()) {
        const SessionImageId sid = m_sessionIds.at(row);
        if (sid != kInvalidSessionImageId) {
            const auto it = m_sessionIdImageOverrides.constFind(sid);
            if (it != m_sessionIdImageOverrides.cend() && !it.value().isNull()) {
                const QImage thumb =
                    prepareThumbnailFromImage(it.value(), filmstripDecodeEdge());
                if (!thumb.isNull()) {
                    return QPixmap::fromImage(thumb);
                }
            }
        }
    }
    // Path override only when this row is unbound.
    if (row < m_sessionIds.size()
        && m_sessionIds.at(row) == kInvalidSessionImageId
        && row < m_files.size()) {
        const QString path = m_files.at(row);
        const auto it = m_sessionImageOverrides.constFind(path);
        if (it != m_sessionImageOverrides.cend() && !it.value().isNull()) {
            const QImage thumb =
                prepareThumbnailFromImage(it.value(), filmstripDecodeEdge());
            if (!thumb.isNull()) {
                return QPixmap::fromImage(thumb);
            }
        }
    }
    if (QListWidgetItem *it = item(row)) {
        return qvariant_cast<QPixmap>(it->data(ThumbnailDelegate::ThumbPixmapRole));
    }
    return {};
}

void ThumbnailBar::setOnCanvasIndices(const QSet<int> &indices)
{
    if (m_onCanvasIndices == indices) {
        return;
    }
    m_onCanvasIndices = indices;
    viewport()->update();
}

bool ThumbnailBar::isSessionCropped(int row) const
{
    if (row < 0 || row >= m_sessionIds.size()) {
        return false;
    }
    const SessionImageId sid = m_sessionIds.at(row);
    if (sid == kInvalidSessionImageId) {
        return false;
    }
    return m_sessionIdCropSticky.contains(sid);
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
        applyLayoutAspect(it, i);
        const QSize content = it->data(ThumbnailDelegate::ThumbContentSizeRole).toSize();
        if (content.width() < 1 || content.height() < 1) {
            const QSize prov = m_delegate->letterboxContentSize(
                m_delegate->provisionalContentSize());
            it->setData(ThumbnailDelegate::ThumbContentSizeRole, prov);
            it->setSizeHint(m_delegate->cellSizeForContent(font(), prov));
        }
    }
    doItemsLayout();
    updateCenteringMargins();
    if (viewport()) {
        viewport()->update();
    }
}



void ThumbnailBar::rebindFilmstripSurfaces()
{
    for (DisplaySurface::SurfaceId id : m_rowSurfaceIds) {
        if (id != DisplaySurface::kInvalidSurfaceId) {
            m_displaySurfaces.unbind(id);
        }
    }
    m_rowSurfaceIds.clear();
    m_rowSurfaceIds.resize(m_files.size());
    for (int i = 0; i < m_files.size(); ++i) {
        const QString &path = m_files.at(i);
        if (path.isEmpty()) {
            m_rowSurfaceIds[i] = DisplaySurface::kInvalidSurfaceId;
            continue;
        }
        SessionImageId sid = kInvalidSessionImageId;
        if (i < m_sessionIds.size()) {
            sid = m_sessionIds.at(i);
        }
        m_rowSurfaceIds[i] = m_displaySurfaces.bind(
            DisplaySurface::Kind::FilmstripCell, path, sid);
    }
}

void ThumbnailBar::filmstripSurfaceTick()
{
    if (m_files.isEmpty() || m_visibleLoadsSuspended) {
        return;
    }
    const int decodeSize = filmstripDecodeEdge();
    const QRect vis = viewport()->rect().adjusted(-40, -40, 40, 40);
    bool needSchedule = false;
    for (int i = 0; i < m_files.size(); ++i) {
        QListWidgetItem *it = item(i);
        if (!it) {
            continue;
        }
        const QRect r = visualItemRect(it);
        if (!r.intersects(vis)) {
            continue;
        }
        const QString path = m_files.at(i);
        if (path.isEmpty()) {
            continue;
        }
        const int shown = it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
        const bool climbPending =
            m_thumbLoadScheduled.contains(i) || m_thumbAwaitLadder.contains(i);

        // Already meets filmstrip display edge — settled. Do not re-prepare
        // from host every 1.5s (that spammed setThumbnailIcon forever whenever
        // hostEdge > shown, even at target).
        if (shown >= decodeSize) {
            m_thumbAwaitLadder.remove(i);
            m_thumbLoadScheduled.remove(i);
            continue;
        }
        // LQIP-only underlay is not terminal — keep driving tiles until strip edge.
        // Always re-arm TileSynth/pyramid for short visible cells (awaitLadder used
        // to stick after pyramid with no PreferCache).
        // If PreferCache already settled short, forget so the next schedule is not a no-op.
        if (climbPending
            && DisplayQuality::hostLongEdge(path) < decodeSize
            && ThumtooCache::hasDurableTilesKnown(path)) {
            ThumtooCache::forgetPixelsSettled(path, decodeSize);
            // Allow DisplaySurface to ScheduleClimb again; stuck climbPending +
            // evaluate None left cells on LQIP until selection forced a reload.
            m_thumbAwaitLadder.remove(i);
            m_thumbLoadScheduled.remove(i);
        }
        scheduleFilmstripTilePixels(path, decodeSize);

        // Session-id crop/appearance owns the cell — never paint raw host over it.
        // Path-only rows use DisplaySurface::decide below for host upgrades.
        if (i < m_sessionIds.size()) {
            const SessionImageId sid = m_sessionIds.at(i);
            if (sid != kInvalidSessionImageId
                && m_sessionIdImageOverrides.contains(sid)) {
                continue;
            }
        }
        if (m_sessionImageOverrides.contains(path)) {
            continue;
        }
        // Path-only cells: bound FilmstripCell surface + evaluate.
        DisplaySurface::SurfaceId sid = DisplaySurface::kInvalidSurfaceId;
        if (i < m_rowSurfaceIds.size()) {
            sid = m_rowSurfaceIds.at(i);
        }
        if (sid == DisplaySurface::kInvalidSurfaceId) {
            SessionImageId sess = kInvalidSessionImageId;
            if (i < m_sessionIds.size()) {
                sess = m_sessionIds.at(i);
            }
            sid = m_displaySurfaces.bind(
                DisplaySurface::Kind::FilmstripCell, path, sess);
            if (i < m_rowSurfaceIds.size()) {
                m_rowSurfaceIds[i] = sid;
            }
        }
        const int hostEdge = DisplayQuality::hostLongEdge(path);
        m_displaySurfaces.setNeed(sid, decodeSize);
        m_displaySurfaces.setHostLongEdge(sid, hostEdge);
        m_displaySurfaces.setClimbPending(sid, climbPending);
        DisplaySurface::AttachedKind ak = DisplaySurface::AttachedKind::None;
        if (shown > 0) {
            ak = (shown >= decodeSize)
                ? DisplaySurface::AttachedKind::FullSource
                : DisplaySurface::AttachedKind::SoftPreview;
        }
        m_displaySurfaces.setAttached(sid, ak, shown, ContentXform::Value{});
        const DisplaySurface::Action act = m_displaySurfaces.evaluate(sid);
        using AT = DisplaySurface::ActionType;
        if (act.type == AT::None) {
            continue;
        }
        if (act.type == AT::AttachSoft || act.type == AT::AttachFull) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                const SessionImageId rowSid =
                    (i < m_sessionIds.size()) ? m_sessionIds.at(i)
                                              : kInvalidSessionImageId;
                QImage oriented = applyStoredAppearanceToThumb(path, host, rowSid);
                const QImage thumb = prepareThumbnailFromImage(oriented, decodeSize);
                if (!thumb.isNull()) {
                    const int newShown = qMax(thumb.width(), thumb.height());
                    if (newShown > shown) {
                        setThumbnailIcon(i, thumb);
                    }
                    if (newShown >= decodeSize) {
                        m_thumbAwaitLadder.remove(i);
                        m_thumbLoadScheduled.remove(i);
                        continue;
                    }
                }
            }
            // Host not enough for strip edge — allow soft schedule once.
            if (!climbPending) {
                m_thumbAwaitLadder.remove(i);
                m_thumbLoadScheduled.remove(i);
                needSchedule = true;
            }
            continue;
        }
        if (act.type == AT::ScheduleClimb
            || act.type == AT::ScheduleAsyncMaterialize) {
            if (!climbPending) {
                m_thumbAwaitLadder.remove(i);
                m_thumbLoadScheduled.remove(i);
                needSchedule = true;
            }
            continue;
        }
    }
    if (needSchedule) {
        scheduleVisibleThumbnailLoads();
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
    GUI_BUDGET("ThumbnailBar::scheduleVisibleThumbnailLoads");
    if (m_files.isEmpty() || m_visibleLoadsSuspended) {
        return;
    }
    const quint64 gen = m_generation.load();
    // Decode at (or just above) visual demand. Soft ≤512 is a placeholder;
    // overview covers up to kBatchOverviewEdge via thumtoo.
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
            const int cell = ViewTransform::atLeast1(m_thumbSize + 8);
            const int across = ViewTransform::atLeast1(viewport()->width() / cell);
            const int down = ViewTransform::atLeast1(viewport()->height() / cell);
            const int over = FilmstripGeometry::virtualOverscan(across, down);
            lo = ViewTransform::nonNeg(minRow - over);
            hi = qMin(n, maxRow + over + 1);
        } else {
            // No geometry yet — seed from selection or start.
            FilmstripGeometry::seedVirtualRange(currentRow(), n, 24, &lo, &hi);
        }
    }

    // Filmstrip: LQIP underlay + tiles (TileSynth when pyramid known). Soft
    // PreferCache encode is removed. Cap concurrent cache reads / tile drives.
    static const int kMaxConcurrentThumbLoads = []() {
        int v = 6;
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
        const QString path = m_files.at(i);
        // Overrides first — never skip a crop/appearance bake because the cell
        // already "settled" on a path decode (that blocked filmstrip crop updates).
        SessionImageId sid = kInvalidSessionImageId;
        if (i < m_sessionIds.size()) {
            sid = m_sessionIds.at(i);
            if (sid != kInvalidSessionImageId
                && m_sessionIdImageOverrides.contains(sid)) {
                const QImage thumb = prepareThumbnailFromImage(
                    m_sessionIdImageOverrides.value(sid), decodeSize);
                if (!thumb.isNull()) {
                    m_allowOverrideIconInstall = true;
                    setThumbnailIcon(i, thumb);
                    m_allowOverrideIconInstall = false;
                }
                continue;
            }
        }
        if (sid == kInvalidSessionImageId && m_sessionImageOverrides.contains(path)) {
            const QImage thumb = prepareThumbnailFromImage(
                m_sessionImageOverrides.value(path), decodeSize);
            if (!thumb.isNull()) {
                m_allowOverrideIconInstall = true;
                setThumbnailIcon(i, thumb);
                m_allowOverrideIconInstall = false;
            }
            continue;
        }
        // Soft placeholder is fine until long edge meets the display ladder step.
        if (QListWidgetItem *it = item(i)) {
            const int haveEdge =
                it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
            if (haveEdge >= decodeSize) {
                continue;
            }
            // LQIP underlay is not settled — still need soft/overview for strip edge.
        }
        if (inFlight >= kMaxConcurrentThumbLoads) {
            break;
        }
        // Size-first: never decode/paint thumbs until durable aspect is known.
        // LQIP/soft samples before sizeReady forced provisional square cells and
        // wrong filmstrip aspect on cold open.
        if (!m_cropToSquare) {
            const QSize known = ThumtooCache::cachedSize(path);
            if (!(known.isValid() && known.width() > 0 && known.height() > 0)) {
                if (ThumtooCache::isAvailable()) {
                    ThumtooCache::scheduleProbe(path);
                }
                continue;
            }
        }
        // Paint ImageCache LQIP on the GUI now — do not wait for a pool job or
        // tile pyramid when warm/probe already left a sample in process memory.
        {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                if (QListWidgetItem *it = item(i)) {
                    const int have =
                        it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
                    const int hostEdge = ImageCache::longEdge(host);
                    if (hostEdge > have) {
                        SessionImageId rowSid = kInvalidSessionImageId;
                        if (i < m_sessionIds.size()) {
                            rowSid = m_sessionIds.at(i);
                        }
                        QImage oriented =
                            applyStoredAppearanceToThumb(path, host, rowSid);
                        const QImage thumb =
                            prepareThumbnailFromImage(oriented, decodeSize);
                        if (!thumb.isNull()) {
                            setThumbnailIcon(i, thumb);
                        }
                    }
                    // Already at strip edge — no tile job needed.
                    const int haveAfter =
                        it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
                    if (haveAfter >= decodeSize) {
                        continue;
                    }
                    // Drive tiles now — do not wait for a pool job to discover
                    // that LQIP is weak (that left the strip pixelated for seconds).
                    scheduleFilmstripTilePixels(path, decodeSize);
                }
            }
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
            const SessionImageId rowSid =
                (i < bar->m_sessionIds.size()) ? bar->m_sessionIds.at(i)
                                              : kInvalidSessionImageId;
            const QImage image = bar->makeThumbnail(path, decodeSize, rowSid);
            bar = guard.data();
            if (!bar || gen != bar->m_generation.load()) {
                return;
            }
            const int gotEdge = image.isNull() ? 0 : qMax(image.width(), image.height());
            // LQIP / tiny host samples are placeholders only — must not settle the
            // row or we never schedule soft and the strip stays blurry forever.
            const bool weakPlaceholder =
                !image.isNull() && gotEdge < decodeSize;
            if (image.isNull() || weakPlaceholder) {
                QMetaObject::invokeMethod(bar, [guard, i, gen, path, decodeSize, image,
                                                gotEdge, weakPlaceholder]() {
                    ThumbnailBar *const host = guard.data();
                    if (!host || gen != host->m_generation.load()) {
                        return;
                    }
                    host->m_thumbLoadScheduled.remove(i);
                    // Session-id (or path) override already owns this cell — never
                    // paint a weaker path decode over a crop/appearance override.
                    if (i < host->m_sessionIds.size()) {
                        const SessionImageId rowId = host->m_sessionIds.at(i);
                        if (rowId != kInvalidSessionImageId
                            && host->m_sessionIdImageOverrides.contains(rowId)) {
                            emit host->loadsChanged();
                            return;
                        }
                    }
                    if (host->m_sessionImageOverrides.contains(path)) {
                        emit host->loadsChanged();
                        return;
                    }
                    // Size-first: do not paint LQIP/soft until native aspect is known.
                    const QSize known = ThumtooCache::cachedSize(path);
                    const bool haveSize = known.isValid() && known.width() > 0
                        && known.height() > 0;
                    if (weakPlaceholder && !image.isNull() && haveSize) {
                        if (QListWidgetItem *it = host->item(i)) {
                            const int have =
                                it->data(ThumbnailDelegate::ThumbDecodeEdgeRole).toInt();
                            if (gotEdge > have) {
                                host->setThumbnailIcon(i, image);
                            }
                        } else {
                            host->setThumbnailIcon(i, image);
                        }
                    }
                    if (ThumtooCache::isAvailable()) {
                        host->m_thumbAwaitLadder.insert(i);
                        if (!haveSize) {
                            ThumtooCache::scheduleProbe(path);
                        } else {
                            // LQIP/cache underlay may already show; drive tiles.
                            host->scheduleFilmstripTilePixels(path, decodeSize);
                        }
                    } else if (image.isNull()) {
                        host->m_thumbFailed.insert(i);
                    }
                    host->viewport()->update();
                    emit host->loadsChanged();
                }, Qt::QueuedConnection);
                return;
            }
            // A crop may have landed while this job ran — do not clobber it.
            // Path override only protects unbound rows; bound rows use id map.
            // Always clear scheduled on the GUI thread so the row is not stuck.
            QMetaObject::invokeMethod(bar, [guard, i, path, gen, image, decodeSize]() {
                ThumbnailBar *const host = guard.data();
                if (!host || gen != host->m_generation.load()) {
                    return;
                }
                if (i < 0 || i >= host->m_files.size() || host->m_files.at(i) != path) {
                    return;
                }
                host->m_thumbLoadScheduled.remove(i);
                host->m_thumbFailed.remove(i);
                if (i < host->m_sessionIds.size()) {
                    const SessionImageId rowId = host->m_sessionIds.at(i);
                    if (rowId != kInvalidSessionImageId
                        && host->m_sessionIdImageOverrides.contains(rowId)) {
                        emit host->loadsChanged();
                        return;
                    }
                }
                if (host->m_sessionImageOverrides.contains(path)) {
                    emit host->loadsChanged();
                    return;
                }
                host->setThumbnailIcon(i, image);
                const int got = ImageCache::longEdge(image);
                if (got < decodeSize) {
                    host->m_thumbAwaitLadder.insert(i);
                    host->scheduleFilmstripTilePixels(path, decodeSize);
                }
                emit host->loadsChanged();
                // Free slot may allow more visible rows to start.
                host->scheduleVisibleThumbnailLoads();
            }, Qt::QueuedConnection);
        }, -1);
    }
    emit loadsChanged();
}

void ThumbnailBar::clearPressState()
{
    m_pressActive = false;
    m_pressItem = nullptr;
    m_dragStarted = false;
    m_pressSelectedRows.clear();
}


bool ThumbnailBar::rowHasAppearanceOverride(int row) const
{
    if (row < 0 || row >= m_files.size()) {
        return false;
    }
    if (row < m_sessionIds.size()) {
        const SessionImageId sid = m_sessionIds.at(row);
        if (sid != kInvalidSessionImageId
            && m_sessionIdImageOverrides.contains(sid)) {
            return true;
        }
    }
    const QString &path = m_files.at(row);
    return !path.isEmpty() && m_sessionImageOverrides.contains(path);
}

QSize ThumbnailBar::layoutAspectForRow(int row) const
{
    // Ground truth: host provider (ItemWorld SessionImageId + native size).
    // Path-only XDG is fallback when unbound or provider unset — never override
    // pixmap size (that mixed soft sample aspect into cell geometry).
    if (row < 0 || row >= m_files.size()) {
        return {};
    }
    const QString path = m_files.at(row);
    const SessionImageId sid = (row < m_sessionIds.size()) ? m_sessionIds.at(row)
                                                           : kInvalidSessionImageId;
    if (m_layoutAspectProvider) {
        const QSize fromHost = m_layoutAspectProvider(sid, path);
        if (fromHost.width() > 0 && fromHost.height() > 0) {
            return fromHost;
        }
    }
    if (path.isEmpty()) {
        return {};
    }
    QSize native = ThumtooCache::cachedSize(path);
    if (native.width() < 1 || native.height() < 1) {
        return {};
    }
    // Bound: provider miss means ItemWorld has no content row — native only.
    // Path XDG orient would desync from Image underlay (2198). Unbound: full XDG.
    if (sid != kInvalidSessionImageId) {
        return native;
    }
    WorkspaceItemState layoutSt;
    ThumtooCache::StoredContentAppearance stored;
    if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
        SessionAppearance::applyStoredContentAppearance(&layoutSt, stored, false);
    }
    return ContentXform::layoutSize(native, layoutSt);
}

void ThumbnailBar::applyLayoutAspect(QListWidgetItem *item, int row, const QSize &nativeHint)
{
    if (!item || !m_delegate || m_cropToSquare) {
        return;
    }
    QSize aspect = layoutAspectForRow(row);
    if ((aspect.width() < 1 || aspect.height() < 1)
        && nativeHint.width() > 0 && nativeHint.height() > 0) {
        // Provider miss: orient nativeHint via path/session same as layoutAspectForRow.
        const QString path = (row >= 0 && row < m_files.size()) ? m_files.at(row) : QString();
        const SessionImageId sid = (row >= 0 && row < m_sessionIds.size())
            ? m_sessionIds.at(row) : kInvalidSessionImageId;
        if (m_layoutAspectProvider && !path.isEmpty()) {
            aspect = m_layoutAspectProvider(sid, path);
        }
        if (aspect.width() < 1 || aspect.height() < 1) {
            WorkspaceItemState layoutSt;
            // Path XDG orient only for unbound rows (bound = ItemWorld / native).
            if (sid == kInvalidSessionImageId && !path.isEmpty()) {
                ThumtooCache::StoredContentAppearance stored;
                if (ThumtooCache::loadContentAppearance(path, &stored)
                    && !stored.isIdentity()) {
                    SessionAppearance::applyStoredContentAppearance(
                        &layoutSt, stored, false);
                }
            }
            aspect = ContentXform::layoutSize(nativeHint, layoutSt);
        }
        if (aspect.width() < 1 || aspect.height() < 1) {
            aspect = nativeHint;
        }
    }
    if (aspect.width() < 1 || aspect.height() < 1) {
        return;
    }
    const QSize content = m_delegate->letterboxContentSize(aspect);
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
        // Already have a real thumb or appearance override — those own aspect.
        if (it->data(ThumbnailDelegate::ThumbLoadedRole).toBool()
            || rowHasAppearanceOverride(i)) {
            continue;
        }
        const QString &path = m_files.at(i);
        if (path.isEmpty()) {
            continue;
        }
        if (const QSize cached = ThumtooCache::cachedSize(path);
            cached.isValid() && cached.width() > 0 && cached.height() > 0) {
            applyLayoutAspect(it, i, cached);
            any = true;
        } else if (ThumtooCache::isAvailable()) {
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
        QSet<SessionImageId> stickyKept;
        for (SessionImageId id : kept.keys()) {
            if (m_sessionIdCropSticky.contains(id)) {
                stickyKept.insert(id);
            }
        }
        m_sessionIdCropSticky.swap(stickyKept);
    }
    setFiles(files);
    // setFiles clears rows and sizes them from *unoriented* native cache size.
    // Re-install session-id appearance overrides so rotated/cropped rows keep
    // oriented aspect (drop/Duplicate called setSession and cells jumped).
    for (int row = 0; row < m_sessionIds.size() && row < m_files.size(); ++row) {
        const SessionImageId sid = m_sessionIds.at(row);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        auto it = m_sessionIdImageOverrides.constFind(sid);
        if (it == m_sessionIdImageOverrides.cend() || it.value().isNull()) {
            continue;
        }
        const QImage thumb = prepareThumbnailFromImage(it.value(), filmstripDecodeEdge());
        if (thumb.isNull()) {
            continue;
        }
        m_allowOverrideIconInstall = true;
        setThumbnailIcon(row, thumb);
        m_allowOverrideIconInstall = false;
    }
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
    rebindFilmstripSurfaces();
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

    // Provisional cell size is computed in appendFileRowsChunk (same path as
    // Gallery::primeGalleryGeometryFromCache). Large sessions: do not build
    // tens of thousands of QListWidgetItems in one
    // stack frame (setFiles was ~60s inside onSizeResolveGateComplete via a
    // same-thread DirectConnection to gallerySizeResolveFinished).
    GUI_BUDGET("ThumbnailBar::setFiles");
    m_fileFillGeneration += 1;
    m_fileFillNext = 0;
    m_fileFillPriority = -1;
    appendFileRowsChunk();
}

void ThumbnailBar::materializeFileRows(int begin, int end, const QSize &provCell)
{
    if (begin < 0 || end <= begin || begin >= m_files.size()) {
        return;
    }
    end = qMin(end, m_files.size());
    for (int i = begin; i < end; ++i) {
        const QString &path = m_files.at(i);
        auto *item = new QListWidgetItem(this);
        item->setText(PagePath::displayName(path));
        item->setToolTip(path);
        item->setData(RolePath, path);
        const SessionImageId sid = (i < m_sessionIds.size()) ? m_sessionIds.at(i)
                                                            : kInvalidSessionImageId;
        item->setData(RoleSessionId, QVariant::fromValue(static_cast<qint64>(sid)));
        item->setData(ThumbnailDelegate::ThumbLoadedRole, false);
        QSize native;
        if (!m_cropToSquare && !path.isEmpty()) {
            // Process memo only on GUI — never Store I/O during bulk fill.
            native = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        }
        if (m_delegate && !m_cropToSquare && native.isValid()
            && native.width() > 0 && native.height() > 0) {
            applyLayoutAspect(item, i, native);
        } else if (m_delegate && !m_cropToSquare) {
            const QSize prov = m_delegate->letterboxContentSize(
                m_delegate->provisionalContentSize());
            item->setData(ThumbnailDelegate::ThumbContentSizeRole, prov);
            item->setSizeHint(provCell);
            // Size probes only when the row is on-screen (scheduleVisible).
        } else if (m_delegate) {
            item->setData(ThumbnailDelegate::ThumbContentSizeRole,
                          QSize(m_thumbSize, m_thumbSize));
            item->setSizeHint(provCell);
        } else {
            item->setSizeHint(provCell);
        }
    }
}

void ThumbnailBar::appendFileRowsChunk()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("ThumbnailBar::appendFileRowsChunk");
    if (m_fileFillNext < 0 || m_fileFillNext > m_files.size()) {
        return;
    }
    const quint64 gen = m_fileFillGeneration;
    const QSize provCell = (m_delegate && !m_cropToSquare)
        ? m_delegate->cellSizeForContent(font(), m_delegate->provisionalContentSize())
        : (m_delegate ? m_delegate->cellSize(font())
                      : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight()));

    // Full session — no hard row cap. Progressive chunks keep the GUI responsive;
    // thumb *pixels* stay viewport-virtualized (scheduleVisibleThumbnailLoads).
    // Letterbox variable widths need real items for correct scroll extent.
    constexpr int kChunk = 32;
    const int limit = m_files.size();
    const int begin = m_fileFillNext;
    const int end = qMin(begin + kChunk, limit);
    setUpdatesEnabled(false);
    materializeFileRows(begin, end, provCell);
    setUpdatesEnabled(true);
    m_fileFillNext = end;

    if (gen != m_fileFillGeneration) {
        return; // superseded by a newer setFiles
    }
    if (m_fileFillNext < limit) {
        // Yield paints/tiles — 0ms re-arm starved the GUI for a minute on large N.
        QTimer::singleShot(16, this, &ThumbnailBar::appendFileRowsChunk);
        return;
    }
    m_fileFillNext = -1;
    m_fileFillPriority = -1;

    if (m_multiSelect) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }
    if (count() > 0 && !m_multiSelect && currentRow() < 0) {
        setCurrentRow(0);
    }
    scheduleThumbnailLoads();
    updateCenteringMargins();
    QTimer::singleShot(0, this, [this]() {
        primeGeometryFromCache();
        scheduleVisibleThumbnailLoads();
    });
    QTimer::singleShot(100, this, [this]() {
        scheduleVisibleThumbnailLoads();
    });
}

void ThumbnailBar::ensureMaterializedThrough(int sessionIndex)
{
    GUI_BUDGET("ThumbnailBar::ensureMaterializedThrough");
    ASSERT_GUI_THREAD();
    if (m_files.isEmpty() || sessionIndex < 0) {
        return;
    }
    sessionIndex = qMin(sessionIndex, m_files.size() - 1);
    // Already past this index (fill idle with full count, or fill cursor ahead).
    if (count() > sessionIndex) {
        return;
    }
    if (m_fileFillNext < 0) {
        // Fill finished or never started but count is short — should not happen.
        return;
    }
    const quint64 gen = m_fileFillGeneration;
    const QSize provCell = (m_delegate && !m_cropToSquare)
        ? m_delegate->cellSizeForContent(font(), m_delegate->provisionalContentSize())
        : (m_delegate ? m_delegate->cellSize(font())
                      : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight()));
    // Catch up in bounded bursts so a jump to a high index does not freeze the GUI.
    constexpr int kBurst = 64;
    int guard = 0;
    setUpdatesEnabled(false);
    while (m_fileFillNext >= 0 && m_fileFillNext <= sessionIndex
           && m_fileFillNext < m_files.size() && guard++ < 512) {
        if (gen != m_fileFillGeneration) {
            setUpdatesEnabled(true);
            return;
        }
        const int begin = m_fileFillNext;
        const int end = qMin(begin + kBurst, m_files.size());
        materializeFileRows(begin, end, provCell);
        m_fileFillNext = end;
        if (m_fileFillNext >= m_files.size()) {
            m_fileFillNext = -1;
            m_fileFillPriority = -1;
            break;
        }
    }
    setUpdatesEnabled(true);
    updateCenteringMargins();
    if (m_fileFillNext >= 0 && m_fileFillNext < m_files.size()
        && gen == m_fileFillGeneration) {
        QTimer::singleShot(16, this, &ThumbnailBar::appendFileRowsChunk);
    } else if (m_fileFillNext < 0) {
        scheduleThumbnailLoads();
        QTimer::singleShot(0, this, [this]() {
            primeGeometryFromCache();
            scheduleVisibleThumbnailLoads();
        });
    }
}

void ThumbnailBar::setVisibleLoadsSuspended(bool on)
{
    if (m_visibleLoadsSuspended == on) {
        return;
    }
    m_visibleLoadsSuspended = on;
    if (!on) {
        scheduleVisibleThumbnailLoads();
    }
}

void ThumbnailBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_files.size()) {
        return;
    }
    // Progressive fill may not have reached this row yet — catch up (bounded).
    if (index >= count()) {
        m_fileFillPriority = index;
        ensureMaterializedThrough(index);
    }
    if (index < 0 || index >= count()) {
        return;
    }
    QListWidgetItem *it = item(index);
    if (!it) {
        return;
    }
    const bool blocked = blockSignals(true);
    setCurrentRow(index);
    blockSignals(blocked);
    // Only scroll when the row is outside the viewport — EnsureVisible on
    // every ←/→ re-layouts the strip and retriggers thumbnail loads.
    if (viewport()) {
        const QRect vr = viewport()->rect();
        const QRect ir = visualItemRect(it);
        if (!vr.intersects(ir.adjusted(-8, -8, 8, 8))) {
            scrollToItem(it, QAbstractItemView::EnsureVisible);
        }
    }
    if (!m_visibleLoadsSuspended) {
        scheduleVisibleThumbnailLoads();
    }
}

int ThumbnailBar::currentIndex() const
{
    return currentRow();
}

QList<SessionImageId> ThumbnailBar::selectedSessionIds() const
{
    QList<SessionImageId> out;
    const QList<QListWidgetItem *> sel = selectedItems();
    out.reserve(sel.size());
    for (QListWidgetItem *it : sel) {
        if (!it) {
            continue;
        }
        const int row = this->row(it);
        if (row < 0 || row >= m_sessionIds.size()) {
            continue;
        }
        const SessionImageId sid = m_sessionIds.at(row);
        if (sid != kInvalidSessionImageId) {
            out.append(sid);
        }
    }
    return out;
}

QList<int> ThumbnailBar::selectedSessionIndices() const
{
    QList<int> out;
    const QList<QListWidgetItem *> sel = selectedItems();
    out.reserve(sel.size());
    for (QListWidgetItem *it : sel) {
        if (!it) {
            continue;
        }
        const int row = this->row(it);
        if (row >= 0 && row < m_files.size()) {
            out.append(row);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
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
    if (!m_visibleLoadsSuspended) {
        scheduleVisibleThumbnailLoads();
    }
    // Gallery/Workspace: multi-select is applied on mouse release; row change is
    // not navigation.
    if (m_multiSelect) {
        return;
    }
    if (row < 0) {
        return;
    }
    // Image mode only below. Ctrl/Shift/Meta: temporary ExtendedSelection for
    // bulk session ops — update selection chrome, do not navigate the image.
    const Qt::KeyboardModifiers mods =
        QGuiApplication::keyboardModifiers()
        | (m_pressActive ? m_pressModifiers : Qt::KeyboardModifiers());
    if (mods & (Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier)) {
        emit workspaceSelectionChanged();
        return;
    }
    if (selectionMode() != QAbstractItemView::SingleSelection
        && selectedItems().size() > 1) {
        emit workspaceSelectionChanged();
        return;
    }
    // Plain click: navigate session cursor (MainWindow::setCurrentIndex).
    // Never indexActivated here — that re-enters Image mode and broke single-click.
    if (selectionMode() != QAbstractItemView::SingleSelection) {
        setSelectionMode(QAbstractItemView::SingleSelection);
    }
    emit indexNavigated(row);
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

    // Alt+Arrow: move selection one step along the strip (session reorder).
    const bool alt = event->modifiers() & Qt::AltModifier;
    if (alt && count() > 1) {
        const QList<int> rows = selectedIndices();
        if (!rows.isEmpty()) {
            const bool horizontal = (m_orientation == Qt::Horizontal);
            int delta = 0; // -1 = toward start, +1 = toward end
            if (horizontal) {
                if (event->key() == Qt::Key_Left) {
                    delta = -1;
                } else if (event->key() == Qt::Key_Right) {
                    delta = 1;
                }
            } else {
                if (event->key() == Qt::Key_Up) {
                    delta = -1;
                } else if (event->key() == Qt::Key_Down) {
                    delta = 1;
                }
            }
            if (delta != 0) {
                QList<int> moving = rows;
                std::sort(moving.begin(), moving.end());
                const int first = moving.first();
                const int last = moving.last();
                int insertBefore = -1;
                if (delta < 0) {
                    if (first > 0) {
                        insertBefore = first - 1;
                    }
                } else {
                    if (last + 1 < count()) {
                        insertBefore = last + 2; // after the next item
                    }
                }
                if (insertBefore >= 0) {
                    emit reorderRowsRequested(moving, insertBefore);
                    event->accept();
                    return;
                }
            }
        }
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

    QList<int> sortedSel = indices;
    std::sort(sortedSel.begin(), sortedSel.end());
    const int firstSel = sortedSel.isEmpty() ? -1 : sortedSel.first();
    const int lastSel = sortedSel.isEmpty() ? -1 : sortedSel.last();

    QAction *moveEarlierAct = menu.addAction(
        m_orientation == Qt::Horizontal ? tr("Move &Left") : tr("Move &Up"));
    moveEarlierAct->setShortcut(QKeySequence(Qt::ALT | (m_orientation == Qt::Horizontal
                                                           ? Qt::Key_Left
                                                           : Qt::Key_Up)));
    moveEarlierAct->setEnabled(firstSel > 0);

    QAction *moveLaterAct = menu.addAction(
        m_orientation == Qt::Horizontal ? tr("Move &Right") : tr("Move &Down"));
    moveLaterAct->setShortcut(QKeySequence(Qt::ALT | (m_orientation == Qt::Horizontal
                                                          ? Qt::Key_Right
                                                          : Qt::Key_Down)));
    moveLaterAct->setEnabled(lastSel >= 0 && lastSel + 1 < total);

    QAction *moveStartAct = menu.addAction(tr("Move to &Start"));
    moveStartAct->setEnabled(firstSel > 0);

    QAction *moveEndAct = menu.addAction(tr("Move to &End"));
    moveEndAct->setEnabled(lastSel >= 0 && lastSel + 1 < total);

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
    } else if (chosen == moveEarlierAct && firstSel > 0) {
        emit reorderRowsRequested(sortedSel, firstSel - 1);
    } else if (chosen == moveLaterAct && lastSel >= 0 && lastSel + 1 < total) {
        emit reorderRowsRequested(sortedSel, lastSel + 2);
    } else if (chosen == moveStartAct && firstSel > 0) {
        emit reorderRowsRequested(sortedSel, 0);
    } else if (chosen == moveEndAct && lastSel >= 0 && lastSel + 1 < total) {
        emit reorderRowsRequested(sortedSel, total);
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
            QStringLiteral("application/x-biltoo-session-ids"),
            QStringLiteral("application/x-biltoo-session-rows")};
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
    // Row indices for filmstrip-internal reorder (stable order of @p items).
    QByteArray rowPayload;
    for (QListWidgetItem *it : items) {
        if (!it) {
            continue;
        }
        const int r = row(it);
        if (r < 0) {
            continue;
        }
        if (!rowPayload.isEmpty()) {
            rowPayload.append(',');
        }
        rowPayload.append(QByteArray::number(r));
    }
    if (!rowPayload.isEmpty()) {
        mime->setData(QStringLiteral("application/x-biltoo-session-rows"), rowPayload);
    }
    return mime;
}

Qt::DropActions ThumbnailBar::supportedDragActions() const
{
    return Qt::CopyAction | Qt::LinkAction | Qt::MoveAction;
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
            // Letterbox mode sets iconSize to 1×1 so layout hugs content — that
            // must not drive the drag preview (scaled to 1px = invisible ghost).
            // Prefer resolvedThumbPixmap (session override / path thumb), then
            // scale to a visible preview edge (~thumbSize, min 64).
            if (const int row = this->row(first); row >= 0) {
                const QPixmap resolved = resolvedThumbPixmap(row);
                if (!resolved.isNull()) {
                    pix = resolved;
                }
            }
            const int edge = qMax(64, m_thumbSize);
            if (qMax(pix.width(), pix.height()) > edge) {
                pix = pix.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            drag->setPixmap(pix);
            drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
        }
    }

    drag->exec(supportedDragActions(), Qt::CopyAction);
}

QPoint ThumbnailBar::dropPosInViewport(const QPoint &widgetPos) const
{
    if (!viewport()) {
        return widgetPos;
    }
    // Drag events are delivered to the view; visualItemRect is viewport-local.
    return viewport()->mapFrom(this, widgetPos);
}

int ThumbnailBar::insertIndexAt(const QPoint &pos) const
{
    if (count() <= 0) {
        return 0;
    }
    const bool horizontal = (m_orientation == Qt::Horizontal);
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (!it) {
            continue;
        }
        const QRect r = visualItemRect(it);
        if (horizontal) {
            if (pos.x() < r.center().x()) {
                return i;
            }
        } else {
            if (pos.y() < r.center().y()) {
                return i;
            }
        }
    }
    return count();
}

void ThumbnailBar::setDropInsertIndex(int index)
{
    if (m_dropInsertIndex == index) {
        return;
    }
    m_dropInsertIndex = index;
    viewport()->update();
}

void ThumbnailBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()
        && event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-session-rows"))
        && event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        setDropInsertIndex(insertIndexAt(dropPosInViewport(event->position().toPoint())));
        return;
    }
    event->ignore();
}

void ThumbnailBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()
        && event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-session-rows"))
        && event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        setDropInsertIndex(insertIndexAt(dropPosInViewport(event->position().toPoint())));
        return;
    }
    event->ignore();
}

void ThumbnailBar::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    setDropInsertIndex(-1);
}

void ThumbnailBar::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()
        || !event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-session-rows"))
        || event->source() != this) {
        event->ignore();
        setDropInsertIndex(-1);
        return;
    }
    const QByteArray raw =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-session-rows"));
    QList<int> rows;
    for (const QByteArray &part : raw.split(',')) {
        bool ok = false;
        const int r = part.trimmed().toInt(&ok);
        if (ok && r >= 0) {
            rows.append(r);
        }
    }
    const int insertBefore = insertIndexAt(dropPosInViewport(event->position().toPoint()));
    setDropInsertIndex(-1);
    if (!rows.isEmpty()) {
        emit reorderRowsRequested(rows, insertBefore);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    event->ignore();
}

void ThumbnailBar::paintEvent(QPaintEvent *event)
{
    QListWidget::paintEvent(event);
    if (m_dropInsertIndex < 0 || count() <= 0) {
        return;
    }
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor line = palette().color(QPalette::Highlight);
    QPen pen(line, 2);
    painter.setPen(pen);

    const bool horizontal = (m_orientation == Qt::Horizontal);
    const int idx = qBound(0, m_dropInsertIndex, count());
    QRect guide;
    if (idx < count()) {
        const QRect r = visualItemRect(item(idx));
        if (horizontal) {
            guide = QRect(r.left() - 1, r.top(), 2, r.height());
        } else {
            guide = QRect(r.left(), r.top() - 1, r.width(), 2);
        }
    } else {
        const QRect r = visualItemRect(item(count() - 1));
        if (horizontal) {
            guide = QRect(r.right() - 1, r.top(), 2, r.height());
        } else {
            guide = QRect(r.left(), r.bottom() - 1, r.width(), 2);
        }
    }
    painter.fillRect(guide, line);
}

void ThumbnailBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_middleScrollActive = true;
        m_middleScrollPos = event->pos();
        m_pressActive = false;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        m_pressActive = false;
        m_middleScrollActive = false;
        QListWidget::mousePressEvent(event);
        return;
    }

    QListWidgetItem *hit = itemAt(event->pos());
    m_pressPos = event->pos();
    m_pressItem = hit;
    m_pressActive = true;
    m_dragStarted = false;
    m_pressModifiers = event->modifiers();
    // Snapshot selection at press: multi-select is applied on release, and
    // selection can change under us during the drag gesture. Use this list for
    // the session-row mime payload so multi-reorder does not shrink to one row.
    m_pressSelectedRows = selectedIndices();

    // --- Mode policy ---------------------------------------------------------
    // Image (m_multiSelect false): single-click navigates; Ctrl/Shift only for
    // temporary multi-select (bulk remove etc.). Gallery + Workspace
    // (m_multiSelect true): normal multi-select on release; drag starts on move.
    // -------------------------------------------------------------------------
    if (!m_multiSelect) {
        const bool multiMods = event->modifiers()
                               & (Qt::ControlModifier | Qt::ShiftModifier
                                  | Qt::MetaModifier);
        if (hit && multiMods) {
            if (selectionMode() == QAbstractItemView::SingleSelection) {
                setSelectionMode(QAbstractItemView::ExtendedSelection);
                setSelectionRectVisible(false);
            }
            QListWidget::mousePressEvent(event);
            event->accept();
            return;
        }
        // Plain click: force SingleSelection so a prior Ctrl gesture cannot leave
        // ExtendedSelection stuck (currentRowChanged then skipped navigation).
        if (selectionMode() != QAbstractItemView::SingleSelection) {
            setSelectionMode(QAbstractItemView::SingleSelection);
            setSelectionRectVisible(false);
        }
        QListWidget::mousePressEvent(event);
        return;
    }

    // Gallery / Workspace: apply selection on release if the gesture is not a
    // drag. Canvas membership is drag-drop only — never toggle on press.
    event->accept();
}

void ThumbnailBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_middleScrollActive && (event->buttons() & Qt::MiddleButton)) {
        const QPoint delta = event->pos() - m_middleScrollPos;
        m_middleScrollPos = event->pos();
        if (QScrollBar *h = horizontalScrollBar()) {
            h->setValue(h->value() - delta.x());
        }
        if (QScrollBar *v = verticalScrollBar()) {
            v->setValue(v->value() - delta.y());
        }
        event->accept();
        return;
    }
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
            // Multi-reorder payload: prefer rows selected at press when the
            // press is on one of them. Fall back to live selection, then press.
            // Full-strip selection without Ctrl/Shift is treated as accidental
            // (Workspace mirror / Select-All residue) → drag only the press row.
            QList<QListWidgetItem *> items;
            QList<int> rows = m_pressSelectedRows;
            if (rows.isEmpty()) {
                rows = selectedIndices();
            }
            const int pressRow = m_pressItem ? row(m_pressItem) : -1;
            const bool pressInSel = pressRow >= 0 && rows.contains(pressRow);
            if (pressInSel && rows.size() > 1) {
                if (rows.size() == count() && count() > 1
                    && !(m_pressModifiers & (Qt::ControlModifier | Qt::ShiftModifier
                                            | Qt::MetaModifier))) {
                    rows = {pressRow};
                }
                std::sort(rows.begin(), rows.end());
                for (int r : rows) {
                    if (QListWidgetItem *it = item(r)) {
                        items.append(it);
                    }
                }
            }
            if (items.isEmpty() && m_pressItem) {
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
    if (event->button() == Qt::MiddleButton && m_middleScrollActive) {
        m_middleScrollActive = false;
        unsetCursor();
        event->accept();
        return;
    }
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
    // Open in Image mode — never toggle Workspace canvas membership.
    // Place on Workspace via drag-drop only.
    emit indexActivated(row(hit));
    event->accept();
}
