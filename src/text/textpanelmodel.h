// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTPANELMODEL_H
#define TEXTPANELMODEL_H

#include "host/thumtoocache.h"

#include <QAbstractListModel>
#include <QVector>

/**
 * Read-only list model over a PageTextLayer's regions (one row per region).
 * Roles carry text, kind, and the stable region index for selection bridging.
 */
class TextPanelModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        KindRole,
        RegionIndexRole,
        IsLinkRole,
    };

    explicit TextPanelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLayer(const ThumtooCache::PageTextLayer &layer);
    void clear();

    int regionIndexAt(int row) const;
    int rowForRegion(int regionIndex) const;

private:
    ThumtooCache::PageTextLayer m_layer;
    /** Rows in reading order → original region index. */
    QVector<int> m_order;
};

#endif
