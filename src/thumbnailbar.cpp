// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"

#include <QFileInfo>
#include <QImageReader>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QMetaObject>
#include <QThreadPool>
#include <algorithm>

ThumbnailBar::ThumbnailBar(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListWidget::IconMode);
    setFlow(QListWidget::LeftToRight);
    setWrapping(false);
    setResizeMode(QListWidget::Adjust);
    setMovement(QListWidget::Static);
    setIconSize(QSize(kThumbSize, kThumbSize));
    setGridSize(QSize(kThumbSize + 12, kThumbSize + 28));
    setSpacing(4);
    setUniformItemSizes(true);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::NoFocus);

    qRegisterMetaType<QImage>("QImage");

    setMaximumHeight(kBarHeight);
    setMinimumHeight(kBarHeight);

    connect(this, &QListWidget::itemActivated, this, &ThumbnailBar::onItemActivated);
    connect(this, &QListWidget::currentRowChanged, this, &ThumbnailBar::onCurrentRowChanged);
}

ThumbnailBar::~ThumbnailBar()
{
    cancelPendingLoads();
}

QSize ThumbnailBar::sizeHint() const
{
    return QSize(400, kBarHeight);
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

void ThumbnailBar::setFiles(const QStringList &files)
{
    cancelPendingLoads();
    clear();

    const quint64 gen = m_generation.load();

    for (const QString &path : files) {
        auto *item = new QListWidgetItem(this);
        item->setText(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(kThumbSize + 12, kThumbSize + 24));
        item->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    }

    // Schedule thumbnail decoding on the global thread pool
    for (int i = 0; i < files.size(); ++i) {
        const QString path = files.at(i);
        QThreadPool::globalInstance()->start([this, i, path, gen]() {
            if (gen != m_generation.load()) {
                return; // superseded
            }
            const QImage image = makeThumbnail(path, kThumbSize);
            if (image.isNull() || gen != m_generation.load()) {
                return;
            }
            // Deliver result on the GUI thread
            QMetaObject::invokeMethod(this, "setThumbnailIcon", Qt::QueuedConnection,
                                      Q_ARG(int, i),
                                      Q_ARG(QImage, image));
        });
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
