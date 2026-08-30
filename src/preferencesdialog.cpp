// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setModal(true);
    setMinimumWidth(400);

    // --- Slideshow ---
    m_intervalSpin = new QDoubleSpinBox(this);
    m_intervalSpin->setRange(0.5, 60.0);
    m_intervalSpin->setSingleStep(0.5);
    m_intervalSpin->setDecimals(1);
    m_intervalSpin->setSuffix(tr(" s"));
    m_intervalSpin->setToolTip(tr("Time between slides when the slideshow is running"));
    m_intervalSpin->setWhatsThis(
        tr("How long each image is shown during a slideshow, in seconds."));

    m_slideshowFullscreenCheck = new QCheckBox(tr("Start slideshow in fullscreen"), this);
    m_slideshowFullscreenCheck->setToolTip(
        tr("Enter fullscreen automatically when starting a slideshow"));
    m_slideshowFullscreenCheck->setWhatsThis(
        tr("When enabled, Play Slideshow switches to fullscreen for an uncluttered "
           "presentation. Leaving the slideshow does not exit fullscreen; use F11 "
           "or Esc for that."));

    auto *slideshowForm = new QFormLayout;
    slideshowForm->setContentsMargins(0, 0, 0, 0);
    slideshowForm->setHorizontalSpacing(12);
    slideshowForm->setVerticalSpacing(8);
    slideshowForm->addRow(tr("Interval:"), m_intervalSpin);
    slideshowForm->addRow(QString(), m_slideshowFullscreenCheck);

    auto *slideshowGroup = new QGroupBox(tr("Slideshow"), this);
    slideshowGroup->setLayout(slideshowForm);

    // --- Session ---
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem(tr("Name (natural)"), 0);
    m_sortCombo->addItem(tr("Modification time"), 1);
    m_sortCombo->setToolTip(tr("Default order when loading a set of images"));
    m_sortCombo->setWhatsThis(
        tr("Name (natural) sorts img2 before img10. "
           "Modification time orders files by when they were last changed."));

    m_workspaceCheck = new QCheckBox(tr("Start in workspace mode"), this);
    m_workspaceCheck->setToolTip(
        tr("When enabled, QImgView starts with the multi-image workspace active"));
    m_workspaceCheck->setWhatsThis(
        tr("Workspace mode is off by default. Turn this on only if you usually "
           "compare several images at once. You can still toggle workspace mode "
           "from the toolbar or View menu during a session."));

    auto *sessionForm = new QFormLayout;
    sessionForm->setContentsMargins(0, 0, 0, 0);
    sessionForm->setHorizontalSpacing(12);
    sessionForm->setVerticalSpacing(8);
    sessionForm->addRow(tr("Sort images by:"), m_sortCombo);
    sessionForm->addRow(QString(), m_workspaceCheck);

    auto *sessionGroup = new QGroupBox(tr("Session"), this);
    sessionGroup->setLayout(sessionForm);

    // --- Workspace / Image mode ---
    m_masonryWidthSpin = new QSpinBox(this);
    m_masonryWidthSpin->setRange(80, 800);
    m_masonryWidthSpin->setSingleStep(10);
    m_masonryWidthSpin->setSuffix(tr(" px"));
    m_masonryWidthSpin->setValue(240);
    m_masonryWidthSpin->setToolTip(tr(
        "Width of each column in Masonry layout. Images scale to this width."));
    m_masonryWidthSpin->setWhatsThis(tr(
        "Width of each column in Masonry layout. Images scale to this width; "
        "extra columns appear when the window is wider."));

    m_imageModePanCheck = new QCheckBox(tr("Left-drag pans in image mode"), this);
    m_imageModePanCheck->setToolTip(
        tr("When enabled, dragging with the left mouse button pans the image"));
    m_imageModePanCheck->setWhatsThis(
        tr("In image mode, left-drag pans by default. Turn this off to reserve "
           "left-drag for other gestures; pan with Alt+left-drag or the middle "
           "mouse button instead."));

    auto *viewForm = new QFormLayout;
    viewForm->setContentsMargins(0, 0, 0, 0);
    viewForm->setHorizontalSpacing(12);
    viewForm->setVerticalSpacing(8);
    viewForm->addRow(tr("Masonry column width:"), m_masonryWidthSpin);
    viewForm->addRow(QString(), m_imageModePanCheck);

    auto *viewGroup = new QGroupBox(tr("View"), this);
    viewGroup->setLayout(viewForm);

    // GNOME 2 HIG: Cancel left, OK right via QDialogButtonBox::GtkLayout-like order
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("&Cancel"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(slideshowGroup);
    layout->addWidget(sessionGroup);
    layout->addWidget(viewGroup);
    layout->addStretch(1);
    layout->addWidget(buttons);
}

int PreferencesDialog::slideshowIntervalMs() const
{
    return qRound(m_intervalSpin->value() * 1000.0);
}

void PreferencesDialog::setSlideshowIntervalMs(int ms)
{
    const double seconds = qBound(0.5, ms / 1000.0, 60.0);
    m_intervalSpin->setValue(seconds);
}

int PreferencesDialog::sortModeIndex() const
{
    return m_sortCombo->currentData().toInt();
}

void PreferencesDialog::setSortModeIndex(int index)
{
    const int i = m_sortCombo->findData(index);
    if (i >= 0) {
        m_sortCombo->setCurrentIndex(i);
    }
}

bool PreferencesDialog::startInWorkspaceMode() const
{
    return m_workspaceCheck->isChecked();
}

void PreferencesDialog::setStartInWorkspaceMode(bool on)
{
    m_workspaceCheck->setChecked(on);
}

bool PreferencesDialog::slideshowFullscreen() const
{
    return m_slideshowFullscreenCheck->isChecked();
}

void PreferencesDialog::setSlideshowFullscreen(bool on)
{
    m_slideshowFullscreenCheck->setChecked(on);
}

int PreferencesDialog::masonryColumnWidth() const
{
    return m_masonryWidthSpin->value();
}

void PreferencesDialog::setMasonryColumnWidth(int pixels)
{
    m_masonryWidthSpin->setValue(qBound(m_masonryWidthSpin->minimum(), pixels,
                                       m_masonryWidthSpin->maximum()));
}

bool PreferencesDialog::imageModeLeftDragPan() const
{
    return m_imageModePanCheck->isChecked();
}

void PreferencesDialog::setImageModeLeftDragPan(bool on)
{
    m_imageModePanCheck->setChecked(on);
}
