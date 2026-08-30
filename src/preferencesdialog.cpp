// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"
#include "defaultapps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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

    // --- General tab (slideshow / session / view) ---
    auto *generalPage = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(8, 8, 8, 8);
    generalLayout->setSpacing(12);
    generalLayout->addWidget(slideshowGroup);
    generalLayout->addWidget(sessionGroup);
    generalLayout->addWidget(viewGroup);
    generalLayout->addStretch(1);

    // --- Default application tab (GIO) ---
    auto *defaultsPage = new QWidget(this);
    auto *defaultsLayout = new QVBoxLayout(defaultsPage);
    defaultsLayout->setContentsMargins(8, 8, 8, 8);
    defaultsLayout->setSpacing(8);

    m_mimeStatusLabel = new QLabel(defaultsPage);
    m_mimeStatusLabel->setWordWrap(true);

    m_mimeTree = new QTreeWidget(defaultsPage);
    m_mimeTree->setColumnCount(3);
    m_mimeTree->setHeaderLabels({tr("Type"), tr("MIME"), tr("Current default")});
    m_mimeTree->setRootIsDecorated(false);
    m_mimeTree->setUniformRowHeights(true);
    m_mimeTree->setMinimumHeight(160);
    m_mimeTree->header()->setStretchLastSection(true);
    m_mimeTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_mimeTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto *btnRow = new QHBoxLayout;
    m_setCheckedBtn = new QPushButton(tr("Set as default for &checked"), defaultsPage);
    m_setAllBtn = new QPushButton(tr("Set as default for &all listed"), defaultsPage);
    btnRow->addWidget(m_setCheckedBtn);
    btnRow->addWidget(m_setAllBtn);
    btnRow->addStretch(1);

    defaultsLayout->addWidget(m_mimeStatusLabel);
    defaultsLayout->addWidget(m_mimeTree, 1);
    defaultsLayout->addLayout(btnRow);

    connect(m_setCheckedBtn, &QPushButton::clicked, this, &PreferencesDialog::onSetDefaultForChecked);
    connect(m_setAllBtn, &QPushButton::clicked, this, &PreferencesDialog::onSetDefaultForAll);
    refreshDefaultAppsList();

    auto *tabs = new QTabWidget(this);
    tabs->addTab(generalPage, tr("&General"));
    tabs->addTab(defaultsPage, tr("&Default application"));

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
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);

    setMinimumWidth(520);
    resize(560, 520);
}

void PreferencesDialog::refreshDefaultAppsList()
{
    m_mimeTree->clear();

    if (!DefaultApps::isAvailable()) {
        m_mimeStatusLabel->setText(
            tr("Setting default applications requires GLib GIO, which is not "
               "enabled in this build. QImgView still appears under “Open with” "
               "when its desktop file is installed."));
        m_mimeTree->setEnabled(false);
        m_setCheckedBtn->setEnabled(false);
        m_setAllBtn->setEnabled(false);
        return;
    }

    m_mimeStatusLabel->setText(
        tr("Choose image types for which QImgView should be the system default. "
           "Uses the FreeDesktop GIO API (qimgview.desktop must be installed)."));
    m_mimeTree->setEnabled(true);
    m_setCheckedBtn->setEnabled(true);
    m_setAllBtn->setEnabled(true);

    for (const DefaultApps::MimeStatus &s : DefaultApps::statusForSupportedTypes()) {
        auto *item = new QTreeWidgetItem(m_mimeTree);
        item->setText(0, s.label);
        item->setText(1, s.mimeType);
        item->setText(2, s.isUs
                              ? tr("QImgView")
                              : (s.currentAppName.isEmpty()
                                     ? tr("(none)")
                                     : s.currentAppName));
        item->setToolTip(2, s.currentAppId.isEmpty() ? s.currentAppName : s.currentAppId);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, s.isUs ? Qt::Unchecked : Qt::Checked);
        item->setData(0, Qt::UserRole, s.mimeType);
        if (s.isUs) {
            QFont f = item->font(2);
            f.setBold(true);
            item->setFont(2, f);
        }
    }
}

void PreferencesDialog::onSetDefaultForChecked()
{
    QStringList types;
    for (int i = 0; i < m_mimeTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_mimeTree->topLevelItem(i);
        if (item && item->checkState(0) == Qt::Checked) {
            types.append(item->data(0, Qt::UserRole).toString());
        }
    }
    if (types.isEmpty()) {
        QMessageBox::information(this, tr("Default application"),
                                 tr("Check one or more types first."));
        return;
    }
    QStringList errors;
    const int ok = DefaultApps::setDefaultForTypes(types, &errors);
    refreshDefaultAppsList();
    if (!errors.isEmpty()) {
        QMessageBox::warning(
            this, tr("Default application"),
            tr("Updated %1 of %2 types.\n\n%3")
                .arg(ok)
                .arg(types.size())
                .arg(errors.join(QLatin1Char('\n'))));
    } else {
        QMessageBox::information(
            this, tr("Default application"),
            tr("QImgView is now the default for %n type(s).", nullptr, ok));
    }
}

void PreferencesDialog::onSetDefaultForAll()
{
    for (int i = 0; i < m_mimeTree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem *item = m_mimeTree->topLevelItem(i)) {
            item->setCheckState(0, Qt::Checked);
        }
    }
    onSetDefaultForChecked();
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
