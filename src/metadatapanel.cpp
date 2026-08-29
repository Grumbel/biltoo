// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadatapanel.h"

#include <QFileInfo>
#include <QDateTime>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

MetadataPanel::MetadataPanel(QWidget *parent)
    : QWidget(parent)
{
    m_header = new QLabel(tr("No image"), this);
    m_header->setWordWrap(true);
    m_header->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Field"), tr("Value")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_header);
    layout->addWidget(m_tree, 1);

    setMinimumWidth(220);
}

void MetadataPanel::clear()
{
    m_header->setText(tr("No image"));
    m_tree->clear();
}

void MetadataPanel::addRow(const QString &key, const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, key);
    item->setText(1, value);
    item->setToolTip(1, value);
}

void MetadataPanel::setImagePath(const QString &path)
{
    m_tree->clear();

    if (path.isEmpty()) {
        clear();
        return;
    }

    const QFileInfo info(path);
    m_header->setText(info.fileName());
    m_header->setToolTip(path);

    addRow(tr("Path"), info.absoluteFilePath());
    addRow(tr("Size"),
           tr("%1 bytes").arg(info.size()));
    addRow(tr("Modified"),
           info.lastModified().toString(Qt::DefaultLocaleShortDate));

    QImageReader reader(path);
    reader.setAutoTransform(true);

    if (!reader.canRead()) {
        addRow(tr("Error"), reader.errorString());
        return;
    }

    const QSize size = reader.size();
    if (size.isValid()) {
        addRow(tr("Dimensions"),
               tr("%1 × %2").arg(size.width()).arg(size.height()));
    }
    addRow(tr("Format"), QString::fromLatin1(reader.format()).toUpper());

    // Embedded metadata exposed by the plugin (Exif, etc.)
    const QStringList keys = reader.textKeys();
    if (keys.isEmpty()) {
        addRow(tr("Metadata"), tr("(none reported by Qt image plugin)"));
    } else {
        QStringList sorted = keys;
        sorted.sort(Qt::CaseInsensitive);
        for (const QString &key : sorted) {
            addRow(key, reader.text(key));
        }
    }
}
