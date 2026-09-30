// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/ocrpanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFrame>

OcrPanel::OcrPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

void OcrPanel::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *opts = new QGroupBox(tr("Options"), inner);
    auto *form = new QFormLayout(opts);
    m_scope = new QComboBox(opts);
    m_scope->addItem(tr("Current image"), int(Scope::CurrentPage));
    m_scope->addItem(tr("All pages of multipage document"), int(Scope::Document));
    m_scope->setToolTip(tr("Current image: the focused session image (any format).\n"
                           "Multipage: every page when the path expands to a document (PDF/DjVu/EPUB)."));
    form->addRow(tr("Scope"), m_scope);

    m_lang = new QLineEdit(opts);
    m_lang->setPlaceholderText(QStringLiteral("eng"));
    m_lang->setText(QStringLiteral("eng"));
    m_lang->setToolTip(tr("Tesseract language code(s), e.g. eng or eng+deu"));
    form->addRow(tr("Language"), m_lang);

    m_jobs = new QSpinBox(opts);
    m_jobs->setRange(1, 4);
    m_jobs->setValue(2);
    m_jobs->setToolTip(tr("Parallel page workers for document OCR (1–4).\n"
                          "Tesseract itself is serialized; extra jobs overlap rasterize."));
    form->addRow(tr("Jobs"), m_jobs);

    m_dpiRow = new QWidget(opts);
    auto *dpiLay = new QHBoxLayout(m_dpiRow);
    dpiLay->setContentsMargins(0, 0, 0, 0);
    dpiLay->setSpacing(6);

    m_dpiAuto = new QToolButton(m_dpiRow);
    m_dpiAuto->setText(tr("Auto"));
    m_dpiAuto->setCheckable(true);
    m_dpiAuto->setChecked(true);
    m_dpiAuto->setToolTip(
        tr("When on, Tesseract source DPI is estimated:\n"
           "document pages use 72×raster/page-box; plain images use 300.\n"
           "Turn off to set DPI manually (useful for mixed type sizes)."));
    dpiLay->addWidget(m_dpiAuto);

    m_dpi = new QSpinBox(m_dpiRow);
    m_dpi->setRange(70, 600);
    m_dpi->setValue(300);
    m_dpi->setSingleStep(10);
    m_dpi->setSuffix(QStringLiteral(" DPI"));
    m_dpi->setToolTip(
        tr("Manual Tesseract source resolution (70–600).\n"
           "Try 200–400 if recognition of mixed body/caption sizes is poor."));
    dpiLay->addWidget(m_dpi, 1);

    form->addRow(tr("Source DPI"), m_dpiRow);
    connect(m_dpiAuto, &QToolButton::toggled, this, [this](bool) { syncDpiControls(); });
    syncDpiControls();

    layout->addWidget(opts);

    auto *actions = new QGroupBox(tr("Run"), inner);
    auto *actLay = new QVBoxLayout(actions);
    m_runBtn = new QPushButton(tr("Run OCR"), actions);
    m_runBtn->setDefault(false);
    m_runBtn->setAutoDefault(false);
    m_cancelBtn = new QPushButton(tr("Cancel"), actions);
    m_cancelBtn->setEnabled(false);
    actLay->addWidget(m_runBtn);
    actLay->addWidget(m_cancelBtn);
    connect(m_runBtn, &QPushButton::clicked, this, &OcrPanel::runRequested);
    connect(m_cancelBtn, &QPushButton::clicked, this, &OcrPanel::cancelRequested);
    layout->addWidget(actions);

    auto *prog = new QGroupBox(tr("Progress"), inner);
    auto *progLay = new QVBoxLayout(prog);
    m_progressLabel = new QLabel(tr("Idle"), prog);
    m_progressLabel->setWordWrap(true);
    m_progress = new QProgressBar(prog);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    progLay->addWidget(m_progressLabel);
    progLay->addWidget(m_progress);
    m_summary = new QLabel(tr("No job yet"), prog);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color: palette(mid);"));
    progLay->addWidget(m_summary);
    layout->addWidget(prog);

    auto *info = new QGroupBox(tr("Current image layers"), inner);
    auto *infoLay = new QVBoxLayout(info);
    m_layerInfo = new QLabel(tr("—"), info);
    m_layerInfo->setWordWrap(true);
    m_layerInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLay->addWidget(m_layerInfo);
    layout->addWidget(info);

    auto *logBox = new QGroupBox(tr("Log"), inner);
    auto *logLay = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit(logBox);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setPlaceholderText(tr("OCR status messages appear here."));
    logLay->addWidget(m_log);
    layout->addWidget(logBox, 1);

    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void OcrPanel::syncDpiControls()
{
    const bool autoOn = m_dpiAuto && m_dpiAuto->isChecked();
    if (m_dpi) {
        m_dpi->setVisible(!autoOn);
        m_dpi->setEnabled(!autoOn && !m_busy);
    }
}

OcrPanel::Scope OcrPanel::scope() const
{
    if (!m_scope) {
        return Scope::CurrentPage;
    }
    return static_cast<Scope>(m_scope->currentData().toInt());
}

QString OcrPanel::language() const
{
    if (!m_lang) {
        return QStringLiteral("eng");
    }
    const QString t = m_lang->text().trimmed();
    return t.isEmpty() ? QStringLiteral("eng") : t;
}

void OcrPanel::setLanguage(const QString &lang)
{
    if (m_lang) {
        m_lang->setText(lang.isEmpty() ? QStringLiteral("eng") : lang);
    }
}

int OcrPanel::jobs() const
{
    return m_jobs ? m_jobs->value() : 2;
}

void OcrPanel::setJobs(int n)
{
    if (m_jobs) {
        m_jobs->setValue(qBound(1, n, 4));
    }
}

int OcrPanel::sourceDpi() const
{
    if (!m_dpiAuto || m_dpiAuto->isChecked()) {
        return 0;
    }
    return m_dpi ? m_dpi->value() : 0;
}

void OcrPanel::setSourceDpi(int dpi)
{
    if (dpi <= 0) {
        if (m_dpiAuto) {
            m_dpiAuto->setChecked(true);
        }
    } else {
        if (m_dpi) {
            m_dpi->setValue(qBound(70, dpi, 600));
        }
        if (m_dpiAuto) {
            m_dpiAuto->setChecked(false);
        }
    }
    syncDpiControls();
}

void OcrPanel::setBusy(bool busy)
{
    m_busy = busy;
    if (m_runBtn) {
        m_runBtn->setEnabled(!busy);
    }
    if (m_cancelBtn) {
        m_cancelBtn->setEnabled(busy);
    }
    if (m_scope) {
        m_scope->setEnabled(!busy);
    }
    if (m_lang) {
        m_lang->setEnabled(!busy);
    }
    if (m_jobs) {
        m_jobs->setEnabled(!busy);
    }
    if (m_dpiAuto) {
        m_dpiAuto->setEnabled(!busy);
    }
    syncDpiControls();
    if (!busy && m_progress) {
        m_progress->setRange(0, 100);
    }
}

void OcrPanel::setProgress(int percent, const QString &caption)
{
    if (m_progressLabel) {
        m_progressLabel->setText(caption);
    }
    if (!m_progress) {
        return;
    }
    if (percent < 0) {
        m_progress->setRange(0, 0); // indeterminate
    } else {
        m_progress->setRange(0, 100);
        m_progress->setValue(qBound(0, percent, 100));
    }
}

void OcrPanel::clearProgress()
{
    if (m_progress) {
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
    }
    if (m_progressLabel) {
        m_progressLabel->setText(tr("Idle"));
    }
}

void OcrPanel::appendLog(const QString &line)
{
    if (!m_log) {
        return;
    }
    const QString stamped =
        QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                      line);
    m_log->appendPlainText(stamped);
    emit logLineAppended(line);
}

void OcrPanel::setSummary(const QString &text)
{
    if (m_summary) {
        m_summary->setText(text);
    }
}

void OcrPanel::setLayerInfo(const QString &text)
{
    if (m_layerInfo) {
        m_layerInfo->setText(text.isEmpty() ? tr("—") : text);
    }
}
