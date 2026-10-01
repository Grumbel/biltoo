// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/facepanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

FacePanel::FacePanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void FacePanel::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_backend = new QLabel(tr("Backend: —"), this);
    m_backend->setWordWrap(true);
    root->addWidget(m_backend);

    auto *opts = new QGroupBox(tr("Detection"), this);
    auto *form = new QFormLayout(opts);
    m_threshold = new QDoubleSpinBox(opts);
    m_threshold->setRange(0.05, 0.99);
    m_threshold->setSingleStep(0.05);
    m_threshold->setValue(0.60);
    m_threshold->setDecimals(2);
    form->addRow(tr("Score threshold"), m_threshold);
    m_overlay = new QCheckBox(tr("Show overlay on image"), opts);
    m_overlay->setChecked(true);
    form->addRow(m_overlay);
    m_landmarks = new QCheckBox(tr("Show landmarks"), opts);
    m_landmarks->setChecked(true);
    form->addRow(m_landmarks);
    root->addWidget(opts);

    auto *row = new QHBoxLayout;
    m_detectBtn = new QPushButton(tr("Detect faces"), this);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    row->addWidget(m_detectBtn);
    row->addWidget(m_clearBtn);
    root->addLayout(row);

    m_status = new QLabel(tr("Idle"), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_list = new QListWidget(this);
    m_list->setMinimumHeight(120);
    root->addWidget(m_list, 1);

    connect(m_detectBtn, &QPushButton::clicked, this, &FacePanel::detectRequested);
    connect(m_clearBtn, &QPushButton::clicked, this, &FacePanel::clearRequested);
    connect(m_threshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { emit scoreThresholdChanged(float(v)); });
    connect(m_overlay, &QCheckBox::toggled, this, &FacePanel::overlayVisibleChanged);
    connect(m_landmarks, &QCheckBox::toggled, this, &FacePanel::showLandmarksChanged);
}

void FacePanel::setBackendSummary(const QString &text)
{
    m_backend->setText(text);
}

void FacePanel::setBusy(bool busy)
{
    m_busy = busy;
    m_detectBtn->setEnabled(!busy);
    m_detectBtn->setText(busy ? tr("Detecting…") : tr("Detect faces"));
}

void FacePanel::setStatus(const QString &text)
{
    m_status->setText(text);
}

void FacePanel::setFacesSummary(const QStringList &lines)
{
    m_list->clear();
    m_list->addItems(lines);
}

float FacePanel::scoreThreshold() const
{
    return float(m_threshold->value());
}

void FacePanel::setScoreThreshold(float t)
{
    QSignalBlocker b(m_threshold);
    m_threshold->setValue(double(t));
}

bool FacePanel::overlayVisible() const
{
    return m_overlay->isChecked();
}

void FacePanel::setOverlayVisible(bool on)
{
    QSignalBlocker b(m_overlay);
    m_overlay->setChecked(on);
}

bool FacePanel::showLandmarks() const
{
    return m_landmarks->isChecked();
}

void FacePanel::setShowLandmarks(bool on)
{
    QSignalBlocker b(m_landmarks);
    m_landmarks->setChecked(on);
}
