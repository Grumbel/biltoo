// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"
#include "defaultapps.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
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

    // --- View / Image mode ---
    m_imageModePanCheck = new QCheckBox(tr("Left-drag pans in image mode"), this);
    m_imageModePanCheck->setToolTip(
        tr("When enabled, dragging with the left mouse button pans the image"));
    m_imageModePanCheck->setWhatsThis(
        tr("In image mode, left-drag pans by default. Turn this off to reserve "
           "left-drag for other gestures; pan with Alt+left-drag or the middle "
           "mouse button instead."));

    m_bgPatternCombo = new QComboBox(this);
    m_bgPatternCombo->addItem(tr("Solid"), 0);
    m_bgPatternCombo->addItem(tr("Checkerboard"), 1);
    m_bgPatternCombo->setToolTip(tr("Canvas background fill style"));
    m_bgPatternCombo->setWhatsThis(
        tr("Solid fills the canvas with the primary colour. Checkerboard "
           "alternates primary and secondary colours (useful for transparency)."));

    m_bgColorBtn = new QPushButton(this);
    m_bgColorBtn->setToolTip(tr("Primary background colour"));
    m_bgColorBtn->setMinimumWidth(80);
    connect(m_bgColorBtn, &QPushButton::clicked, this, &PreferencesDialog::chooseBackgroundColor);

    m_bgColorAltBtn = new QPushButton(this);
    m_bgColorAltBtn->setToolTip(tr("Secondary colour for checkerboard pattern"));
    m_bgColorAltBtn->setMinimumWidth(80);
    connect(m_bgColorAltBtn, &QPushButton::clicked, this, &PreferencesDialog::chooseBackgroundColorAlt);

    m_bgCheckerWorkspaceOnlyCheck = new QCheckBox(
        tr("Checkerboard only in Workspace mode"), this);
    m_bgCheckerWorkspaceOnlyCheck->setChecked(true);
    m_bgCheckerWorkspaceOnlyCheck->setToolTip(
        tr("When enabled, Image and Gallery use a solid fill; only Workspace uses the checkerboard"));
    m_bgCheckerWorkspaceOnlyCheck->setWhatsThis(
        tr("Matches the default behaviour: a flat colour behind photos, and a "
           "checkerboard on the free-form workspace so transparency is visible."));

    connect(m_bgPatternCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateBackgroundControlsEnabled(); });

    updateColorButton(m_bgColorBtn, m_bgColor);
    updateColorButton(m_bgColorAltBtn, m_bgColorAlt);
    updateBackgroundControlsEnabled();

    auto *viewForm = new QFormLayout;
    viewForm->setContentsMargins(0, 0, 0, 0);
    viewForm->setHorizontalSpacing(12);
    viewForm->setVerticalSpacing(8);
    viewForm->addRow(QString(), m_imageModePanCheck);
    viewForm->addRow(tr("Background pattern:"), m_bgPatternCombo);
    viewForm->addRow(tr("Background colour:"), m_bgColorBtn);
    viewForm->addRow(tr("Checker colour:"), m_bgColorAltBtn);
    viewForm->addRow(QString(), m_bgCheckerWorkspaceOnlyCheck);

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
    m_setAllBtn = new QPushButton(tr("&Set all as default"), defaultsPage);
    m_removeAllBtn = new QPushButton(tr("&Remove all as default"), defaultsPage);
    m_setAllBtn->setToolTip(tr("Make QImgView the default handler for every listed type"));
    m_removeAllBtn->setToolTip(
        tr("Clear QImgView as the default for every listed type (system default is restored)"));
    btnRow->addWidget(m_setAllBtn);
    btnRow->addWidget(m_removeAllBtn);
    btnRow->addStretch(1);

    defaultsLayout->addWidget(m_mimeStatusLabel);
    defaultsLayout->addWidget(m_mimeTree, 1);
    defaultsLayout->addLayout(btnRow);

    connect(m_mimeTree, &QTreeWidget::itemChanged, this, &PreferencesDialog::onMimeItemChanged);
    connect(m_setAllBtn, &QPushButton::clicked, this, &PreferencesDialog::onSetAllAsDefault);
    connect(m_removeAllBtn, &QPushButton::clicked, this, &PreferencesDialog::onRemoveAllAsDefault);
    refreshDefaultAppsList();

    auto *tabs = new QTabWidget(this);
    tabs->addTab(generalPage, tr("&General"));
    tabs->addTab(defaultsPage, tr("&Default application"));

    // GNOME 2 HIG dialog buttons: Cancel on the left, affirmative (OK) on the
    // right. QDialogButtonBox follows the style hint, which is often Win/KDE
    // order under Fusion; build the row explicitly so order is stable.
    auto *cancelBtn = new QPushButton(tr("&Cancel"), this);
    auto *okBtn = new QPushButton(tr("&OK"), this);
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addWidget(cancelBtn);
    buttonRow->addStretch(1);
    buttonRow->addWidget(okBtn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(tabs, 1);
    layout->addLayout(buttonRow);

    setMinimumWidth(520);
    resize(560, 520);
}

void PreferencesDialog::refreshDefaultAppsList()
{
    m_mimeTreeUpdating = true;
    m_mimeTree->clear();

    if (!DefaultApps::isAvailable()) {
        m_mimeStatusLabel->setText(
            tr("Setting default applications requires GLib GIO, which is not "
               "enabled in this build. QImgView still appears under “Open with” "
               "when its desktop file is installed."));
        m_mimeTree->setEnabled(false);
        m_setAllBtn->setEnabled(false);
        m_removeAllBtn->setEnabled(false);
        m_mimeTreeUpdating = false;
        return;
    }

    m_mimeStatusLabel->setText(
        tr("Checked types are handled by QImgView as the system default. "
           "Toggle a checkbox to set or clear the association immediately. "
           "Uses the FreeDesktop GIO API (qimgview.desktop must be installed)."));
    m_mimeTree->setEnabled(true);
    m_setAllBtn->setEnabled(true);
    m_removeAllBtn->setEnabled(true);

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
        // Checked means QImgView is currently the default.
        item->setCheckState(0, s.isUs ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, s.mimeType);
        if (s.isUs) {
            QFont f = item->font(2);
            f.setBold(true);
            item->setFont(2, f);
        }
    }
    m_mimeTreeUpdating = false;
}

void PreferencesDialog::onMimeItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_mimeTreeUpdating || !item || column != 0) {
        return;
    }
    const QString mime = item->data(0, Qt::UserRole).toString();
    if (mime.isEmpty()) {
        return;
    }

    QString error;
    bool ok = false;
    if (item->checkState(0) == Qt::Checked) {
        ok = DefaultApps::setDefaultForType(mime, &error);
    } else {
        ok = DefaultApps::clearDefaultForType(mime, &error);
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Default application"), error);
    }
    // Always refresh so the "Current default" column and checkbox match reality.
    refreshDefaultAppsList();
}

void PreferencesDialog::onSetAllAsDefault()
{
    QStringList types;
    for (int i = 0; i < m_mimeTree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem *item = m_mimeTree->topLevelItem(i)) {
            types.append(item->data(0, Qt::UserRole).toString());
        }
    }
    if (types.isEmpty()) {
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
    }
}

void PreferencesDialog::onRemoveAllAsDefault()
{
    QStringList types;
    for (int i = 0; i < m_mimeTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_mimeTree->topLevelItem(i);
        // Checked rows are those where QImgView is currently the default.
        if (item && item->checkState(0) == Qt::Checked) {
            types.append(item->data(0, Qt::UserRole).toString());
        }
    }
    if (types.isEmpty()) {
        return;
    }
    QStringList errors;
    const int ok = DefaultApps::clearDefaultForTypes(types, &errors);
    refreshDefaultAppsList();
    if (!errors.isEmpty()) {
        QMessageBox::warning(
            this, tr("Default application"),
            tr("Cleared %1 of %2 types.\n\n%3")
                .arg(ok)
                .arg(types.size())
                .arg(errors.join(QLatin1Char('\n'))));
    }
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

bool PreferencesDialog::imageModeLeftDragPan() const
{
    return m_imageModePanCheck->isChecked();
}

void PreferencesDialog::setImageModeLeftDragPan(bool on)
{
    m_imageModePanCheck->setChecked(on);
}

QColor PreferencesDialog::backgroundColor() const
{
    return m_bgColor;
}

void PreferencesDialog::setBackgroundColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_bgColor = color;
    updateColorButton(m_bgColorBtn, m_bgColor);
}

QColor PreferencesDialog::backgroundColorAlt() const
{
    return m_bgColorAlt;
}

void PreferencesDialog::setBackgroundColorAlt(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_bgColorAlt = color;
    updateColorButton(m_bgColorAltBtn, m_bgColorAlt);
}

int PreferencesDialog::backgroundPatternIndex() const
{
    return m_bgPatternCombo ? m_bgPatternCombo->currentData().toInt() : 0;
}

void PreferencesDialog::setBackgroundPatternIndex(int index)
{
    if (!m_bgPatternCombo) {
        return;
    }
    const int i = m_bgPatternCombo->findData(index);
    if (i >= 0) {
        m_bgPatternCombo->setCurrentIndex(i);
    }
    updateBackgroundControlsEnabled();
}

bool PreferencesDialog::checkerboardWorkspaceOnly() const
{
    return m_bgCheckerWorkspaceOnlyCheck && m_bgCheckerWorkspaceOnlyCheck->isChecked();
}

void PreferencesDialog::setCheckerboardWorkspaceOnly(bool on)
{
    if (m_bgCheckerWorkspaceOnlyCheck) {
        m_bgCheckerWorkspaceOnlyCheck->setChecked(on);
    }
}

void PreferencesDialog::updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button) {
        return;
    }
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; }")
            .arg(color.name(QColor::HexRgb),
                 color.lightness() < 128 ? QStringLiteral("#ffffff")
                                         : QStringLiteral("#000000")));
}

void PreferencesDialog::chooseBackgroundColor()
{
    const QColor c = QColorDialog::getColor(m_bgColor, this, tr("Background colour"));
    if (c.isValid()) {
        setBackgroundColor(c);
    }
}

void PreferencesDialog::chooseBackgroundColorAlt()
{
    const QColor c = QColorDialog::getColor(m_bgColorAlt, this, tr("Checker colour"));
    if (c.isValid()) {
        setBackgroundColorAlt(c);
    }
}

void PreferencesDialog::updateBackgroundControlsEnabled()
{
    const bool checker = backgroundPatternIndex() == 1;
    if (m_bgColorAltBtn) {
        m_bgColorAltBtn->setEnabled(checker);
    }
    if (m_bgCheckerWorkspaceOnlyCheck) {
        m_bgCheckerWorkspaceOnlyCheck->setEnabled(checker);
    }
}
