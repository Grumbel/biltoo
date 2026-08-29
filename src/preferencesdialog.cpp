// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setModal(true);

    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(500, 60000);
    m_intervalSpin->setSingleStep(500);
    m_intervalSpin->setSuffix(tr(" ms"));
    m_intervalSpin->setToolTip(tr("Time between slides when the slideshow is running"));

    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem(tr("Name (natural)"), 0);
    m_sortCombo->addItem(tr("Modification time"), 1);
    m_sortCombo->setToolTip(tr("Default order when loading a set of images"));

    auto *form = new QFormLayout;
    form->addRow(tr("Slideshow interval:"), m_intervalSpin);
    form->addRow(tr("Sort images by:"), m_sortCombo);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

int PreferencesDialog::slideshowIntervalMs() const
{
    return m_intervalSpin->value();
}

void PreferencesDialog::setSlideshowIntervalMs(int ms)
{
    m_intervalSpin->setValue(ms);
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
