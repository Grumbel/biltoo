// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/croppanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QFrame>

CropPanel::CropPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

void CropPanel::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *modeBox = new QGroupBox(tr("Mode"), inner);
    auto *modeForm = new QFormLayout(modeBox);
    m_mode = new QComboBox(modeBox);
    m_mode->addItem(tr("Manual margins"), int(CropPanelRecipe::Mode::ManualMargins));
    m_mode->addItem(tr("Autocrop"), int(CropPanelRecipe::Mode::Autocrop));
    m_mode->setToolTip(tr("Manual: inset from each edge in page pixels.\n"
                          "Autocrop: trim uniform background (GIMP-style), then extra margin."));
    modeForm->addRow(tr("Crop"), m_mode);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        syncModeVisibility();
        emitIfChanged();
    });
    layout->addWidget(modeBox);

    m_manualBox = new QGroupBox(tr("Manual margins (px)"), inner);
    auto *manForm = new QFormLayout(m_manualBox);
    auto addSpin = [&](const QString &name, QSpinBox **box) {
        *box = new QSpinBox(m_manualBox);
        (*box)->setRange(0, 100000);
        (*box)->setValue(0);
        (*box)->setSuffix(QStringLiteral(" px"));
        manForm->addRow(name, *box);
        connect(*box, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            emitIfChanged();
        });
    };
    addSpin(tr("Left"), &m_marginL);
    addSpin(tr("Top"), &m_marginT);
    addSpin(tr("Right"), &m_marginR);
    addSpin(tr("Bottom"), &m_marginB);
    layout->addWidget(m_manualBox);

    m_autoBox = new QGroupBox(tr("Autocrop"), inner);
    auto *autoForm = new QFormLayout(m_autoBox);
    m_threshold = new QSlider(Qt::Horizontal, m_autoBox);
    m_threshold->setRange(1, 128);
    m_threshold->setValue(24);
    m_thresholdVal = new QLabel(QStringLiteral("24"), m_autoBox);
    m_thresholdVal->setMinimumWidth(36);
    m_thresholdVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto *row = new QWidget(m_autoBox);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(m_threshold, 1);
        hl->addWidget(m_thresholdVal);
        autoForm->addRow(tr("Threshold"), row);
    }
    connect(m_threshold, &QSlider::valueChanged, this, [this](int v) {
        if (m_thresholdVal) {
            m_thresholdVal->setText(QString::number(v));
        }
        emitIfChanged();
    });
    layout->addWidget(m_autoBox);

    auto *extraBox = new QGroupBox(tr("Extra margin (px)"), inner);
    extraBox->setToolTip(tr("Expanded outward after manual inset or autocrop so content is not tight."));
    auto *extraForm = new QFormLayout(extraBox);
    auto addExtra = [&](const QString &name, QSpinBox **box) {
        *box = new QSpinBox(extraBox);
        (*box)->setRange(0, 100000);
        (*box)->setValue(0);
        (*box)->setSuffix(QStringLiteral(" px"));
        extraForm->addRow(name, *box);
        connect(*box, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            emitIfChanged();
        });
    };
    addExtra(tr("Left"), &m_extraL);
    addExtra(tr("Top"), &m_extraT);
    addExtra(tr("Right"), &m_extraR);
    addExtra(tr("Bottom"), &m_extraB);
    layout->addWidget(extraBox);

    auto *actions = new QGroupBox(tr("Apply"), inner);
    auto *actLay = new QVBoxLayout(actions);
    m_applyCurrentBtn = new QPushButton(tr("Apply to current"), actions);
    m_resetCurrentBtn = new QPushButton(tr("Reset crop (current)"), actions);
    m_applySelectionBtn = new QPushButton(tr("Apply to selection"), actions);
    m_applySelectionBtn->setEnabled(false);
    m_applySelectionBtn->setToolTip(
        tr("Write this crop recipe onto all selected Gallery or Workspace tiles. "
           "One undo step groups the batch."));
    m_resetSelectionBtn = new QPushButton(tr("Reset crop (selection)"), actions);
    m_resetSelectionBtn->setEnabled(false);
    actLay->addWidget(m_applyCurrentBtn);
    actLay->addWidget(m_resetCurrentBtn);
    actLay->addWidget(m_applySelectionBtn);
    actLay->addWidget(m_resetSelectionBtn);
    connect(m_applyCurrentBtn, &QPushButton::clicked, this, [this]() {
        emit applyToCurrentRequested(recipe());
    });
    connect(m_resetCurrentBtn, &QPushButton::clicked, this, [this]() {
        emit resetCropOnCurrentRequested();
    });
    connect(m_applySelectionBtn, &QPushButton::clicked, this, [this]() {
        emit applyToSelectionRequested(recipe());
    });
    connect(m_resetSelectionBtn, &QPushButton::clicked, this, [this]() {
        emit resetCropOnSelectionRequested();
    });
    layout->addWidget(actions);
    layout->addStretch(1);

    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    syncModeVisibility();
}

void CropPanel::syncModeVisibility()
{
    const int mode = m_mode ? m_mode->currentData().toInt() : 0;
    const bool manual = mode == int(CropPanelRecipe::Mode::ManualMargins);
    if (m_manualBox) {
        m_manualBox->setVisible(manual);
    }
    if (m_autoBox) {
        m_autoBox->setVisible(!manual);
    }
}

CropPanelRecipe CropPanel::recipe() const
{
    CropPanelRecipe r;
    if (m_mode) {
        r.mode = static_cast<CropPanelRecipe::Mode>(m_mode->currentData().toInt());
    }
    r.marginLeft = m_marginL ? m_marginL->value() : 0;
    r.marginTop = m_marginT ? m_marginT->value() : 0;
    r.marginRight = m_marginR ? m_marginR->value() : 0;
    r.marginBottom = m_marginB ? m_marginB->value() : 0;
    r.autocropThreshold = m_threshold ? m_threshold->value() : 24;
    r.extraLeft = m_extraL ? m_extraL->value() : 0;
    r.extraTop = m_extraT ? m_extraT->value() : 0;
    r.extraRight = m_extraR ? m_extraR->value() : 0;
    r.extraBottom = m_extraB ? m_extraB->value() : 0;
    return r;
}

void CropPanel::setRecipe(const CropPanelRecipe &r)
{
    m_block = true;
    if (m_mode) {
        const int idx = m_mode->findData(int(r.mode));
        if (idx >= 0) {
            m_mode->setCurrentIndex(idx);
        }
    }
    if (m_marginL) m_marginL->setValue(r.marginLeft);
    if (m_marginT) m_marginT->setValue(r.marginTop);
    if (m_marginR) m_marginR->setValue(r.marginRight);
    if (m_marginB) m_marginB->setValue(r.marginBottom);
    if (m_threshold) m_threshold->setValue(r.autocropThreshold);
    if (m_extraL) m_extraL->setValue(r.extraLeft);
    if (m_extraT) m_extraT->setValue(r.extraTop);
    if (m_extraR) m_extraR->setValue(r.extraRight);
    if (m_extraB) m_extraB->setValue(r.extraBottom);
    m_block = false;
    syncModeVisibility();
}

void CropPanel::emitIfChanged()
{
    if (!m_block) {
        emit recipeChanged(recipe());
    }
}

void CropPanel::setEnabledControls(bool on)
{
    setEnabled(on);
}

void CropPanel::setApplyToSelectionEnabled(bool on)
{
    if (m_applySelectionBtn) {
        m_applySelectionBtn->setEnabled(on);
    }
    if (m_resetSelectionBtn) {
        m_resetSelectionBtn->setEnabled(on);
    }
}
