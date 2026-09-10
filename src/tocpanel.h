// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOCPANEL_H
#define TOCPANEL_H

#include "thumtoocache.h"

#include <QWidget>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Side panel: document outline (TOC) from thumtoo.
 * Double-click / activate → page navigation or external URI.
 */
class TocPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TocPanel(QWidget *parent = nullptr);

    void setOutline(const ThumtooCache::DocumentOutline &outline);
    void clear();
    bool isEmpty() const;

signals:
    void navigateToPage(int page_1based);
    void openExternalUri(const QString &uri);

private slots:
    void onItemActivated(QTreeWidgetItem *item, int column);

private:
    QLabel *m_emptyLabel = nullptr;
    QTreeWidget *m_tree = nullptr;
};

#endif // TOCPANEL_H
