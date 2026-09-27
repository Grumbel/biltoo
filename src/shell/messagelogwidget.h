// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MESSAGELOGWIDGET_H
#define MESSAGELOGWIDGET_H

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QLabel;
class QToolButton;
class QTimer;

/**
 * Selectable, copyable message log for status / errors.
 *
 * Classic UI: timestamped lines, severity prefix, readable system colours,
 * optional amber “new message” flash so errors are not grey-on-grey.
 */
class MessageLogWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Severity {
        Info = 0,
        Warning,
        Error,
    };

    explicit MessageLogWidget(QWidget *parent = nullptr);

    void append(const QString &text, Severity severity = Severity::Info);
    void appendInfo(const QString &text) { append(text, Severity::Info); }
    void appendWarning(const QString &text) { append(text, Severity::Warning); }
    void appendError(const QString &text) { append(text, Severity::Error); }

    /** Replace contents with a single line (keeps history off for compact TTS strip). */
    void setCurrent(const QString &text, Severity severity = Severity::Info);

    void clear();
    QString allText() const;

    void setMaximumBlockCount(int n);
    void setPlaceholderText(const QString &text);
    void setCompact(bool on); // smaller min height, hide toolbar labels

signals:
    void messageAppended(Severity severity, const QString &text);

private:
    void flashNew(Severity severity);
    void copyAll();
    static QString severityTag(Severity s);
    static QString lineColorCss(Severity s);

    QPlainTextEdit *m_edit = nullptr;
    QLabel *m_badge = nullptr;
    QToolButton *m_copyBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QTimer *m_flashTimer = nullptr;
    bool m_compact = false;
};

#endif
