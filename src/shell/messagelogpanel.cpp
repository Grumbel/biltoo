// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/messagelogpanel.h"

#include <QVBoxLayout>
#include <QLabel>

MessageLogPanel::MessageLogPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    auto *hint = new QLabel(
        tr("Application messages (TTS, OCR, load errors). "
           "Select text or use Copy. Warnings and errors highlight briefly."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(window-text);"));
    lay->addWidget(hint);
    m_log = new MessageLogWidget(this);
    m_log->setPlaceholderText(tr("No messages yet."));
    m_log->setMaximumBlockCount(1000);
    lay->addWidget(m_log, 1);
}

void MessageLogPanel::append(const QString &source, const QString &text,
                             MessageLogWidget::Severity severity)
{
    if (!m_log || text.trimmed().isEmpty()) {
        return;
    }
    const QString line = source.isEmpty()
        ? text.trimmed()
        : QStringLiteral("%1: %2").arg(source, text.trimmed());
    m_log->append(line, severity);
}

void MessageLogPanel::appendError(const QString &source, const QString &text)
{
    append(source, text, MessageLogWidget::Severity::Error);
}

void MessageLogPanel::appendWarning(const QString &source, const QString &text)
{
    append(source, text, MessageLogWidget::Severity::Warning);
}

void MessageLogPanel::appendInfo(const QString &source, const QString &text)
{
    append(source, text, MessageLogWidget::Severity::Info);
}
