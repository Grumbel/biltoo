// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helppanel.h"

#include <QAction>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

HelpPanel::HelpPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    m_title->setFont(titleFont);

    m_shortcuts = new QLabel(this);
    m_shortcuts->setWordWrap(true);
    m_shortcuts->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);

    m_body = new QTextBrowser(this);
    m_body->setOpenExternalLinks(true);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Match panel background; avoid looking like an editable field.
    m_body->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; }"));

    layout->addWidget(m_title);
    layout->addWidget(m_shortcuts);
    layout->addWidget(rule);
    layout->addWidget(m_body, 1);

    clear();
}

void HelpPanel::clear()
{
    m_title->setText(tr("Help"));
    m_shortcuts->clear();
    m_shortcuts->hide();
    m_body->setHtml(tr(
        "<p>Hover a menu item or toolbar button, or run a command, to see "
        "detailed help here.</p>"
        "<p>This panel is meant for longer explanations than the status bar "
        "or tooltip one-liners. Coverage is still being filled in.</p>"));
}

QString HelpPanel::plainActionTitle(const QAction *action)
{
    if (!action) {
        return {};
    }
    QString t = action->text();
    t.remove(QLatin1Char('&'));
    // Drop trailing ellipsis used in menu labels.
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QChar(0x2026))) {
        t.chop(1);
    }
    return t.trimmed();
}

QString HelpPanel::shortcutsLine(const QAction *action)
{
    if (!action) {
        return {};
    }
    const QList<QKeySequence> seqs = action->shortcuts();
    if (seqs.isEmpty()) {
        const QKeySequence primary = action->shortcut();
        if (primary.isEmpty()) {
            return {};
        }
        return primary.toString(QKeySequence::NativeText);
    }
    QStringList parts;
    parts.reserve(seqs.size());
    for (const QKeySequence &s : seqs) {
        if (!s.isEmpty()) {
            parts.append(s.toString(QKeySequence::NativeText));
        }
    }
    return parts.join(QStringLiteral(" · "));
}

QString HelpPanel::bodyHtmlForAction(const QAction *action)
{
    if (!action) {
        return {};
    }
    const QString whats = action->whatsThis().trimmed();
    if (!whats.isEmpty()) {
        // Allow either plain paragraphs or full HTML fragments.
        if (whats.contains(QLatin1Char('<'))) {
            return whats;
        }
        return QStringLiteral("<p>%1</p>").arg(whats.toHtmlEscaped());
    }

    const QString tip = action->statusTip().trimmed();
    if (!tip.isEmpty()) {
        return tr("<p>%1</p>"
                  "<p><i>Detailed help for this command has not been written yet.</i></p>")
            .arg(tip.toHtmlEscaped());
    }
    return tr("<p><i>No help text is available for this command yet.</i></p>");
}

void HelpPanel::showAction(const QAction *action)
{
    if (!action || action->isSeparator()) {
        return;
    }

    const QString title = plainActionTitle(action);
    m_title->setText(title.isEmpty() ? tr("(unnamed action)") : title);

    const QString keys = shortcutsLine(action);
    if (keys.isEmpty()) {
        m_shortcuts->clear();
        m_shortcuts->hide();
    } else {
        m_shortcuts->setText(tr("Shortcut: %1").arg(keys));
        m_shortcuts->show();
    }

    m_body->setHtml(bodyHtmlForAction(action));
}
