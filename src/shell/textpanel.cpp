// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/textpanel.h"
#include "text/textpanelmodel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QItemSelectionModel>
#include <QEvent>
#include <QAbstractItemView>

TextPanel::TextPanel(QWidget *parent)
    : QWidget(parent)
{
    m_model = new TextPanelModel(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_info = new QLabel(tr("No text layer"), this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(m_info);
    auto *legend = new QLabel(
        tr("Colours: body · header · footer · page# · link"), this);
    legend->setWordWrap(true);
    legend->setStyleSheet(QStringLiteral("color: palette(mid); font-size: small;"));
    legend->setToolTip(
        tr("Kind comes from OCR layout post-pass (top/bottom bands + token shape).
"
           "Native PDF text is usually all “body” until a similar pass runs.
"
           "Columns / true headings are not classified yet."));
    layout->addWidget(legend);

    auto *opts = new QHBoxLayout;
    m_outlines = new QCheckBox(tr("Outlines"), this);
    m_outlines->setToolTip(tr("Draw region bounding boxes on the page"));
    m_glyphs = new QCheckBox(tr("Show text in boxes"), this);
    m_glyphs->setToolTip(
        tr("Paint the recognized text inside each region on the page "
           "(what OCR/native extraction produced)"));
    opts->addWidget(m_outlines);
    opts->addWidget(m_glyphs);
    layout->addLayout(opts);
    connect(m_outlines, &QCheckBox::toggled, this, &TextPanel::showOutlinesToggled);
    connect(m_glyphs, &QCheckBox::toggled, this, &TextPanel::showGlyphsToggled);

    auto *refresh = new QPushButton(tr("Reload layer"), this);
    refresh->setToolTip(tr("Re-fetch the text layer for the current image"));
    connect(refresh, &QPushButton::clicked, this, &TextPanel::refreshRequested);
    layout->addWidget(refresh);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setMouseTracking(true);
    m_view->setUniformItemSizes(false);
    m_view->setWordWrap(true);
    layout->addWidget(m_view, 1);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                onViewSelectionChanged();
            });
    connect(m_view, &QListView::entered, this, &TextPanel::onViewEntered);
    // Viewport leave: clear hover when leaving the list.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
}

bool TextPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view->viewport() && event->type() == QEvent::Leave) {
        onViewLeft();
    }
    return QWidget::eventFilter(obj, event);
}

void TextPanel::setLayerInfo(const QString &info)
{
    if (m_info) {
        m_info->setText(info.isEmpty() ? tr("No text layer") : info);
    }
}

void TextPanel::setShowGlyphsChecked(bool on)
{
    if (m_glyphs) {
        QSignalBlocker b(m_glyphs);
        m_glyphs->setChecked(on);
    }
}

void TextPanel::setShowOutlinesChecked(bool on)
{
    if (m_outlines) {
        QSignalBlocker b(m_outlines);
        m_outlines->setChecked(on);
    }
}

void TextPanel::setSelectedRegions(const QVector<int> &regionIndices)
{
    if (!m_view || !m_model) {
        return;
    }
    m_blockSel = true;
    QItemSelection sel;
    for (int ri : regionIndices) {
        const int row = m_model->rowForRegion(ri);
        if (row < 0) {
            continue;
        }
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
    }
    m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (!regionIndices.isEmpty()) {
        const int row = m_model->rowForRegion(regionIndices.first());
        if (row >= 0) {
            m_view->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
        }
    }
    m_blockSel = false;
}

void TextPanel::setHoverRegion(int regionIndex)
{
    if (!m_view || !m_model || regionIndex < 0) {
        return;
    }
    const int row = m_model->rowForRegion(regionIndex);
    if (row < 0) {
        return;
    }
    // Soft visual: current index without changing selection.
    m_view->setCurrentIndex(m_model->index(row, 0));
}

void TextPanel::onViewSelectionChanged()
{
    if (m_blockSel || !m_view || !m_model) {
        return;
    }
    QVector<int> regions;
    const auto rows = m_view->selectionModel()->selectedRows();
    regions.reserve(rows.size());
    for (const QModelIndex &idx : rows) {
        const int ri = m_model->regionIndexAt(idx.row());
        if (ri >= 0) {
            regions.append(ri);
        }
    }
    emit selectionRegionsChanged(regions);
}

void TextPanel::onViewEntered(const QModelIndex &index)
{
    if (!m_model || !index.isValid()) {
        return;
    }
    emit hoverRegionChanged(m_model->regionIndexAt(index.row()));
}

void TextPanel::onViewLeft()
{
    emit hoverRegionChanged(-1);
}
