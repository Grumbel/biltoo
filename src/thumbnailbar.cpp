// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"
#include "imageloader.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
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
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QThreadPool>
#include <QUrl>
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

QImage ThumbnailBar::makeThumbnail(const QString &path, int maxSize)
{
    QImage image = ImageLoader::loadThumbnail(path, maxSize);
    if (image.isNull()) {
        return image;
    }
    // Center-crop to square so the cell is filled edge-to-edge
    const int side = qMin(image.width(), image.height());
    if (side <= 0) {
        return image;
    }
    const int x = (image.width() - side) / 2;
    const int y = (image.height() - side) / 2;
    image = image.copy(x, y, side, side);
    if (image.width() != maxSize || image.height() != maxSize) {
        image = image.scaled(maxSize, maxSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void ThumbnailBar::scheduleThumbnailLoads()
{
    const quint64 gen = m_generation.load();
    const int decodeSize = m_thumbSize;
    m_decodedSize = decodeSize;

    for (int i = 0; i < m_files.size(); ++i) {
        const QString path = m_files.at(i);
        QThreadPool::globalInstance()->start([this, i, path, gen, decodeSize]() {
            if (gen != m_generation.load()) {
                return;
            }
            const QImage image = makeThumbnail(path, decodeSize);
            if (image.isNull() || gen != m_generation.load()) {
                return;
            }
            QMetaObject::invokeMethod(this, "setThumbnailIcon", Qt::QueuedConnection,
                                      Q_ARG(int, i),
                                      Q_ARG(QImage, image));
        });
    }
}

void ThumbnailBar::setFiles(const QStringList &files)
{
    cancelPendingLoads();
    clear();
    m_files = files;

    const QSize cell = m_delegate ? m_delegate->cellSize(font())
                                  : QSize(m_thumbSize + 4, m_thumbSize + labelBandHeight());
    for (const QString &path : files) {
        auto *item = new QListWidgetItem(this);
        item->setText(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(cell);
        item->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    }

    if (m_workspaceMode) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }

    if (count() > 0 && !m_workspaceMode) {
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

void ThumbnailBar::setWorkspaceMode(bool on)
{
    if (m_workspaceMode == on) {
        return;
    }
    m_workspaceMode = on;

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
    if (m_workspaceMode) {
        return;
    }
    if (item) {
        emit indexActivated(row(item));
    }
}

void ThumbnailBar::onCurrentRowChanged(int row)
{
    if (m_workspaceMode) {
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
    // for navigation — Select All there is still useful before bulk remove.
    const bool wasSingle = (!m_workspaceMode
                            && selectionMode() == QAbstractItemView::SingleSelection);
    if (wasSingle) {
        setSelectionMode(QAbstractItemView::MultiSelection);
        setSelectionRectVisible(false);
    }
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSelected(true);
        }
    }
    m_selectionAnchor = 0;
    if (m_workspaceMode) {
        emit workspaceSelectionChanged();
    }
}

void ThumbnailBar::selectNoneThumbs()
{
    clearSelection();
    m_selectionAnchor = -1;
    if (!m_workspaceMode) {
        setSelectionMode(QAbstractItemView::SingleSelection);
    }
    if (m_workspaceMode) {
        emit workspaceSelectionChanged();
    }
}

void ThumbnailBar::invertThumbSelection()
{
    if (count() == 0) {
        return;
    }
    const bool wasSingle = (!m_workspaceMode
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
    if (m_workspaceMode) {
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
    if (hit) {
        if (m_workspaceMode) {
            if (!hit->isSelected()) {
                hit->setSelected(true);
            }
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
                    paths.append(it->data(Qt::UserRole).toString());
                }
            }
        } else if (hit) {
            paths.append(hit->data(Qt::UserRole).toString());
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
    auto *data = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(items.size());
    for (QListWidgetItem *it : items) {
        if (!it) {
            continue;
        }
        const QString path = it->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    data->setUrls(urls);
    return data;
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
    QMimeData *data = mimeData(items);
    if (!data || data->urls().isEmpty()) {
        delete data;
        return;
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(data);

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

    // Image mode: Ctrl/Shift+click adds that file onto the workspace
    if (!m_workspaceMode) {
        if (hit && (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            emit indexAddToWorkspace(row(hit));
            event->accept();
            return;
        }
        // Single click navigates the session (list selection)
        QListWidget::mousePressEvent(event);
        return;
    }

    // Workspace mode: no rubber-band. Selection drives which images are on the canvas.
    //  - Click: toggle that thumbnail
    //  - Ctrl+click: toggle without moving the range anchor
    //  - Shift+click: select inclusive range from anchor
    if (!hit) {
        event->accept();
        return;
    }

    const int r = row(hit);
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;

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
    } else {
        hit->setSelected(!hit->isSelected());
        m_selectionAnchor = r;
    }

    emit workspaceSelectionChanged();
    event->accept();
}

void ThumbnailBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressActive && !m_dragStarted && (event->buttons() & Qt::LeftButton)
        && m_pressItem) {
        const int dist = (event->pos() - m_pressPos).manhattanLength();
        if (dist >= QApplication::startDragDistance()) {
            // Image mode: drag files out. Workspace: only with Alt (no rubber-band).
            const bool fileDrag = !m_workspaceMode
                || (event->modifiers() & Qt::AltModifier);
            if (fileDrag) {
                m_dragStarted = true;
                QList<QListWidgetItem *> items;
                if (m_workspaceMode) {
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
                m_pressActive = false;
                m_pressItem = nullptr;
                event->accept();
                return;
            }
            // Workspace without Alt: ignore drag distance (no rubber-band)
            event->accept();
            return;
        }
    }
    if (!m_workspaceMode) {
        QListWidget::mouseMoveEvent(event);
    } else {
        event->accept();
    }
}

void ThumbnailBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressActive = false;
    m_pressItem = nullptr;
    m_dragStarted = false;
    if (!m_workspaceMode) {
        QListWidget::mouseReleaseEvent(event);
    } else {
        event->accept();
    }
}
