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
    const RowRef &ref = m_order.at(index.row());
    if (ref.memberIndex < 0 || ref.memberIndex >= m_members.size()) {
        return {};
    }
    const MemberLayer &mem = m_members.at(ref.memberIndex);
    if (ref.regionIndex < 0 || ref.regionIndex >= mem.layer.regions.size()) {
        return {};
    }
    const auto &r = mem.layer.regions.at(ref.regionIndex);
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
        if (isMultiPage() && !mem.pageLabel.isEmpty()) {
            return QStringLiteral("[%1 · %2] %3").arg(mem.pageLabel, kind, t.simplified());
        }
        return QStringLiteral("[%1] %2").arg(kind, t.simplified());
    }
    case TextRole:
        return r.text;
    case Qt::ToolTipRole:
        return r.text.isEmpty() ? r.linkUri : r.text;
    case KindRole: {
        if (r.role == ThumtooCache::TextRegion::Role::Link) {
            return tr("link");
        }
        return TextRegionStyle::kindLabel(r.kind);
    }
    case Qt::DecorationRole:
        return TextRegionStyle::outlineColor(r);
    case RegionIndexRole:
        return ref.regionIndex;
    case SessionIdRole:
        return QVariant::fromValue(qint64(mem.sessionId));
    case PageLabelRole:
        return mem.pageLabel;
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
        {SessionIdRole, "sessionId"},
        {PageLabelRole, "pageLabel"},
        {IsLinkRole, "isLink"},
        {Qt::DisplayRole, "display"},
    };
}

void TextPanelModel::setLayer(const ThumtooCache::PageTextLayer &layer)
{
    MemberLayer m;
    m.sessionId = kInvalidSessionImageId;
    m.layer = layer;
    setMemberLayers(QVector<MemberLayer>{m});
}

void TextPanelModel::setMemberLayers(const QVector<MemberLayer> &members)
{
    beginResetModel();
    m_members = members;
    rebuildOrder();
    endResetModel();
}

void TextPanelModel::rebuildOrder()
{
    m_order.clear();
    for (int mi = 0; mi < m_members.size(); ++mi) {
        const auto &layer = m_members.at(mi).layer;
        QVector<int> order(layer.regions.size());
        QVector<QRectF> rects(layer.regions.size());
        QVector<int> blocks(layer.regions.size(), -1);
        for (int i = 0; i < layer.regions.size(); ++i) {
            order[i] = i;
            rects[i] = layer.regions.at(i).bbox;
            blocks[i] = layer.regions.at(i).blockId;
        }
        const bool ocrOrder = layer.source == ThumtooCache::TextLayerSource::Ocr;
        TextLayerGeometry::sortReadingOrder(&order, rects, 4.0, &blocks,
                                            layer.pageYUp, ocrOrder);
        for (int ri : order) {
            RowRef ref;
            ref.memberIndex = mi;
            ref.regionIndex = ri;
            m_order.append(ref);
        }
    }
}

void TextPanelModel::clear()
{
    beginResetModel();
    m_members.clear();
    m_order.clear();
    endResetModel();
}

int TextPanelModel::regionIndexAt(int row) const
{
    if (row < 0 || row >= m_order.size()) {
        return -1;
    }
    return m_order.at(row).regionIndex;
}

SessionImageId TextPanelModel::sessionIdAt(int row) const
{
    if (row < 0 || row >= m_order.size()) {
        return kInvalidSessionImageId;
    }
    const int mi = m_order.at(row).memberIndex;
    if (mi < 0 || mi >= m_members.size()) {
        return kInvalidSessionImageId;
    }
    return m_members.at(mi).sessionId;
}

int TextPanelModel::rowForRegion(int regionIndex) const
{
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_order.at(i).regionIndex == regionIndex) {
            return i;
        }
    }
    return -1;
}

int TextPanelModel::rowForRegion(SessionImageId sessionId, int regionIndex) const
{
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_order.at(i).regionIndex != regionIndex) {
            continue;
        }
        const int mi = m_order.at(i).memberIndex;
        if (mi >= 0 && mi < m_members.size()
            && m_members.at(mi).sessionId == sessionId) {
            return i;
        }
    }
    return rowForRegion(regionIndex);
}
