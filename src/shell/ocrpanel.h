// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OCRPANEL_H
#define OCRPANEL_H

#include <QWidget>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QLabel;
class QPlainTextEdit;

/**
 * Dock panel for Tesseract OCR: scope, language, force, concurrency,
 * run/cancel, and a live status log (replaces the three View → OCR menu items).
 */
class OcrPanel : public QWidget {
    Q_OBJECT
public:
    enum class Scope {
        CurrentPage = 0,
        Document = 1,
    };

    explicit OcrPanel(QWidget *parent = nullptr);

    Scope scope() const;
    QString language() const;
    void setLanguage(const QString &lang);
    int jobs() const;
    void setJobs(int n);
    bool force() const;
    void setForce(bool on);

    /** Enable/disable Run (and related controls) while a job is active. */
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    /** 0..100; negative hides determinate bar (busy indeterminate). */
    void setProgress(int percent, const QString &caption);
    void clearProgress();

    /** Append a timestamped line to the status log (also updates summary). */
    void appendLog(const QString &line);
    void setSummary(const QString &text);
    void setLayerInfo(const QString &text);

signals:
    void runRequested();
    void cancelRequested();

private:
    void buildUi();

    QComboBox *m_scope = nullptr;
    QLineEdit *m_lang = nullptr;
    QSpinBox *m_jobs = nullptr;
    QCheckBox *m_force = nullptr;
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_layerInfo = nullptr;
    QPlainTextEdit *m_log = nullptr;
    bool m_busy = false;
};

#endif
