// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTPANELMODEL_H
#define TEXTPANELMODEL_H

#include "host/thumtoocache.h"
#include "imageview_types.h"

#include <QAbstractListModel>
#include <QString>
#include <QVector>

/**
 * List model over one or more PageTextLayers (single page or spread flatten).
 * Rows are reading order within each page, pages in member order.
 */
class TextPanelModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        KindRole,
        RegionIndexRole,
        IsLinkRole,
        SessionIdRole,
        PageLabelRole,
    };

    struct MemberLayer {
        SessionImageId sessionId = kInvalidSessionImageId;
        QString pageLabel;
        ThumtooCache::PageTextLayer layer;
    };

    explicit TextPanelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLayer(const ThumtooCache::PageTextLayer &layer);
    /** Ordered members (spread reading order). Empty → clear. */
    void setMemberLayers(const QVector<MemberLayer> &members);
    void clear();

    bool isMultiPage() const { return m_members.size() > 1; }
    int regionIndexAt(int row) const;
    SessionImageId sessionIdAt(int row) const;
    int rowForRegion(int regionIndex) const;
    int rowForRegion(SessionImageId sessionId, int regionIndex) const;

private:
    void rebuildOrder();

    QVector<MemberLayer> m_members;
    struct RowRef {
        int memberIndex = 0;
        int regionIndex = 0;
    };
    QVector<RowRef> m_order;
};

#endif
