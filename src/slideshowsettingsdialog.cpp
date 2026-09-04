// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowsettingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
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

    m_transitionMsSpin = new QSpinBox(this);
    m_transitionMsSpin->setRange(0, 5000);
    m_transitionMsSpin->setSingleStep(50);
    m_transitionMsSpin->setSuffix(tr(" ms"));
    m_transitionMsSpin->setToolTip(tr("Duration of the transition (0 = instant)"));

    m_motionCombo = new QComboBox(this);
    m_motionCombo->addItem(tr("Off"), 0);
    m_motionCombo->addItem(tr("Pan and zoom"), 1);
    m_motionCombo->addItem(tr("Pan and scan"), 2);
    m_motionCombo->setToolTip(
        tr("Pan and zoom: slowly zoom in while panning (Ken Burns).\n"
           "Pan and scan: pan across the full width or height (no zoom).\n"
           "Motion uses Fill framing; Slideshow zoom applies when motion is Off."));

    m_panZoomFactorSpin = new QDoubleSpinBox(this);
    m_panZoomFactorSpin->setRange(1.02, 1.40);
    m_panZoomFactorSpin->setSingleStep(0.01);
    m_panZoomFactorSpin->setDecimals(2);
    m_panZoomFactorSpin->setToolTip(
        tr("Pan and zoom only: end scale relative to cover framing"));

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->addItem(tr("Fit"), 0);
    m_zoomCombo->addItem(tr("Fill"), 1);
    m_zoomCombo->addItem(tr("1:1"), 2);
    m_zoomCombo->setToolTip(
        tr("How each slide is framed when dwell motion is Off.\n"
           "Fit: whole image visible.\n"
           "Fill: cover the window (may crop).\n"
           "1:1: native pixels, centred.\n"
           "When motion is On, the camera always uses Fill."));

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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    root->addLayout(form);
    root->addWidget(buttons);

    setMinimumWidth(360);
}

int SlideshowSettingsDialog::intervalMs() const
{
    return m_intervalSpin ? int(m_intervalSpin->value() * 1000.0 + 0.5) : 3000;
}

void SlideshowSettingsDialog::setIntervalMs(int ms)
{
    if (m_intervalSpin) {
        m_intervalSpin->setValue(qMax(0.5, ms / 1000.0));
    }
}

bool SlideshowSettingsDialog::startFullscreen() const
{
    return m_fullscreenCheck && m_fullscreenCheck->isChecked();
}

void SlideshowSettingsDialog::setStartFullscreen(bool on)
{
    if (m_fullscreenCheck) {
        m_fullscreenCheck->setChecked(on);
    }
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
    const int idx = m_transitionCombo->findData(index);
    if (idx >= 0) {
        m_transitionCombo->setCurrentIndex(idx);
    }
}

int SlideshowSettingsDialog::transitionDurationMs() const
{
    return m_transitionMsSpin ? m_transitionMsSpin->value() : 400;
}

void SlideshowSettingsDialog::setTransitionDurationMs(int ms)
{
    if (m_transitionMsSpin) {
        m_transitionMsSpin->setValue(qBound(0, ms, 5000));
    }
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
    const int idx = m_motionCombo->findData(index);
    if (idx >= 0) {
        m_motionCombo->setCurrentIndex(idx);
    }
}

double SlideshowSettingsDialog::panZoomFactor() const
{
    return m_panZoomFactorSpin ? m_panZoomFactorSpin->value() : 1.12;
}

void SlideshowSettingsDialog::setPanZoomFactor(double factor)
{
    if (m_panZoomFactorSpin) {
        m_panZoomFactorSpin->setValue(qBound(1.02, factor, 1.40));
    }
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
    const int idx = m_zoomCombo->findData(index);
    if (idx >= 0) {
        m_zoomCombo->setCurrentIndex(idx);
    }
}
