// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/facepanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
    form->addRow(tr("Detect score"), m_threshold);
    m_matchThreshold = new QDoubleSpinBox(opts);
    m_matchThreshold->setRange(0.05, 0.99);
    m_matchThreshold->setSingleStep(0.02);
    m_matchThreshold->setValue(0.36);
    m_matchThreshold->setDecimals(2);
    form->addRow(tr("Match cosine"), m_matchThreshold);
    m_overlay = new QCheckBox(tr("Show overlay on image"), opts);
    m_overlay->setChecked(true);
    form->addRow(m_overlay);
    m_landmarks = new QCheckBox(tr("Show landmarks"), opts);
    m_landmarks->setChecked(true);
    form->addRow(m_landmarks);
    root->addWidget(opts);

    auto *row = new QHBoxLayout;
    m_detectBtn = new QPushButton(tr("Detect / recognize"), this);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    row->addWidget(m_detectBtn);
    row->addWidget(m_clearBtn);
    root->addLayout(row);

    auto *enrollBox = new QGroupBox(tr("Enroll"), this);
    auto *enrollLay = new QVBoxLayout(enrollBox);
    m_name = new QLineEdit(enrollBox);
    m_name->setPlaceholderText(tr("Name for selected face"));
    enrollLay->addWidget(m_name);
    m_enrollBtn = new QPushButton(tr("Enroll selected face"), enrollBox);
    enrollLay->addWidget(m_enrollBtn);
    root->addWidget(enrollBox);

    m_gallery = new QLabel(tr("Gallery: 0 identities"), this);
    m_gallery->setWordWrap(true);
    root->addWidget(m_gallery);

    m_status = new QLabel(tr("Idle"), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_list = new QListWidget(this);
    m_list->setMinimumHeight(120);
    root->addWidget(m_list, 1);

    connect(m_detectBtn, &QPushButton::clicked, this, &FacePanel::detectRequested);
    connect(m_clearBtn, &QPushButton::clicked, this, &FacePanel::clearRequested);
    connect(m_enrollBtn, &QPushButton::clicked, this, &FacePanel::enrollRequested);
    connect(m_threshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { emit scoreThresholdChanged(float(v)); });
    connect(m_matchThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { emit matchThresholdChanged(float(v)); });
    connect(m_overlay, &QCheckBox::toggled, this, &FacePanel::overlayVisibleChanged);
    connect(m_landmarks, &QCheckBox::toggled, this, &FacePanel::showLandmarksChanged);
}

void FacePanel::setBackendSummary(const QString &text) { m_backend->setText(text); }

void FacePanel::setBusy(bool busy)
{
    m_busy = busy;
    m_detectBtn->setEnabled(!busy);
    m_enrollBtn->setEnabled(!busy);
    m_detectBtn->setText(busy ? tr("Working…") : tr("Detect / recognize"));
}

void FacePanel::setStatus(const QString &text) { m_status->setText(text); }

void FacePanel::setFacesSummary(const QStringList &lines)
{
    m_list->clear();
    m_list->addItems(lines);
}

void FacePanel::setGallerySummary(const QString &text) { m_gallery->setText(text); }

float FacePanel::scoreThreshold() const { return float(m_threshold->value()); }

void FacePanel::setScoreThreshold(float t)
{
    QSignalBlocker b(m_threshold);
    m_threshold->setValue(double(t));
}

float FacePanel::matchThreshold() const { return float(m_matchThreshold->value()); }

void FacePanel::setMatchThreshold(float t)
{
    QSignalBlocker b(m_matchThreshold);
    m_matchThreshold->setValue(double(t));
}

bool FacePanel::overlayVisible() const { return m_overlay->isChecked(); }

void FacePanel::setOverlayVisible(bool on)
{
    QSignalBlocker b(m_overlay);
    m_overlay->setChecked(on);
}

bool FacePanel::showLandmarks() const { return m_landmarks->isChecked(); }

void FacePanel::setShowLandmarks(bool on)
{
    QSignalBlocker b(m_landmarks);
    m_landmarks->setChecked(on);
}

QString FacePanel::enrollName() const { return m_name->text(); }

int FacePanel::selectedFaceIndex() const { return m_list->currentRow(); }
