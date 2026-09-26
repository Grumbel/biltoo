// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textpanelmodel.h"
#include "text/textlayergeometry.h"
#include "text/textregionstyle.h"

TextPanelModel::TextPanelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TextPanelModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_order.size();
}

QVariant TextPanelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_order.size()) {
        return {};
    }
    const int ri = m_order.at(index.row());
    if (ri < 0 || ri >= m_layer.regions.size()) {
        return {};
    }
    const auto &r = m_layer.regions.at(ri);
    switch (role) {
    case Qt::DisplayRole: {
        QString t = r.text;
        if (t.isEmpty() && r.role == ThumtooCache::TextRegion::Role::Link) {
            t = r.linkUri.isEmpty() ? tr("[link]") : r.linkUri;
        }
        if (t.isEmpty()) {
            t = tr("[empty]");
        }
        const QString kind = (r.role == ThumtooCache::TextRegion::Role::Link)
            ? tr("link")
            : TextRegionStyle::kindLabel(r.kind);
        return QStringLiteral("[%1] %2").arg(kind, t.simplified());
    }
    case TextRole: {
        return r.text;
    }
    case Qt::ToolTipRole:
        return r.text.isEmpty() ? r.linkUri : r.text;
    case KindRole: {
        if (r.role == ThumtooCache::TextRegion::Role::Link) {
            return tr("link");
        }
        return TextRegionStyle::kindLabel(r.kind);
    }
    case Qt::DecorationRole: {
        return TextRegionStyle::outlineColor(r);
    }
    case RegionIndexRole:
        return ri;
    case IsLinkRole:
        return r.role == ThumtooCache::TextRegion::Role::Link;
    default:
        return {};
    }
}

QHash<int, QByteArray> TextPanelModel::roleNames() const
{
    return {
        {TextRole, "text"},
        {KindRole, "kind"},
        {RegionIndexRole, "regionIndex"},
        {IsLinkRole, "isLink"},
        {Qt::DisplayRole, "display"},
    };
}

void TextPanelModel::setLayer(const ThumtooCache::PageTextLayer &layer)
{
    beginResetModel();
    m_layer = layer;
    m_order.resize(layer.regions.size());
    QVector<QRectF> rects(layer.regions.size());
    QVector<int> blocks(layer.regions.size(), -1);
    for (int i = 0; i < layer.regions.size(); ++i) {
        m_order[i] = i;
        rects[i] = layer.regions.at(i).bbox;
        blocks[i] = layer.regions.at(i).blockId;
    }
    TextLayerGeometry::sortReadingOrder(&m_order, rects, 4.0, &blocks);
    endResetModel();
}

void TextPanelModel::clear()
{
    beginResetModel();
    m_layer = {};
    m_order.clear();
    endResetModel();
}

int TextPanelModel::regionIndexAt(int row) const
{
    if (row < 0 || row >= m_order.size()) {
        return -1;
    }
    return m_order.at(row);
}

int TextPanelModel::rowForRegion(int regionIndex) const
{
    return m_order.indexOf(regionIndex);
}
