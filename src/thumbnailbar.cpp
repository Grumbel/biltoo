// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"

#include <QFileInfo>
#include <QImageReader>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QMetaObject>
#include <QResizeEvent>
#include <QThreadPool>
#include <algorithm>

namespace {
constexpr int kLabelPad = 28; // text + vertical margins around the icon
constexpr int kGridPadX = 12;
} // namespace

ThumbnailBar::ThumbnailBar(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListWidget::IconMode);
    setFlow(QListWidget::LeftToRight);
    setWrapping(false);
    setResizeMode(QListWidget::Adjust);
    setMovement(QListWidget::Static);
    setSpacing(4);
    setUniformItemSizes(true);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::NoFocus);

    qRegisterMetaType<QImage>("QImage");

    applyThumbMetrics();

    connect(this, &QListWidget::itemActivated, this, &ThumbnailBar::onItemActivated);
    connect(this, &QListWidget::currentRowChanged, this, &ThumbnailBar::onCurrentRowChanged);
}

ThumbnailBar::~ThumbnailBar()
{
    cancelPendingLoads();
}

int ThumbnailBar::heightForThumbSize(int thumbSize)
{
    return thumbSize + kLabelPad;
}

int ThumbnailBar::thumbSizeForHeight(int height)
{
    const int size = height - kLabelPad;
    return qBound(kMinThumbSize, size, kMaxThumbSize);
}

void ThumbnailBar::applyThumbMetrics()
{
    setIconSize(QSize(m_thumbSize, m_thumbSize));
    setGridSize(QSize(m_thumbSize + kGridPadX, m_thumbSize + kLabelPad));
    const int barH = heightForThumbSize(m_thumbSize);
    setMinimumHeight(heightForThumbSize(kMinThumbSize));
    setMaximumHeight(heightForThumbSize(kMaxThumbSize));
    // Prefer current size; sizeHint reports barH
    Q_UNUSED(barH);

    const QSize hint(m_thumbSize + kGridPadX, m_thumbSize + kLabelPad - 4);
    for (int i = 0; i < count(); ++i) {
        if (QListWidgetItem *it = item(i)) {
            it->setSizeHint(hint);
        }
    }
}

QSize ThumbnailBar::sizeHint() const
{
    return QSize(400, heightForThumbSize(m_thumbSize));
}

QSize ThumbnailBar::minimumSizeHint() const
{
    return QSize(200, heightForThumbSize(kMinThumbSize));
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
    // Follow the bar height so a splitter drag scales the icons
    const int fitted = thumbSizeForHeight(event->size().height());
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
    QImageReader reader(path);
    reader.setAutoTransform(true);

    QSize size = reader.size();
    if (size.isValid()) {
        size.scale(maxSize, maxSize, Qt::KeepAspectRatio);
        reader.setScaledSize(size);
    }

    return reader.read();
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

    const QSize hint(m_thumbSize + kGridPadX, m_thumbSize + kLabelPad - 4);
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
