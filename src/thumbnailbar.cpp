// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailbar.h"

#include <QFileInfo>
#include <QImageReader>
#include <QListWidgetItem>
#include <QScrollBar>

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

    setMaximumHeight(kBarHeight);
    setMinimumHeight(kBarHeight);

    connect(this, &QListWidget::itemActivated, this, &ThumbnailBar::onItemActivated);
    connect(this, &QListWidget::currentRowChanged, this, &ThumbnailBar::onCurrentRowChanged);
}

QSize ThumbnailBar::sizeHint() const
{
    return QSize(400, kBarHeight);
}

void ThumbnailBar::setFiles(const QStringList &files)
{
    clear();

    for (const QString &path : files) {
        auto *item = new QListWidgetItem(this);
        item->setText(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(kThumbSize + 12, kThumbSize + 24));
        // Placeholder icon; real thumbnail loaded lazily below
        item->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    }

    // Load thumbnails (prototype: synchronous; OK for modest lists)
    for (int i = 0; i < count(); ++i) {
        loadThumbnail(i);
    }

    if (count() > 0) {
        setCurrentRow(0);
    }
}

void ThumbnailBar::loadThumbnail(int row)
{
    QListWidgetItem *item = this->item(row);
    if (!item) {
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    QImageReader reader(path);
    reader.setAutoTransform(true);

    QSize size = reader.size();
    if (size.isValid()) {
        size.scale(kThumbSize, kThumbSize, Qt::KeepAspectRatio);
        reader.setScaledSize(size);
    }

    const QImage image = reader.read();
    if (!image.isNull()) {
        item->setIcon(QIcon(QPixmap::fromImage(image)));
    }
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

void ThumbnailBar::onItemActivated(QListWidgetItem *item)
{
    if (item) {
        emit indexActivated(row(item));
    }
}

void ThumbnailBar::onCurrentRowChanged(int row)
{
    if (row >= 0) {
        emit indexActivated(row);
    }
}
