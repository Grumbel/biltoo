// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tocpanel.h"

#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

TocPanel::TocPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_emptyLabel = new QLabel(tr("No table of contents for this document."), this);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_emptyLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemActivated, this, &TocPanel::onItemActivated);

    clear();
}

void TocPanel::clear()
{
    m_tree->clear();
    m_tree->hide();
    m_emptyLabel->show();
}

bool TocPanel::isEmpty() const
{
    return m_tree->topLevelItemCount() == 0;
}

void TocPanel::setOutline(const ThumtooCache::DocumentOutline &outline)
{
    m_tree->clear();
    if (outline.items.isEmpty()) {
        m_tree->hide();
        m_emptyLabel->show();
        return;
    }
    m_emptyLabel->hide();
    m_tree->show();

    // Stack of parents by level (1-based levels from thumtoo).
    QVector<QTreeWidgetItem *> stack;
    for (const ThumtooCache::OutlineItem &it : outline.items) {
        const int level = qMax(1, it.level);
        while (stack.size() >= level) {
            stack.pop_back();
        }
        QString label = it.title.isEmpty() ? tr("(untitled)") : it.title;
        if (it.page > 0) {
            label += QStringLiteral("  ·  %1").arg(it.page);
        }
        auto *item = new QTreeWidgetItem(QStringList{label});
        item->setData(0, Qt::UserRole, it.page);
        item->setData(0, Qt::UserRole + 1, it.uri);
        item->setToolTip(0, it.uri.isEmpty()
                                 ? (it.page > 0 ? tr("Page %1").arg(it.page) : label)
                                 : it.uri);
        if (stack.isEmpty()) {
            m_tree->addTopLevelItem(item);
        } else {
            stack.last()->addChild(item);
        }
        stack.push_back(item);
    }
    m_tree->expandToDepth(1);
}

void TocPanel::onItemActivated(QTreeWidgetItem *item, int)
{
    if (!item) {
        return;
    }
    const int page = item->data(0, Qt::UserRole).toInt();
    const QString uri = item->data(0, Qt::UserRole + 1).toString();
    if (page > 0) {
        emit navigateToPage(page);
    } else if (!uri.isEmpty()) {
        emit openExternalUri(uri);
    }
}
