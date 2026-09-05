// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowsettingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SlideshowSettingsDialog::SlideshowSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Slideshow Settings"));
    setModal(true);

    m_intervalSpin = new QDoubleSpinBox(this);
    m_intervalSpin->setRange(0.5, 3600.0);
    m_intervalSpin->setDecimals(1);
    m_intervalSpin->setSingleStep(0.5);
    m_intervalSpin->setSuffix(tr(" s"));
    m_intervalSpin->setToolTip(tr("Time each image stays on screen (dwell)"));

    m_fullscreenCheck = new QCheckBox(tr("Start slideshow in fullscreen"), this);
    m_fullscreenCheck->setToolTip(
        tr("Enter fullscreen automatically when starting a slideshow"));

    m_transitionCombo = new QComboBox(this);
    m_transitionCombo->addItem(tr("None"), 0);
    m_transitionCombo->addItem(tr("Crossfade"), 1);
    m_transitionCombo->addItem(tr("Fade through black"), 2);
    m_transitionCombo->addItem(tr("Slide (projector)"), 3);
    m_transitionCombo->setToolTip(tr("Effect used when advancing to the next image"));

    m_transitionMsSpin = new QDoubleSpinBox(this);
    m_transitionMsSpin->setRange(0.0, 5.0);
    m_transitionMsSpin->setSingleStep(0.05);
    m_transitionMsSpin->setDecimals(2);
    m_transitionMsSpin->setSuffix(tr(" s"));
    m_transitionMsSpin->setToolTip(
        tr("Duration of the full transition (outgoing + incoming; capped to the interval)"));

    m_motionCombo = new QComboBox(this);
    m_motionCombo->addItem(tr("Off"), 0);
    m_motionCombo->addItem(tr("Pan and zoom"), 1);
    m_motionCombo->addItem(tr("Pan and scan"), 2);
    m_motionCombo->setToolTip(
        tr("Pan and zoom: Ken Burns zoom while panning between points of interest.\n"
           "Pan and scan: pan across the image (no zoom).\n"
           "Both start from the Slideshow zoom base (Fit / Fill / 1:1)."));

    m_panZoomFactorSpin = new QDoubleSpinBox(this);
    m_panZoomFactorSpin->setRange(1.02, 1.40);
    m_panZoomFactorSpin->setSingleStep(0.01);
    m_panZoomFactorSpin->setDecimals(2);
    m_panZoomFactorSpin->setToolTip(
        tr("Pan and zoom only: end scale relative to the Slideshow zoom base"));

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->addItem(tr("Fit"), 0);
    m_zoomCombo->addItem(tr("Fill"), 1);
    m_zoomCombo->addItem(tr("1:1"), 2);
    m_zoomCombo->setToolTip(
        tr("Base framing for each slide (also when dwell motion is On).\n"
           "Fit: whole image visible (letterbox).\n"
           "Fill: cover the window (may crop).\n"
           "1:1: native pixels, centred (padding if smaller than the window).\n"
           "Pan and zoom starts from this scale; pan and scan pans at this scale."));

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->addRow(tr("Interval:"), m_intervalSpin);
    form->addRow(QString(), m_fullscreenCheck);
    form->addRow(tr("Transition:"), m_transitionCombo);
    form->addRow(tr("Transition duration:"), m_transitionMsSpin);
    form->addRow(tr("Dwell motion:"), m_motionCombo);
    form->addRow(tr("Pan and zoom factor:"), m_panZoomFactorSpin);
    form->addRow(tr("Slideshow zoom:"), m_zoomCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Close button maps to reject() for a Close-only box.
    if (QPushButton *closeBtn = buttons->button(QDialogButtonBox::Close)) {
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(m_intervalSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                syncTransitionCap();
                emitChanged();
            });
    connect(m_fullscreenCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
    connect(m_transitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { emitChanged(); });
    connect(m_transitionMsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { emitChanged(); });
    connect(m_motionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { emitChanged(); });
    connect(m_panZoomFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { emitChanged(); });
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { emitChanged(); });

    setMinimumWidth(360);
}

void SlideshowSettingsDialog::emitChanged()
{
    if (!m_blockEmit) {
        emit settingsChanged();
    }
}

void SlideshowSettingsDialog::syncTransitionCap()
{
    if (!m_intervalSpin || !m_transitionMsSpin) {
        return;
    }
    // Duration is the full transition (out + in); may use the whole dwell.
    // Both interval and transition are shown in seconds.
    const double intervalSec = m_intervalSpin->value();
    const double capSec = qBound(0.0, intervalSec, 5.0);
    m_blockEmit = true;
    m_transitionMsSpin->setMaximum(capSec);
    if (m_transitionMsSpin->value() > capSec) {
        m_transitionMsSpin->setValue(capSec);
    }
    m_blockEmit = false;
}

int SlideshowSettingsDialog::intervalMs() const
{
    return m_intervalSpin ? int(m_intervalSpin->value() * 1000.0 + 0.5) : 3000;
}

void SlideshowSettingsDialog::setIntervalMs(int ms)
{
    if (!m_intervalSpin) {
        return;
    }
    m_blockEmit = true;
    m_intervalSpin->setValue(qMax(0.5, ms / 1000.0));
    m_blockEmit = false;
    syncTransitionCap();
}

bool SlideshowSettingsDialog::startFullscreen() const
{
    return m_fullscreenCheck && m_fullscreenCheck->isChecked();
}

void SlideshowSettingsDialog::setStartFullscreen(bool on)
{
    if (!m_fullscreenCheck) {
        return;
    }
    m_blockEmit = true;
    m_fullscreenCheck->setChecked(on);
    m_blockEmit = false;
}

int SlideshowSettingsDialog::transitionIndex() const
{
    return m_transitionCombo ? m_transitionCombo->currentData().toInt() : 1;
}

void SlideshowSettingsDialog::setTransitionIndex(int index)
{
    if (!m_transitionCombo) {
        return;
    }
    m_blockEmit = true;
    const int idx = m_transitionCombo->findData(index);
    if (idx >= 0) {
        m_transitionCombo->setCurrentIndex(idx);
    }
    m_blockEmit = false;
}

int SlideshowSettingsDialog::transitionDurationMs() const
{
    // UI is seconds; internal API stays milliseconds.
    return m_transitionMsSpin ? qRound(m_transitionMsSpin->value() * 1000.0) : 400;
}

void SlideshowSettingsDialog::setTransitionDurationMs(int ms)
{
    if (!m_transitionMsSpin) {
        return;
    }
    m_blockEmit = true;
    syncTransitionCap();
    const double capSec = m_transitionMsSpin->maximum();
    const double sec = qBound(0.0, ms / 1000.0, capSec);
    m_transitionMsSpin->setValue(sec);
    m_blockEmit = false;
}

int SlideshowSettingsDialog::motionIndex() const
{
    return m_motionCombo ? m_motionCombo->currentData().toInt() : 0;
}

void SlideshowSettingsDialog::setMotionIndex(int index)
{
    if (!m_motionCombo) {
        return;
    }
    m_blockEmit = true;
    const int idx = m_motionCombo->findData(index);
    if (idx >= 0) {
        m_motionCombo->setCurrentIndex(idx);
    }
    m_blockEmit = false;
}

double SlideshowSettingsDialog::panZoomFactor() const
{
    return m_panZoomFactorSpin ? m_panZoomFactorSpin->value() : 1.12;
}

void SlideshowSettingsDialog::setPanZoomFactor(double factor)
{
    if (!m_panZoomFactorSpin) {
        return;
    }
    m_blockEmit = true;
    m_panZoomFactorSpin->setValue(qBound(1.02, factor, 1.40));
    m_blockEmit = false;
}

int SlideshowSettingsDialog::zoomIndex() const
{
    return m_zoomCombo ? m_zoomCombo->currentData().toInt() : 0;
}

void SlideshowSettingsDialog::setZoomIndex(int index)
{
    if (!m_zoomCombo) {
        return;
    }
    m_blockEmit = true;
    const int idx = m_zoomCombo->findData(index);
    if (idx >= 0) {
        m_zoomCombo->setCurrentIndex(idx);
    }
    m_blockEmit = false;
}
