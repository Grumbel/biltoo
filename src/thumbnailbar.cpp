// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"
#include "archivepath.h"
#include "imageloader.h"

#include <QAction>
#include <QApplication>
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
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyle>
#include <QThreadPool>
#include <QUrl>
#include <QVariant>
#include <algorithm>

// ---------------------------------------------------------------------------
// Layout model (single source of truth)
//
//   cell width  = thumbSize + 2 * kCellPadX
//   cell height = thumbSize + labelBand
//   labelBand   = QFontMetrics::height() + kLabelGap   (no style padding)
//
// Horizontal bar: thin axis is HEIGHT = cell height
// Vertical bar:   thin axis is WIDTH  = cell width  (label sits under icon,
//                 so it consumes vertical space inside each item, not bar width)
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
    const int labelH = labelBandHeight(font);
    return QSize(m_thumbSize + 2 * kCellPadX, m_thumbSize + labelH);
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    Q_UNUSED(index);
    return cellSize(option.font);
}

void ThumbnailDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect cell = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    if (selected) {
        painter->fillRect(cell, option.palette.brush(QPalette::Highlight));
    } else if (hovered) {
        QColor c = option.palette.color(QPalette::Highlight);
        c.setAlpha(48);
        painter->fillRect(cell, c);
    }

    const QFontMetrics fm(option.font);
    const int labelBand = labelBandHeight(option.font);
    // Icon must leave room for the caption inside the allocated cell
    const int iconSide = qBound(1, qMin(m_thumbSize, cell.height() - labelBand), cell.width());
    const int iconX = cell.left() + (cell.width() - iconSide) / 2;
    const int iconY = cell.top();

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        // Always Normal — QIcon::Selected is theme-dependent and can hide the pixmap
        icon.paint(painter, QRect(iconX, iconY, iconSide, iconSide), Qt::AlignCenter,
                   QIcon::Normal, selected ? QIcon::On : QIcon::Off);
    }

    const QString text = index.data(Qt::DisplayRole).toString();
    if (m_labelsVisible && !text.isEmpty() && labelBand > 0 && cell.height() > iconSide) {
        const QRect textRect(cell.left() + kCellPadX,
                             iconY + iconSide + kLabelGap,
                             qMax(1, cell.width() - 2 * kCellPadX),
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
    if (bar && bar->isOnCanvas(index.row()) && iconSide > 8) {
            const int fold = qBound(10, iconSide / 4, 28);
            const QPoint topRight(iconX + iconSide, iconY);
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
    setSpacing(2);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::ClickFocus);
    setWordWrap(false);
    setTextElideMode(Qt::ElideMiddle);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);

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
}

ThumbnailBar::~ThumbnailBar()
{
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
    // Horizontal-bar height ≈ thumb + label; used as a generic default.
    return thumbSize + ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
}

int ThumbnailBar::thumbSizeForExtent(int extent)
{
    const int label = ThumbnailDelegate::labelBandHeightForFont(QApplication::font());
    return qBound(kMinThumbSize, extent - label, kMaxThumbSize);
}

int ThumbnailBar::thumbSizeFromBarExtent(int extent) const
{
    if (m_orientation == Qt::Horizontal) {
        // extent is bar height = cell height = thumb + labelBand
        return qBound(kMinThumbSize, extent - labelBandHeight(), kMaxThumbSize);
    }
    // Vertical bar: extent is bar width ≈ cell width = thumb + 2*pad
    return qBound(kMinThumbSize,
                 extent - 2 * ThumbnailDelegate::kCellPadX,
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
    setIconSize(QSize(m_thumbSize, m_thumbSize));
    if (m_delegate) {
        m_delegate->setThumbSize(m_thumbSize);
    }

    const QSize cell = m_delegate ? m_delegate->cellSize(font())
                                  : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight());
    // gridSize is the definitive cell size for IconMode
    setGridSize(cell);

    const int label = labelBandHeight();
    if (m_orientation == Qt::Horizontal) {
        // Thin axis = height = thumb + label
        setMinimumHeight(kMinThumbSize + label);
        setMaximumHeight(kMaxThumbSize + label);
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        // Thin axis = width = thumb + horizontal pad (label is under the icon)
        setMinimumWidth(kMinThumbSize + 2 * ThumbnailDelegate::kCellPadX);
        setMaximumWidth(kMaxThumbSize + 2 * ThumbnailDelegate::kCellPadX);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }

    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSizeHint(cell);
        }
    }
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
        return QSize(200, kMinThumbSize + labelBandHeight());
    }
    return QSize(kMinThumbSize + 2 * ThumbnailDelegate::kCellPadX, 200);
}

void ThumbnailBar::setThumbSize(int pixels)
{
    const int clamped = qBound(kMinThumbSize, pixels, kMaxThumbSize);
    if (clamped == m_thumbSize) {
        return;
    }
    m_thumbSize = clamped;
    applyThumbMetrics();

    if (m_thumbSize > m_decodedSize && !m_files.isEmpty()) {
        scheduleThumbnailLoads();
    }
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

void ThumbnailBar::cancelPendingLoads()
{
    ++m_generation;
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

void ThumbnailBar::setThumbnailIcon(int row, const QImage &image)
{
    if (row < 0 || row >= count() || image.isNull()) {
        return;
    }
    if (QListWidgetItem *it = item(row)) {
        it->setIcon(QIcon(QPixmap::fromImage(image)));
    }
}

QImage ThumbnailBar::prepareThumbnailFromImage(const QImage &image, int maxSize) const
{
    if (image.isNull() || maxSize < 1) {
        return QImage();
    }
    if (m_cropToSquare) {
        // Center-crop to square so the cell is filled edge-to-edge.
        const int side = qMin(image.width(), image.height());
        if (side <= 0) {
            return QImage();
        }
        const int x = (image.width() - side) / 2;
        const int y = (image.height() - side) / 2;
        QImage square = image.copy(x, y, side, side);
        if (square.width() != maxSize || square.height() != maxSize) {
            square = square.scaled(maxSize, maxSize, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
        }
        return square;
    }
    // Fit full image into the square cell (letterbox) so aspect ratio is visible.
    QImage fitted = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    if (fitted.isNull()) {
        return QImage();
    }
    QImage cell(maxSize, maxSize, QImage::Format_ARGB32_Premultiplied);
    cell.fill(QColor(40, 40, 40));
    QPainter painter(&cell);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const int ox = (maxSize - fitted.width()) / 2;
    const int oy = (maxSize - fitted.height()) / 2;
    painter.drawImage(ox, oy, fitted);
    painter.end();
    return cell;
}

void ThumbnailBar::setCropToSquare(bool on)
{
    if (m_cropToSquare == on) {
        return;
    }
    m_cropToSquare = on;
    ++m_generation;
    scheduleThumbnailLoads();
}

QImage ThumbnailBar::makeThumbnail(const QString &path, int maxSize) const
{
    QImage image = ImageLoader::loadThumbnail(path, maxSize);
    if (image.isNull()) {
        return image;
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
    const QImage thumb = prepareThumbnailFromImage(image, m_thumbSize);
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
        const QImage thumb = prepareThumbnailFromImage(it.value(), m_thumbSize);
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
    const QImage thumb = prepareThumbnailFromImage(image, m_thumbSize);
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


void ThumbnailBar::scheduleThumbnailLoads()
{
    const quint64 gen = m_generation.load();
    const int decodeSize = m_thumbSize;
    m_decodedSize = decodeSize;

    for (int i = 0; i < m_files.size(); ++i) {
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
        // QPointer: bar may be destroyed while pool jobs still run.
        const QPointer<ThumbnailBar> guard(this);
        QThreadPool::globalInstance()->start([guard, i, path, gen, decodeSize]() {
            ThumbnailBar *bar = guard.data();
            if (!bar || gen != bar->m_generation.load()) {
                return;
            }
            const QImage image = bar->makeThumbnail(path, decodeSize);
            bar = guard.data();
            if (!bar || image.isNull() || gen != bar->m_generation.load()) {
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
            QMetaObject::invokeMethod(bar, "setThumbnailIcon", Qt::QueuedConnection,
                                      Q_ARG(int, i),
                                      Q_ARG(QImage, image));
        });
    }
}

void ThumbnailBar::clearPressState()
{
    m_pressActive = false;
    m_pressItem = nullptr;
    m_dragStarted = false;
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

    const QSize cell = m_delegate ? m_delegate->cellSize(font())
                                  : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight());
    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files.at(i);
        auto *item = new QListWidgetItem(this);
        item->setText(ArchivePath::displayName(path));
        item->setToolTip(path);
        item->setData(RolePath, path);
        const SessionImageId sid = (i < m_sessionIds.size()) ? m_sessionIds.at(i)
                                                            : kInvalidSessionImageId;
        item->setData(RoleSessionId, QVariant::fromValue(static_cast<qint64>(sid)));
        item->setSizeHint(cell);
        item->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    }

    if (m_multiSelect) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }

    if (count() > 0 && !m_multiSelect) {
        setCurrentRow(0);
    }

    scheduleThumbnailLoads();
    updateCenteringMargins();
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
    return {QStringLiteral("text/uri-list")};
}

QMimeData *ThumbnailBar::mimeData(const QList<QListWidgetItem *> items) const
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QByteArray idPayload;
    urls.reserve(items.size());
    for (QListWidgetItem *it : items) {
        if (!it) {
            continue;
        }
        const QString path = it->data(RolePath).toString();
        if (path.isEmpty()) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(path));
        // Prefer SessionImageId stamped on the item (setFiles / setSessionIds).
        SessionImageId sid = kInvalidSessionImageId;
        const QVariant idVar = it->data(RoleSessionId);
        if (idVar.isValid()) {
            sid = static_cast<SessionImageId>(idVar.toLongLong());
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
    mime->setUrls(urls);
    if (!idPayload.isEmpty()) {
        mime->setData(QStringLiteral("application/x-qimgview-session-ids"), idPayload);
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
    if (!mime || mime->urls().isEmpty()) {
        delete mime;
        return;
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    if (QListWidgetItem *first = items.first()) {
        const QIcon icon = first->icon();
        if (!icon.isNull()) {
            const QPixmap pix = icon.pixmap(iconSize());
            if (!pix.isNull()) {
                drag->setPixmap(pix);
                drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
            }
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
    // toggle, double-click membership, or drag onto the canvas).
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
    // the gesture is not a drag). Canvas membership is double-click / drop.
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
            QList<QListWidgetItem *> items;
            if (m_multiSelect) {
                items = selectedItems();
                if (!m_pressItem->isSelected()) {
                    items = {m_pressItem};
                }
            } else {
                items = {m_pressItem};
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
    const int r = row(hit);
    if (m_multiSelect) {
        // Toggle canvas membership for this session image.
        emit canvasMembershipToggled(r);
        event->accept();
        return;
    }
    // Image mode: activate (navigate) — same as single click path.
    emit indexActivated(r);
    event->accept();
}
