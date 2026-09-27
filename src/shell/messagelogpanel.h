// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MESSAGELOGPANEL_H
#define MESSAGELOGPANEL_H

#include "shell/messagelogwidget.h"

#include <QWidget>

/**
 * Application message log dock content: one place for TTS, OCR, and other
 * user-visible failures without digging through stdout.
 */
class MessageLogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MessageLogPanel(QWidget *parent = nullptr);

    MessageLogWidget *log() const { return m_log; }

    void append(const QString &source, const QString &text,
                MessageLogWidget::Severity severity = MessageLogWidget::Severity::Info);
    void appendError(const QString &source, const QString &text);
    void appendWarning(const QString &source, const QString &text);
    void appendInfo(const QString &source, const QString &text);

private:
    MessageLogWidget *m_log = nullptr;
};

#endif
