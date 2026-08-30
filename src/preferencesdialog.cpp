// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
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
    setMinimumWidth(360);

    auto *intro = new QLabel(
        tr("Adjust default behaviour for slideshows, sorting and the workspace."), this);
    intro->setWordWrap(true);

    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(500, 60000);
    m_intervalSpin->setSingleStep(500);
    m_intervalSpin->setSuffix(tr(" ms"));
    m_intervalSpin->setToolTip(tr("Time between slides when the slideshow is running"));
    m_intervalSpin->setWhatsThis(
        tr("How long each image is shown during a slideshow. "
           "Values are in milliseconds (1000 ms = 1 second)."));

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

    m_slideshowFullscreenCheck = new QCheckBox(tr("Start slideshow in fullscreen"), this);
    m_slideshowFullscreenCheck->setToolTip(
        tr("Enter fullscreen automatically when starting a slideshow"));
    m_slideshowFullscreenCheck->setWhatsThis(
        tr("When enabled, Play Slideshow switches to fullscreen for an uncluttered "
           "presentation. Leaving the slideshow does not exit fullscreen; use F11 "
           "or Esc for that."));

    m_masonryWidthSpin = new QSpinBox(this);
    m_masonryWidthSpin->setRange(80, 800);
    m_masonryWidthSpin->setSingleStep(10);
    m_masonryWidthSpin->setSuffix(tr(" px"));
    m_masonryWidthSpin->setValue(240);
    m_masonryWidthSpin->setToolTip(tr(
        "Width of each column in Masonry layout. Images scale to this width; "
        "extra columns appear when the window is wider, and the view scrolls "
        "when content is taller than the window."));

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->addRow(tr("Slideshow interval:"), m_intervalSpin);
    form->addRow(QString(), m_slideshowFullscreenCheck);
    form->addRow(tr("Sort images by:"), m_sortCombo);
    form->addRow(QString(), m_workspaceCheck);
    m_imageModePanCheck = new QCheckBox(tr("Left-drag pans in image mode"), this);
    m_imageModePanCheck->setToolTip(
        tr("When enabled, dragging with the left mouse button pans the image"));
    m_imageModePanCheck->setWhatsThis(
        tr("In image mode, left-drag pans by default. Turn this off to reserve "
           "left-drag for other gestures; pan with Alt+left-drag or the middle "
           "mouse button instead."));

    form->addRow(tr("Masonry column width:"), m_masonryWidthSpin);
    form->addRow(QString(), m_imageModePanCheck);

    // GNOME 2 HIG: Cancel on the left, OK (affirmative) on the right
    auto *cancelBtn = new QPushButton(tr("&Cancel"), this);
    cancelBtn->setAutoDefault(false);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *okBtn = new QPushButton(tr("&OK"), this);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(okBtn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addStretch(1);
    layout->addLayout(buttonRow);
}

int PreferencesDialog::slideshowIntervalMs() const
{
    return m_intervalSpin->value();
}

void PreferencesDialog::setSlideshowIntervalMs(int ms)
{
    m_intervalSpin->setValue(qBound(m_intervalSpin->minimum(), ms, m_intervalSpin->maximum()));
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
