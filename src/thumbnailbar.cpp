// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"
#include "imageloader.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QMetaObject>
#include <QResizeEvent>
#include <QThreadPool>
#include <algorithm>

namespace {
// Caption band under the icon: font height + minimal gap (no extra style padding).
constexpr int kLabelGap = 2;
constexpr int kGridPadX = 4;
} // namespace

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
    // Drop style-provided item margins that inflate the caption band
    setStyleSheet(QStringLiteral(
        "QListWidget::item { padding: 0px; margin: 0px; border: none; }"));

    // Compact caption under the icon
    QFont captionFont = font();
    if (captionFont.pointSizeF() > 0) {
        captionFont.setPointSizeF(qMax(7.0, captionFont.pointSizeF() - 2.0));
    } else if (captionFont.pixelSize() > 0) {
        captionFont.setPixelSize(qMax(9, captionFont.pixelSize() - 3));
    }
    setFont(captionFont);

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

int ThumbnailBar::extentForThumbSize(int thumbSize)
{
    // Approximate single-line caption height when no widget font is available
    return thumbSize + 12 + kLabelGap;
}

int ThumbnailBar::thumbSizeForExtent(int extent)
{
    const int size = extent - (12 + kLabelGap);
    return qBound(kMinThumbSize, size, kMaxThumbSize);
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

    const int labelH = QFontMetrics(font()).height() + kLabelGap;
    const int cellW = m_thumbSize + kGridPadX;
    const int cellH = m_thumbSize + labelH;
    setGridSize(QSize(cellW, cellH));

    const int minExtent = kMinThumbSize + labelH;
    const int maxExtent = kMaxThumbSize + labelH;

    if (m_orientation == Qt::Horizontal) {
        setMinimumHeight(minExtent);
        setMaximumHeight(maxExtent);
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        setMinimumWidth(minExtent);
        setMaximumWidth(maxExtent);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }

    const QSize hint(cellW, cellH);
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSizeHint(hint);
        }
    }
}

QSize ThumbnailBar::sizeHint() const
{
    const int labelH = QFontMetrics(font()).height() + kLabelGap;
    const int extent = m_thumbSize + labelH;
    if (m_orientation == Qt::Horizontal) {
        return QSize(400, extent);
    }
    return QSize(extent, 400);
}

QSize ThumbnailBar::minimumSizeHint() const
{
    const int labelH = QFontMetrics(font()).height() + kLabelGap;
    const int extent = kMinThumbSize + labelH;
    if (m_orientation == Qt::Horizontal) {
        return QSize(200, extent);
    }
    return QSize(extent, 200);
}

void ThumbnailBar::setThumbSize(int pixels)
{
    const int clamped = qBound(kMinThumbSize, pixels, kMaxThumbSize);
    if (clamped == m_thumbSize) {
        return;
    }
    m_thumbSize = clamped;
    applyThumbMetrics();

    // Reload at higher resolution when the user grows the bar past what we decoded
    if (m_thumbSize > m_decodedSize && !m_files.isEmpty()) {
        scheduleThumbnailLoads();
    }
}

void ThumbnailBar::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    // Follow the thin axis so a splitter drag scales the icons
    const int extent = (m_orientation == Qt::Horizontal)
                           ? event->size().height()
                           : event->size().width();
    const int fitted = thumbSizeForExtent(extent);
    if (fitted != m_thumbSize) {
        setThumbSize(fitted);
    }
}

void ThumbnailBar::cancelPendingLoads()
{
    // Bump generation so in-flight jobs become no-ops when they finish
    m_generation.fetch_add(1);
}

QImage ThumbnailBar::makeThumbnail(const QString &path, int maxSize)
{
    return ImageLoader::loadThumbnail(path, maxSize);
}

void ThumbnailBar::scheduleThumbnailLoads()
{
    cancelPendingLoads();
    const quint64 gen = m_generation.load();
    const int decodeSize = m_thumbSize;
    m_decodedSize = decodeSize;

    for (int i = 0; i < m_files.size(); ++i) {
        const QString path = m_files.at(i);
        QThreadPool::globalInstance()->start([this, i, path, gen, decodeSize]() {
            if (gen != m_generation.load()) {
                return; // superseded
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

    const int labelH = QFontMetrics(font()).height() + kLabelGap;
    const QSize hint(m_thumbSize + kGridPadX, m_thumbSize + labelH);
    for (const QString &path : files) {
        auto *item = new QListWidgetItem(this);
        item->setText(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(hint);
        item->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    }

    if (!files.isEmpty()) {
        scheduleThumbnailLoads();
    } else {
        m_decodedSize = 0;
    }

    if (count() > 0 && !m_workspaceMode) {
        setCurrentRow(0);
    }
}

void ThumbnailBar::setThumbnailIcon(int row, const QImage &image)
{
    QListWidgetItem *item = this->item(row);
    if (!item || image.isNull()) {
        return;
    }
    item->setIcon(QIcon(QPixmap::fromImage(image)));
}

void ThumbnailBar::setCurrentIndex(int index)
{
    if (index >= 0 && index < count()) {
        // Block signals so programmatic selection does not re-emit indexActivated
        const bool blocked = blockSignals(true);
        setCurrentRow(index);
        blockSignals(blocked);
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
        setSelectionMode(QAbstractItemView::MultiSelection);
        clearSelection();
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
        const int row = currentRow();
        if (row >= 0) {
            indices.append(row);
        }
    }
    if (!indices.isEmpty()) {
        emit removeIndicesRequested(indices);
    }
}

void ThumbnailBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        requestRemoveSelection();
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
            // Ensure the item under the cursor is part of the selection
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

    QMenu menu(this);
    QAction *removeAct = menu.addAction(tr("Remove from Session"));
    removeAct->setShortcut(QKeySequence::Delete);
    removeAct->setEnabled(!indices.isEmpty());
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == removeAct && !indices.isEmpty()) {
        emit removeIndicesRequested(indices);
    }
    event->accept();
}

void ThumbnailBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QListWidget::mousePressEvent(event);
        return;
    }

    if (m_workspaceMode) {
        // Plain click toggles workspace membership for that thumbnail
        QListWidgetItem *hit = itemAt(event->pos());
        if (hit) {
            hit->setSelected(!hit->isSelected());
            emit workspaceSelectionChanged();
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    // Classic mode: Ctrl/Shift+click adds to workspace without changing current
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        QListWidgetItem *hit = itemAt(event->pos());
        if (hit) {
            emit indexAddToWorkspace(row(hit));
            event->accept();
            return;
        }
    }
    QListWidget::mousePressEvent(event);
}
