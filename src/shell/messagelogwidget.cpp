// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/messagelogwidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QLabel>
#include <QToolButton>
#include <QTimer>
#include <QTime>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QFrame>

MessageLogWidget::MessageLogWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    m_badge = new QLabel(this);
    m_badge->setVisible(false);
    m_badge->setStyleSheet(
        QStringLiteral("QLabel { background: #f0c040; color: #1a1a1a; "
                       "padding: 1px 6px; border-radius: 3px; font-weight: 600; }"));
    bar->addWidget(m_badge);
    bar->addStretch(1);
    m_copyBtn = new QToolButton(this);
    m_copyBtn->setText(tr("Copy"));
    m_copyBtn->setToolTip(tr("Copy all messages to the clipboard"));
    m_copyBtn->setAutoRaise(true);
    connect(m_copyBtn, &QToolButton::clicked, this, &MessageLogWidget::copyAll);
    bar->addWidget(m_copyBtn);
    m_clearBtn = new QToolButton(this);
    m_clearBtn->setText(tr("Clear"));
    m_clearBtn->setToolTip(tr("Clear the message log"));
    m_clearBtn->setAutoRaise(true);
    connect(m_clearBtn, &QToolButton::clicked, this, &MessageLogWidget::clear);
    bar->addWidget(m_clearBtn);
    root->addLayout(bar);

    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setUndoRedoEnabled(false);
    m_edit->setMaximumBlockCount(500);
    m_edit->setTabChangesFocus(true);
    m_edit->setPlaceholderText(tr("Messages appear here…"));
    m_edit->setMinimumHeight(72);
    // Readable body text — not palette(mid) grey.
    m_edit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: palette(base);"
        "  color: palette(text);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 2px;"
        "  padding: 2px;"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "}"));
    root->addWidget(m_edit, 1);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this, [this]() {
        if (m_edit) {
            m_edit->setStyleSheet(QStringLiteral(
                "QPlainTextEdit {"
                "  background: palette(base);"
                "  color: palette(text);"
                "  border: 1px solid palette(mid);"
                "  border-radius: 2px;"
                "  padding: 2px;"
                "  font-family: monospace;"
                "  font-size: 11px;"
                "}"));
        }
        if (m_badge) {
            m_badge->setVisible(false);
        }
    });
}

void MessageLogWidget::setCompact(bool on)
{
    m_compact = on;
    if (m_edit) {
        m_edit->setMinimumHeight(on ? 48 : 72);
        m_edit->setMaximumHeight(on ? 96 : 16777215);
    }
    if (m_copyBtn) {
        m_copyBtn->setText(on ? tr("Copy") : tr("Copy"));
    }
}

void MessageLogWidget::setMaximumBlockCount(int n)
{
    if (m_edit) {
        m_edit->setMaximumBlockCount(n);
    }
}

void MessageLogWidget::setPlaceholderText(const QString &text)
{
    if (m_edit) {
        m_edit->setPlaceholderText(text);
    }
}

QString MessageLogWidget::severityTag(Severity s)
{
    switch (s) {
    case Severity::Warning:
        return QStringLiteral("warn");
    case Severity::Error:
        return QStringLiteral("error");
    case Severity::Info:
    default:
        return QStringLiteral("info");
    }
}

QString MessageLogWidget::lineColorCss(Severity s)
{
    switch (s) {
    case Severity::Warning:
        return QStringLiteral("#8a6d00"); // dark amber, readable on light/dark base via prefix
    case Severity::Error:
        return QStringLiteral("#c0392b");
    case Severity::Info:
    default:
        return QString();
    }
}

void MessageLogWidget::append(const QString &text, Severity severity)
{
    if (!m_edit || text.trimmed().isEmpty()) {
        return;
    }
    const QString stamped = QStringLiteral("%1 [%2] %3")
                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                     severityTag(severity),
                                     text.trimmed());
    // QPlainTextEdit is plain; severity is in the tag (copyable). Colour via
    // temporary flash + badge; keep text fully selectable.
    m_edit->appendPlainText(stamped);
    m_edit->verticalScrollBar()->setValue(m_edit->verticalScrollBar()->maximum());
    flashNew(severity);
    emit messageAppended(severity, text.trimmed());
}

void MessageLogWidget::setCurrent(const QString &text, Severity severity)
{
    if (!m_edit) {
        return;
    }
    m_edit->clear();
    if (!text.trimmed().isEmpty()) {
        append(text, severity);
    }
}

void MessageLogWidget::clear()
{
    if (m_edit) {
        m_edit->clear();
    }
    if (m_badge) {
        m_badge->setVisible(false);
    }
    if (m_flashTimer) {
        m_flashTimer->stop();
    }
}

QString MessageLogWidget::allText() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

void MessageLogWidget::flashNew(Severity severity)
{
    if (!m_edit) {
        return;
    }
    QString border = QStringLiteral("#f0c040");
    QString bg = QStringLiteral("#fff8e0");
    QString badge = tr("New");
    if (severity == Severity::Error) {
        border = QStringLiteral("#e74c3c");
        bg = QStringLiteral("#fdecea");
        badge = tr("Error");
    } else if (severity == Severity::Warning) {
        border = QStringLiteral("#f0c040");
        bg = QStringLiteral("#fff8e0");
        badge = tr("Warning");
    }
    m_edit->setStyleSheet(QStringLiteral(
                              "QPlainTextEdit {"
                              "  background: %1;"
                              "  color: palette(text);"
                              "  border: 2px solid %2;"
                              "  border-radius: 2px;"
                              "  padding: 2px;"
                              "  font-family: monospace;"
                              "  font-size: 11px;"
                              "}")
                              .arg(bg, border));
    if (m_badge) {
        m_badge->setText(badge);
        m_badge->setVisible(true);
    }
    if (m_flashTimer) {
        m_flashTimer->start(severity == Severity::Error ? 6000 : 3500);
    }
}

void MessageLogWidget::copyAll()
{
    if (!m_edit) {
        return;
    }
    QApplication::clipboard()->setText(m_edit->toPlainText());
}
