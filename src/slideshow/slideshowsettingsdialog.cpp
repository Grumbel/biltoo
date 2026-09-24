// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshow/slideshowsettingsdialog.h"
#include "slideshow/slideshowclocks.h"
#include "view/viewtransform.h"
#include "slideshow/slideshowmotiongeometry.h"
#include "shell/icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Match Preferences / MainWindow::readSettings fallbacks.
constexpr int kDefaultIntervalMs = 3000;
constexpr bool kDefaultFullscreen = true;
constexpr bool kDefaultLoop = true;
constexpr int kDefaultTransition = 1; // Crossfade
constexpr int kDefaultTransitionMs = 400;
constexpr int kDefaultMotion = 0; // Off
constexpr double kDefaultPanZoomFactor = 1.12;
constexpr int kDefaultZoom = 0; // Fit
constexpr int kDefaultLetterbox = 0; // App background
const QColor kDefaultPadColor(42, 42, 42);

} // namespace

SlideshowSettingsDialog::SlideshowSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Slideshow Settings"));
    setModal(true);

    m_intervalSpin = new QDoubleSpinBox(this);
    m_intervalSpin->setRange(0.0, 3600.0);
    m_intervalSpin->setDecimals(3);
    m_intervalSpin->setSingleStep(0.05);
    m_intervalSpin->setSuffix(tr(" s"));
    m_intervalSpin->setToolTip(
        tr("Time each image stays on screen (dwell). 0 = as fast as possible."));

    m_fullscreenCheck = new QCheckBox(tr("Start slideshow in fullscreen"), this);
    m_fullscreenCheck->setToolTip(
        tr("Enter fullscreen automatically when starting a slideshow"));

    m_loopCheck = new QCheckBox(tr("Loop slideshow"), this);
    m_loopCheck->setToolTip(
        tr("When enabled, advance from the last image back to the first.\n"
           "When disabled, the slideshow stops after the last image."));
    m_loopCheck->setChecked(kDefaultLoop);

    m_transitionCombo = new QComboBox(this);
    m_transitionCombo->addItem(tr("None"), 0);
    m_transitionCombo->addItem(tr("Crossfade"), 1);
    m_transitionCombo->addItem(tr("Fade through black"), 2);
    m_transitionCombo->addItem(tr("Slide (projector)"), 3);
    m_transitionCombo->setToolTip(tr("Effect used when advancing to the next image"));

    m_transitionMsSpin = new QDoubleSpinBox(this);
    m_transitionMsSpin->setRange(0.0, 3600.0);
    m_transitionMsSpin->setSingleStep(0.05);
    m_transitionMsSpin->setDecimals(3);
    m_transitionMsSpin->setSuffix(tr(" s"));
    m_transitionMsSpin->setToolTip(
        tr("Duration of the full transition (outgoing + incoming; capped to the interval)"));

    m_motionCombo = new QComboBox(this);
    m_motionCombo->addItem(tr("Off"), 0);
    m_motionCombo->addItem(tr("Pan and zoom"), 1);
    m_motionCombo->addItem(tr("Pan and scan"), 2);
    m_motionCombo->setToolTip(
        tr("Pan and zoom: slowly zoom while panning.\n"
           "Pan and scan: pan across the image (no zoom)."));

    m_panZoomFactorSpin = new QDoubleSpinBox(this);
    // Wide range — no artificial 1.02…1.40 ceiling; clamp only rejects ≤0 / non-finite.
    m_panZoomFactorSpin->setRange(0.01, 1000.0);
    m_panZoomFactorSpin->setDecimals(3);
    m_panZoomFactorSpin->setSingleStep(0.01);
    m_panZoomFactorSpin->setValue(kDefaultPanZoomFactor);
    m_panZoomFactorSpin->setToolTip(
        tr("End scale relative to the start of the dwell (1 = no zoom change).\n"
           "Values above 1 zoom in; below 1 zoom out."));

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->addItem(tr("Fit"), 0);
    m_zoomCombo->addItem(tr("Fill"), 1);
    m_zoomCombo->addItem(tr("1:1"), 2);
    m_zoomCombo->setToolTip(tr("Base zoom for each slide before dwell motion"));

    m_letterboxCombo = new QComboBox(this);
    m_letterboxCombo->addItem(tr("App background"), 0);
    m_letterboxCombo->addItem(tr("Solid colour"), 1);
    m_letterboxCombo->addItem(tr("Zoom and blur"), 2);
    m_letterboxCombo->setToolTip(
        tr("When the image does not cover the window (Fit / 1:1):\n"
           "App background: Preferences canvas colour.\n"
           "Solid colour: dedicated pad colour.\n"
           "Zoom and blur: scaled, blurred image fill."));

    m_padColorBtn = new QPushButton(this);
    m_padColorBtn->setToolTip(tr("Colour for Solid letterbox fill"));
    styleColorButton(m_padColorBtn, m_padColor);
    connect(m_padColorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_padColor, this, tr("Letterbox colour"));
        if (c.isValid()) {
            setPadColor(c);
            updateResetButtons();
            emitChanged();
        }
    });

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    form->addRow(tr("Interval:"),
                 wrapWithReset(m_intervalSpin, &m_resetIntervalBtn, [this]() {
                     setIntervalMs(kDefaultIntervalMs);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(QString(),
                 wrapWithReset(m_fullscreenCheck, &m_resetFullscreenBtn, [this]() {
                     setStartFullscreen(kDefaultFullscreen);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(QString(),
                 wrapWithReset(m_loopCheck, &m_resetLoopBtn, [this]() {
                     setLoop(kDefaultLoop);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Transition:"),
                 wrapWithReset(m_transitionCombo, &m_resetTransitionBtn, [this]() {
                     setTransitionIndex(kDefaultTransition);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Transition duration:"),
                 wrapWithReset(m_transitionMsSpin, &m_resetTransitionMsBtn, [this]() {
                     setTransitionDurationMs(kDefaultTransitionMs);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Dwell motion:"),
                 wrapWithReset(m_motionCombo, &m_resetMotionBtn, [this]() {
                     setMotionIndex(kDefaultMotion);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Pan and zoom factor:"),
                 wrapWithReset(m_panZoomFactorSpin, &m_resetPanZoomFactorBtn, [this]() {
                     setPanZoomFactor(kDefaultPanZoomFactor);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Zoom:"),
                 wrapWithReset(m_zoomCombo, &m_resetZoomBtn, [this]() {
                     setZoomIndex(kDefaultZoom);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Letterbox fill:"),
                 wrapWithReset(m_letterboxCombo, &m_resetLetterboxBtn, [this]() {
                     setLetterboxFillIndex(kDefaultLetterbox);
                     updateResetButtons();
                     emitChanged();
                 }));
    form->addRow(tr("Letterbox colour:"),
                 wrapWithReset(m_padColorBtn, &m_resetPadColorBtn, [this]() {
                     setPadColor(kDefaultPadColor);
                     updateResetButtons();
                     emitChanged();
                 }));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    // Close button is RejectRole in some styles; also wire clicked Close.
    if (auto *closeBtn = buttons->button(QDialogButtonBox::Close)) {
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(m_intervalSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                syncTransitionCap();
                updateResetButtons();
                emitChanged();
            });
    connect(m_fullscreenCheck, &QCheckBox::toggled, this, [this](bool) {
        updateResetButtons();
        emitChanged();
    });
    connect(m_loopCheck, &QCheckBox::toggled, this, [this](bool) {
        updateResetButtons();
        emitChanged();
    });
    connect(m_transitionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateResetButtons();
                emitChanged();
            });
    connect(m_transitionMsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                updateResetButtons();
                emitChanged();
            });
    connect(m_motionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateResetButtons();
                emitChanged();
            });
    connect(m_panZoomFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                updateResetButtons();
                emitChanged();
            });
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateResetButtons();
                emitChanged();
            });
    connect(m_letterboxCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateLetterboxControls();
                updateResetButtons();
                emitChanged();
            });

    updateLetterboxControls();
    updateResetButtons();
    setMinimumWidth(360);
}

QWidget *SlideshowSettingsDialog::wrapWithReset(QWidget *field, QToolButton **resetBtnOut,
                                                const std::function<void()> &resetFn)
{
    auto *row = new QWidget(this);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    field->setParent(row);
    lay->addWidget(field, 1);

    auto *btn = new QToolButton(row);
    btn->setAutoRaise(true);
    btn->setIcon(themeIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload));
    btn->setToolTip(tr("Reset to default"));
    btn->setAccessibleName(tr("Reset to default"));
    btn->setFocusPolicy(Qt::TabFocus);
    const int side = qMax(20, field->sizeHint().height());
    btn->setFixedSize(side, side);
    connect(btn, &QToolButton::clicked, this, [resetFn]() { resetFn(); });
    lay->addWidget(btn, 0, Qt::AlignVCenter);
    if (resetBtnOut) {
        *resetBtnOut = btn;
    }
    return row;
}

void SlideshowSettingsDialog::updateResetButtons()
{
    auto setOn = [](QToolButton *btn, bool differs) {
        if (btn) {
            btn->setEnabled(differs);
        }
    };
    setOn(m_resetIntervalBtn, intervalMs() != kDefaultIntervalMs);
    setOn(m_resetFullscreenBtn, startFullscreen() != kDefaultFullscreen);
    setOn(m_resetLoopBtn, loop() != kDefaultLoop);
    setOn(m_resetTransitionBtn, transitionIndex() != kDefaultTransition);
    setOn(m_resetTransitionMsBtn, transitionDurationMs() != kDefaultTransitionMs);
    setOn(m_resetMotionBtn, motionIndex() != kDefaultMotion);
    setOn(m_resetPanZoomFactorBtn,
          !qFuzzyCompare(panZoomFactor(), kDefaultPanZoomFactor));
    setOn(m_resetZoomBtn, zoomIndex() != kDefaultZoom);
    setOn(m_resetLetterboxBtn, letterboxFillIndex() != kDefaultLetterbox);
    setOn(m_resetPadColorBtn, padColor() != kDefaultPadColor);
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
    const double intervalSec = m_intervalSpin->value();
    const double capSec = ViewTransform::nonNeg(intervalSec);
    m_blockEmit = true;
    m_transitionMsSpin->setMaximum(capSec);
    if (m_transitionMsSpin->value() > capSec) {
        m_transitionMsSpin->setValue(capSec);
    }
    m_blockEmit = false;
}

int SlideshowSettingsDialog::intervalMs() const
{
    return m_intervalSpin ? SlideshowClocks::secondsToMs(m_intervalSpin->value()) : 3000;
}

void SlideshowSettingsDialog::setIntervalMs(int ms)
{
    if (!m_intervalSpin) {
        return;
    }
    m_blockEmit = true;
    m_intervalSpin->setValue(SlideshowClocks::msToSeconds(ms));
    m_blockEmit = false;
    syncTransitionCap();
    updateResetButtons();
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
    updateResetButtons();
}

bool SlideshowSettingsDialog::loop() const
{
    return m_loopCheck && m_loopCheck->isChecked();
}

void SlideshowSettingsDialog::setLoop(bool on)
{
    if (!m_loopCheck) {
        return;
    }
    m_blockEmit = true;
    m_loopCheck->setChecked(on);
    m_blockEmit = false;
    updateResetButtons();
}

int SlideshowSettingsDialog::transitionIndex() const
{
    return m_transitionCombo ? m_transitionCombo->currentData().toInt() : 0;
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
    updateResetButtons();
}

int SlideshowSettingsDialog::transitionDurationMs() const
{
    return m_transitionMsSpin ? SlideshowClocks::secondsToMs(m_transitionMsSpin->value())
                              : 400;
}

void SlideshowSettingsDialog::setTransitionDurationMs(int ms)
{
    if (!m_transitionMsSpin) {
        return;
    }
    m_blockEmit = true;
    syncTransitionCap();
    const double capSec = m_transitionMsSpin->maximum();
    m_transitionMsSpin->setValue(SlideshowClocks::msToSeconds(ms, capSec));
    m_blockEmit = false;
    updateResetButtons();
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
    updateResetButtons();
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
    m_panZoomFactorSpin->setValue(SlideshowMotionGeometry::clampPanZoomFactor(factor));
    m_blockEmit = false;
    updateResetButtons();
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
    updateResetButtons();
}

int SlideshowSettingsDialog::letterboxFillIndex() const
{
    return m_letterboxCombo ? m_letterboxCombo->currentData().toInt() : 0;
}

void SlideshowSettingsDialog::setLetterboxFillIndex(int index)
{
    if (!m_letterboxCombo) {
        return;
    }
    m_blockEmit = true;
    const int idx = m_letterboxCombo->findData(index);
    if (idx >= 0) {
        m_letterboxCombo->setCurrentIndex(idx);
    }
    m_blockEmit = false;
    updateLetterboxControls();
    updateResetButtons();
}

QColor SlideshowSettingsDialog::padColor() const
{
    return m_padColor;
}

void SlideshowSettingsDialog::setPadColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    m_padColor = color;
    styleColorButton(m_padColorBtn, m_padColor);
    updateResetButtons();
}

void SlideshowSettingsDialog::updateLetterboxControls()
{
    const bool solid = letterboxFillIndex() == 1;
    if (m_padColorBtn) {
        m_padColorBtn->setEnabled(solid);
    }
}

void SlideshowSettingsDialog::styleColorButton(QPushButton *btn, const QColor &color)
{
    if (!btn) {
        return;
    }
    btn->setText(color.name(QColor::HexRgb));
    const QString bg = color.name(QColor::HexRgb);
    const QColor fg = (color.lightness() > 140) ? QColor(Qt::black) : QColor(Qt::white);
    btn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; padding: 4px 10px; }")
            .arg(bg, fg.name(QColor::HexRgb)));
}
