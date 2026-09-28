// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/annotationpanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

struct Preset {
    const char *name;
    int r, g, b;
};

const Preset kPresets[] = {
    {"Yellow", 246, 211, 45},
    {"Green", 143, 240, 164},
    {"Cyan", 153, 193, 241},
    {"Pink", 255, 120, 186},
    {"Orange", 255, 163, 72},
    {"Red", 224, 60, 50},
    {"Blue", 53, 132, 228},
    {"Black", 28, 28, 28},
    {"White", 250, 250, 250},
    {"Sticky", 255, 230, 100},
};

} // namespace

AnnotationPanel::AnnotationPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_status = new QLabel(tr("Choose a colour and stroke width for annotation tools."),
                          inner);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *swatchRow = new QHBoxLayout;
    m_swatch = new QLabel(inner);
    m_swatch->setFixedSize(48, 48);
    m_swatch->setFrameShape(QFrame::StyledPanel);
    m_swatch->setToolTip(tr("Current annotation colour"));
    swatchRow->addWidget(m_swatch);
    m_customColorBtn = new QPushButton(tr("Custom…"), inner);
    m_customColorBtn->setToolTip(tr("Open colour dialog"));
    connect(m_customColorBtn, &QPushButton::clicked, this, &AnnotationPanel::pickCustomColor);
    swatchRow->addWidget(m_customColorBtn);
    swatchRow->addStretch(1);
    layout->addLayout(swatchRow);

    auto *presetBox = new QGroupBox(tr("Presets"), inner);
    auto *grid = new QGridLayout(presetBox);
    grid->setSpacing(4);
    int col = 0;
    int row = 0;
    for (const Preset &pr : kPresets) {
        const QColor colr(pr.r, pr.g, pr.b);
        auto *btn = new QToolButton(presetBox);
        btn->setFixedSize(28, 28);
        btn->setToolTip(tr(pr.name));
        QPixmap pm(22, 22);
        pm.fill(colr);
        btn->setIcon(QIcon(pm));
        btn->setIconSize(QSize(20, 20));
        connect(btn, &QToolButton::clicked, this, [this, colr]() { emitColor(colr); });
        grid->addWidget(btn, row, col);
        if (++col >= 5) {
            col = 0;
            ++row;
        }
    }
    layout->addWidget(presetBox);

    auto *widthBox = new QGroupBox(tr("Stroke width"), inner);
    auto *widthForm = new QFormLayout(widthBox);
    m_widthSlider = new QSlider(Qt::Horizontal, widthBox);
    m_widthSlider->setRange(15, 400); // 0.1 px units → 1.5 … 40.0
    m_widthSlider->setValue(int(m_width * 10.0));
    m_widthSlider->setToolTip(tr("Brush / stroke width in page units (approx. pixels at 1:1)"));
    m_widthSpin = new QDoubleSpinBox(widthBox);
    m_widthSpin->setRange(1.0, 40.0);
    m_widthSpin->setDecimals(1);
    m_widthSpin->setSingleStep(0.5);
    m_widthSpin->setSuffix(tr(" px"));
    m_widthSpin->setValue(m_width);
    connect(m_widthSlider, &QSlider::valueChanged, this, &AnnotationPanel::onWidthSlider);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &AnnotationPanel::onWidthSpin);
    widthForm->addRow(m_widthSlider);
    widthForm->addRow(tr("Width"), m_widthSpin);
    layout->addWidget(widthBox);

    m_visibleCheck = new QCheckBox(tr("Show annotation layer"), inner);
    m_visibleCheck->setChecked(true);
    m_visibleCheck->setToolTip(tr("Hide or show all annotations without deleting them"));
    connect(m_visibleCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_block) {
            return;
        }
        emit layerVisibleChanged(on);
    });
    layout->addWidget(m_visibleCheck);

    layout->addStretch(1);
    scroll->setWidget(inner);
    root->addWidget(scroll, 1);

    rebuildSwatch();
}

void AnnotationPanel::rebuildSwatch()
{
    if (!m_swatch) {
        return;
    }
    QPixmap pm(44, 44);
    pm.fill(m_color.isValid() ? m_color : QColor(200, 200, 200));
    m_swatch->setPixmap(pm);
    m_swatch->setToolTip(tr("Current colour: %1").arg(m_color.name(QColor::HexArgb)));
}

void AnnotationPanel::setColor(const QColor &c)
{
    if (!c.isValid() || c == m_color) {
        return;
    }
    m_color = c;
    rebuildSwatch();
}

void AnnotationPanel::setWidth(qreal w)
{
    w = qBound(1.0, w, 40.0);
    if (qFuzzyCompare(w, m_width)) {
        return;
    }
    m_width = w;
    m_block = true;
    if (m_widthSlider) {
        m_widthSlider->setValue(int(m_width * 10.0 + 0.5));
    }
    if (m_widthSpin) {
        m_widthSpin->setValue(m_width);
    }
    m_block = false;
}

void AnnotationPanel::setLayerVisible(bool on)
{
    if (!m_visibleCheck) {
        return;
    }
    m_block = true;
    m_visibleCheck->setChecked(on);
    m_block = false;
}

bool AnnotationPanel::layerVisible() const
{
    return m_visibleCheck ? m_visibleCheck->isChecked() : true;
}

void AnnotationPanel::setStatusText(const QString &text)
{
    if (m_status) {
        m_status->setText(text);
    }
}

void AnnotationPanel::emitColor(const QColor &c)
{
    if (!c.isValid()) {
        return;
    }
    m_color = c;
    rebuildSwatch();
    emit colorChanged(c);
}

void AnnotationPanel::onWidthSlider(int value)
{
    if (m_block) {
        return;
    }
    const qreal w = value / 10.0;
    m_width = w;
    m_block = true;
    if (m_widthSpin) {
        m_widthSpin->setValue(w);
    }
    m_block = false;
    emit widthChanged(w);
}

void AnnotationPanel::onWidthSpin(double value)
{
    if (m_block) {
        return;
    }
    m_width = value;
    m_block = true;
    if (m_widthSlider) {
        m_widthSlider->setValue(int(value * 10.0 + 0.5));
    }
    m_block = false;
    emit widthChanged(value);
}

void AnnotationPanel::pickCustomColor()
{
    const QColor c = QColorDialog::getColor(m_color, this, tr("Annotation colour"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        emitColor(c);
    }
}
