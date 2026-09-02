// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferencesdialog.h"
#include "defaultapps.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
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
    // 0 s = as fast as the machine can advance; milliseconds via three decimals.
    m_intervalSpin->setRange(0.0, 60.0);
    m_intervalSpin->setSingleStep(0.1);
    m_intervalSpin->setDecimals(3);
    m_intervalSpin->setSuffix(tr(" s"));
    m_intervalSpin->setToolTip(tr("Seconds between slides (0 = as fast as possible)"));

    m_slideshowFullscreenCheck = new QCheckBox(tr("Start slideshow in fullscreen"), this);
    m_slideshowFullscreenCheck->setToolTip(
        tr("Enter fullscreen automatically when starting a slideshow"));

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
    m_sortCombo->addItem(tr("File size"), 2);
    m_sortCombo->addItem(tr("Width (pixels)"), 3);
    m_sortCombo->addItem(tr("Height (pixels)"), 4);
    m_sortCombo->addItem(tr("Image size (pixels)"), 5);
    m_sortCombo->setToolTip(tr("Default order when loading a set of images"));

    m_workspaceCheck = new QCheckBox(tr("Start in workspace mode"), this);
    m_workspaceCheck->setToolTip(
        tr("When enabled, QImgView starts with the multi-image workspace active"));

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
        tr("Drag with the left button to pan in Image mode"));

    m_bgPatternCombo = new QComboBox(this);
    m_bgPatternCombo->addItem(tr("Solid"), 0);
    m_bgPatternCombo->addItem(tr("Checkerboard"), 1);
    m_bgPatternCombo->setToolTip(tr("Canvas background fill style"));

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
        tr("Use the checkerboard only on the Workspace canvas"));

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

    // --- HUD overlay appearance ---
    m_hudFontSpin = new QSpinBox(this);
    m_hudFontSpin->setRange(8, 48);
    m_hudFontSpin->setValue(11);
    m_hudFontSpin->setSuffix(tr(" pt"));
    m_hudFontSpin->setToolTip(tr("Point size of the on-image HUD text"));

    m_hudTextColorBtn = new QPushButton(this);
    m_hudTextColorBtn->setMinimumWidth(80);
    m_hudTextColorBtn->setToolTip(tr("HUD text colour"));
    connect(m_hudTextColorBtn, &QPushButton::clicked, this, &PreferencesDialog::chooseHudTextColor);

    m_hudPanelColorBtn = new QPushButton(this);
    m_hudPanelColorBtn->setMinimumWidth(80);
    m_hudPanelColorBtn->setToolTip(tr("HUD panel background colour (supports alpha)"));
    connect(m_hudPanelColorBtn, &QPushButton::clicked, this, &PreferencesDialog::chooseHudPanelColor);

    updateColorButton(m_hudTextColorBtn, m_hudTextColor);
    updateColorButton(m_hudPanelColorBtn, m_hudPanelColor);

    auto *hudForm = new QFormLayout;
    hudForm->setContentsMargins(0, 0, 0, 0);
    hudForm->setHorizontalSpacing(12);
    hudForm->setVerticalSpacing(8);
    hudForm->addRow(tr("Font size:"), m_hudFontSpin);
    hudForm->addRow(tr("Text colour:"), m_hudTextColorBtn);
    hudForm->addRow(tr("Panel colour:"), m_hudPanelColorBtn);

    auto *hudGroup = new QGroupBox(tr("HUD overlay"), this);
    hudGroup->setLayout(hudForm);

    // --- Interface (chrome that is not pure view colours) ---
    m_scrollBarsCheck = new QCheckBox(tr("Show scrollbars"), this);
    m_scrollBarsCheck->setToolTip(tr("Show scrollbars on the image view"));

    m_thumbLabelsCheck = new QCheckBox(tr("Show thumbnail labels"), this);
    m_thumbLabelsCheck->setChecked(true);
    m_thumbLabelsCheck->setToolTip(tr("Show file names under thumbnails"));

    m_thumbPosCombo = new QComboBox(this);
    m_thumbPosCombo->addItem(tr("Bottom"), 0);
    m_thumbPosCombo->addItem(tr("Top"), 1);
    m_thumbPosCombo->addItem(tr("Left"), 2);
    m_thumbPosCombo->addItem(tr("Right"), 3);
    m_thumbPosCombo->setToolTip(tr("Where the thumbnail strip is placed"));

    m_galleryLayoutCombo = new QComboBox(this);
    // Indices match ImageView::LayoutMode (FreeForm=0 is not a Gallery layout).
    m_galleryLayoutCombo->addItem(tr("Horizontal"), 1);
    m_galleryLayoutCombo->addItem(tr("Vertical"), 2);
    m_galleryLayoutCombo->addItem(tr("Grid"), 3);
    // GridCrop (4) omitted — temporarily disabled in the UI.
    m_galleryLayoutCombo->addItem(tr("Masonry"), 5);
    m_galleryLayoutCombo->addItem(tr("Masonry rows"), 6);
    m_galleryLayoutCombo->addItem(tr("Masonry fill"), 7);
    m_galleryLayoutCombo->addItem(tr("Masonry rows fill"), 8);
    m_galleryLayoutCombo->setToolTip(tr("Default Gallery layout for new sessions"));

    auto *ifaceForm = new QFormLayout;
    ifaceForm->setContentsMargins(0, 0, 0, 0);
    ifaceForm->setHorizontalSpacing(12);
    ifaceForm->setVerticalSpacing(8);
    ifaceForm->addRow(QString(), m_scrollBarsCheck);
    ifaceForm->addRow(QString(), m_thumbLabelsCheck);
    ifaceForm->addRow(tr("Thumbnail position:"), m_thumbPosCombo);
    ifaceForm->addRow(tr("Gallery layout:"), m_galleryLayoutCombo);

    auto *ifaceGroup = new QGroupBox(tr("Interface"), this);
    ifaceGroup->setLayout(ifaceForm);

    // --- General tab ---
    auto *generalPage = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(8, 8, 8, 8);
    generalLayout->setSpacing(12);
    generalLayout->addWidget(slideshowGroup);
    generalLayout->addWidget(sessionGroup);
    generalLayout->addWidget(viewGroup);
    generalLayout->addWidget(hudGroup);
    generalLayout->addWidget(ifaceGroup);
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
        tr("Stop using QImgView as the default for these types"));
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

    // GNOME 2 HIG: action buttons grouped on the right — Cancel, then OK.
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(6);
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelBtn);
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
            tr("Default-application settings are not available in this build."));
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
    const double seconds = qBound(0.0, ms / 1000.0, 60.0);
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
    // Show alpha when present (HUD panel); opaque colours stay #RRGGBB.
    const QString label = (color.alpha() < 255)
                              ? color.name(QColor::HexArgb)
                              : color.name(QColor::HexRgb);
    button->setText(label);
    // Stylesheet cannot express alpha reliably; composite over dark grey.
    QColor solid = color;
    if (color.alpha() < 255) {
        const QColor base(60, 60, 60);
        const qreal a = color.alphaF();
        solid.setRgbF(qreal(color.redF()) * a + qreal(base.redF()) * (1.0 - a),
                      qreal(color.greenF()) * a + qreal(base.greenF()) * (1.0 - a),
                      qreal(color.blueF()) * a + qreal(base.blueF()) * (1.0 - a));
        solid.setAlpha(255);
    }
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; }")
            .arg(solid.name(QColor::HexRgb),
                 solid.lightness() < 128 ? QStringLiteral("#ffffff")
                                         : QStringLiteral("#000000")));
}

int PreferencesDialog::hudFontPointSize() const
{
    return m_hudFontSpin ? m_hudFontSpin->value() : 11;
}

void PreferencesDialog::setHudFontPointSize(int pt)
{
    if (m_hudFontSpin) {
        m_hudFontSpin->setValue(qBound(8, pt, 48));
    }
}

QColor PreferencesDialog::hudTextColor() const
{
    return m_hudTextColor;
}

void PreferencesDialog::setHudTextColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_hudTextColor = color;
    updateColorButton(m_hudTextColorBtn, m_hudTextColor);
}

QColor PreferencesDialog::hudPanelColor() const
{
    return m_hudPanelColor;
}

void PreferencesDialog::setHudPanelColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_hudPanelColor = color;
    updateColorButton(m_hudPanelColorBtn, m_hudPanelColor);
}

void PreferencesDialog::chooseHudTextColor()
{
    const QColor c = QColorDialog::getColor(m_hudTextColor, this, tr("HUD text colour"));
    if (c.isValid()) {
        setHudTextColor(c);
    }
}

void PreferencesDialog::chooseHudPanelColor()
{
    const QColor c = QColorDialog::getColor(m_hudPanelColor, this, tr("HUD panel colour"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        setHudPanelColor(c);
    }
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

bool PreferencesDialog::scrollBarsVisible() const
{
    return m_scrollBarsCheck && m_scrollBarsCheck->isChecked();
}

void PreferencesDialog::setScrollBarsVisible(bool on)
{
    if (m_scrollBarsCheck) {
        m_scrollBarsCheck->setChecked(on);
    }
}

bool PreferencesDialog::thumbnailLabelsVisible() const
{
    return m_thumbLabelsCheck && m_thumbLabelsCheck->isChecked();
}

void PreferencesDialog::setThumbnailLabelsVisible(bool on)
{
    if (m_thumbLabelsCheck) {
        m_thumbLabelsCheck->setChecked(on);
    }
}

int PreferencesDialog::thumbnailPositionIndex() const
{
    return m_thumbPosCombo ? m_thumbPosCombo->currentIndex() : 0;
}

void PreferencesDialog::setThumbnailPositionIndex(int index)
{
    if (m_thumbPosCombo && index >= 0 && index < m_thumbPosCombo->count()) {
        m_thumbPosCombo->setCurrentIndex(index);
    }
}

int PreferencesDialog::defaultGalleryLayoutMode() const
{
    if (!m_galleryLayoutCombo) {
        return 5; // Masonry
    }
    return m_galleryLayoutCombo->currentData().toInt();
}

void PreferencesDialog::setDefaultGalleryLayoutMode(int layoutMode)
{
    if (!m_galleryLayoutCombo) {
        return;
    }
    const int idx = m_galleryLayoutCombo->findData(layoutMode);
    if (idx >= 0) {
        m_galleryLayoutCombo->setCurrentIndex(idx);
    }
}
