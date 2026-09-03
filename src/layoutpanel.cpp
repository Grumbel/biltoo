// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutpanel.h"
#include "icons.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

LayoutPanel::LayoutPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    auto *hint = new QLabel(
        tr("Arrange the selected Workspace images with a packaged layout."),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *modeBox = new QGroupBox(tr("Layout"), this);
    auto *modeLayout = new QVBoxLayout(modeBox);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    auto addMode = [&](QToolButton *&btn, const QString &text, const QString &iconName,
                       GalleryLayout::Mode mode, bool checked = false) {
        btn = new QToolButton(modeBox);
        btn->setText(text);
        btn->setIcon(resourceIcon(iconName));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setProperty("layoutMode", static_cast<int>(mode));
        m_modeGroup->addButton(btn);
        modeLayout->addWidget(btn);
    };

    addMode(m_sideBySideBtn, tr("Horizontal"), QStringLiteral("gallery-side-by-side"),
            GalleryLayout::Mode::SideBySide);
    addMode(m_verticalBtn, tr("Vertical"), QStringLiteral("gallery-vertical"),
            GalleryLayout::Mode::Vertical);
    addMode(m_gridBtn, tr("Grid"), QStringLiteral("gallery-grid"),
            GalleryLayout::Mode::Grid, true);
    // Grid Crop disabled for now (session crop interaction is unreliable).
    m_gridCropBtn = nullptr;
    addMode(m_masonryBtn, tr("Masonry"), QStringLiteral("gallery-masonry"),
            GalleryLayout::Mode::Masonry);
    addMode(m_masonryRowsBtn, tr("Masonry Rows"), QStringLiteral("gallery-masonry-rows"),
            GalleryLayout::Mode::MasonryRows);
    addMode(m_masonryFillBtn, tr("Masonry Fill"), QStringLiteral("gallery-masonry"),
            GalleryLayout::Mode::MasonryFill);
    addMode(m_masonryRowsFillBtn, tr("Masonry Rows Fill"), QStringLiteral("gallery-masonry-rows"),
            GalleryLayout::Mode::MasonryRowsFill);

    root->addWidget(modeBox);

    auto *paramsForm = new QFormLayout();
    m_columnsLabel = new QLabel(tr("Columns:"), this);
    m_columnsSpin = new QSpinBox(this);
    m_columnsSpin->setRange(1, 32);
    m_columnsSpin->setValue(3);
    m_columnsSpin->setToolTip(tr("Column count for Grid and Masonry layouts"));
    {
        auto *row = new QWidget(this);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        lay->addWidget(m_columnsSpin, 1);
        auto *reset = new QToolButton(row);
        reset->setAutoRaise(true);
        reset->setIcon(themeIcon(QStringLiteral("edit-clear"), QStyle::SP_DialogResetButton));
        reset->setToolTip(tr("Reset to default"));
        reset->setFixedSize(24, 24);
        connect(reset, &QToolButton::clicked, this, [this]() { m_columnsSpin->setValue(3); });
        lay->addWidget(reset);
        paramsForm->addRow(m_columnsLabel, row);
    }

    m_rowsLabel = new QLabel(tr("Rows:"), this);
    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setRange(1, 32);
    m_rowsSpin->setValue(3);
    m_rowsSpin->setToolTip(tr("Row count for Masonry Rows / Rows Fill"));
    {
        auto *row = new QWidget(this);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        lay->addWidget(m_rowsSpin, 1);
        auto *reset = new QToolButton(row);
        reset->setAutoRaise(true);
        reset->setIcon(themeIcon(QStringLiteral("edit-clear"), QStyle::SP_DialogResetButton));
        reset->setToolTip(tr("Reset to default"));
        reset->setFixedSize(24, 24);
        connect(reset, &QToolButton::clicked, this, [this]() { m_rowsSpin->setValue(3); });
        lay->addWidget(reset);
        paramsForm->addRow(m_rowsLabel, row);
    }
    root->addLayout(paramsForm);

    m_applyBtn = new QPushButton(tr("Apply"), this);
    m_applyBtn->setDefault(true);
    connect(m_applyBtn, &QPushButton::clicked, this, &LayoutPanel::applyRequested);
    root->addWidget(m_applyBtn);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(m_hintLabel);

    root->addStretch(1);

    connect(m_modeGroup, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool) {
        updateControlsEnabled();
    });
    updateControlsEnabled();
}

GalleryLayout::Mode LayoutPanel::selectedMode() const
{
    if (QAbstractButton *btn = m_modeGroup->checkedButton()) {
        return static_cast<GalleryLayout::Mode>(btn->property("layoutMode").toInt());
    }
    return GalleryLayout::Mode::Grid;
}

GalleryLayout::Params LayoutPanel::params() const
{
    GalleryLayout::Params p;
    p.mode = selectedMode();
    p.margin = 16.0;
    p.gap = 12.0;
    p.gridColumns = m_columnsSpin->value();
    p.masonryColumns = m_columnsSpin->value();
    p.masonryRows = m_rowsSpin->value();
    // availW / availH filled by ImageView using the current viewport.
    return p;
}

void LayoutPanel::setWorkspaceActive(bool active)
{
    if (m_workspaceActive == active) {
        return;
    }
    m_workspaceActive = active;
    updateControlsEnabled();
}

void LayoutPanel::setSelectionCount(int count)
{
    if (m_selectionCount == count) {
        return;
    }
    m_selectionCount = count;
    updateControlsEnabled();
}

void LayoutPanel::updateControlsEnabled()
{
    const GalleryLayout::Mode mode = selectedMode();
    const bool cols = (mode == GalleryLayout::Mode::Grid
                       || mode == GalleryLayout::Mode::GridCrop
                       || mode == GalleryLayout::Mode::Masonry
                       || mode == GalleryLayout::Mode::MasonryFill);
    const bool rows = (mode == GalleryLayout::Mode::MasonryRows
                       || mode == GalleryLayout::Mode::MasonryRowsFill);
    m_columnsLabel->setEnabled(m_workspaceActive && cols);
    m_columnsSpin->setEnabled(m_workspaceActive && cols);
    m_rowsLabel->setEnabled(m_workspaceActive && rows);
    m_rowsSpin->setEnabled(m_workspaceActive && rows);

    for (QAbstractButton *btn : m_modeGroup->buttons()) {
        if (btn) {
            btn->setEnabled(m_workspaceActive);
        }
    }

    const bool canApply = m_workspaceActive && m_selectionCount > 0;
    m_applyBtn->setEnabled(canApply);

    if (!m_workspaceActive) {
        m_hintLabel->setText(tr("Switch to Workspace mode to lay out images."));
    } else if (m_selectionCount <= 0) {
        m_hintLabel->setText(tr("Select one or more images on the canvas, then Apply."));
    } else {
        m_hintLabel->setText(
            tr("%n image(s) selected — Apply rearranges only the selection.",
               nullptr, m_selectionCount));
    }
}
