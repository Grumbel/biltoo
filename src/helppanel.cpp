// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helppanel.h"

#include <QAction>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QStringList>
#include <QTextBrowser>
#include <QVariant>
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

    m_showAllShortcutsBtn = new QToolButton(this);
    m_showAllShortcutsBtn->setText(tr("Show all…"));
    m_showAllShortcutsBtn->setToolTip(tr("Open the keyboard shortcuts table"));
    m_showAllShortcutsBtn->setAutoRaise(true);
    m_showAllShortcutsBtn->setCursor(Qt::PointingHandCursor);
    connect(m_showAllShortcutsBtn, &QToolButton::clicked, this, &HelpPanel::showAllShortcutsRequested);

    m_disabledNote = new QLabel(this);
    m_disabledNote->setWordWrap(true);
    m_disabledNote->setStyleSheet(
        QStringLiteral("QLabel {"
                       "  background: palette(alternate-base);"
                       "  border: 1px solid palette(mid);"
                       "  border-radius: 3px;"
                       "  padding: 6px;"
                       "  color: palette(window-text);"
                       "}"));
    m_disabledNote->hide();

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
    auto *shortcutRow = new QHBoxLayout;
    shortcutRow->setContentsMargins(0, 0, 0, 0);
    shortcutRow->setSpacing(8);
    shortcutRow->addWidget(m_shortcuts, 1);
    shortcutRow->addWidget(m_showAllShortcutsBtn, 0, Qt::AlignTop);
    layout->addLayout(shortcutRow);
    layout->addWidget(m_disabledNote);
    layout->addWidget(rule);
    layout->addWidget(m_body, 1);

    clear();
}

void HelpPanel::clear()
{
    m_title->setText(tr("Help"));
    m_shortcuts->clear();
    m_shortcuts->hide();
    m_disabledNote->clear();
    m_disabledNote->hide();
    m_body->setHtml(tr(
        "<p>Hover a menu item or toolbar button, or run a command, to see "
        "detailed help here.</p>"
        "<p>Disabled commands still show help when you hover them, including "
        "why they are unavailable when that reason is known.</p>"
        "<p>Use <b>Show all…</b> next to the shortcut line for a table of every "
        "bound key — select a row for Help, double-click or Enter to run it.</p>"
        "<p>Coverage is still being filled in for some commands.</p>"));
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

QString HelpPanel::disabledReasonForAction(const QAction *action)
{
    if (!action || action->isEnabled()) {
        return {};
    }
    // Explicit reason set by enablement sites (preferred).
    const QVariant prop = action->property("biltooDisabledHelp");
    if (prop.isValid()) {
        const QString reason = prop.toString().trimmed();
        if (!reason.isEmpty()) {
            return reason;
        }
    }
    // Many sites already put the "why not" into statusTip while disabled
    // (e.g. slideshow). Prefer that over a generic line.
    const QString tip = action->statusTip().trimmed();
    if (!tip.isEmpty()) {
        return tip;
    }
    return tr("This command is not available in the current context.");
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
        // When disabled, tip is often the reason — body stays short.
        if (!action->isEnabled()) {
            return tr("<p><i>Detailed help for this command has not been written yet.</i></p>");
        }
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

    if (!action->isEnabled()) {
        const QString reason = disabledReasonForAction(action);
        m_disabledNote->setText(tr("<b>Currently unavailable</b><br/>%1").arg(reason.toHtmlEscaped()));
        m_disabledNote->setTextFormat(Qt::RichText);
        m_disabledNote->show();
    } else {
        m_disabledNote->clear();
        m_disabledNote->hide();
    }

    m_body->setHtml(bodyHtmlForAction(action));
}

void HelpPanel::showTopic(const QString &title, const QString &bodyHtml, const QString &shortcutsLine)
{
    m_title->setText(title.isEmpty() ? tr("Help") : title);
    if (shortcutsLine.isEmpty()) {
        m_shortcuts->clear();
        m_shortcuts->hide();
    } else {
        m_shortcuts->setText(shortcutsLine);
        m_shortcuts->show();
    }
    m_disabledNote->clear();
    m_disabledNote->hide();
    m_body->setHtml(bodyHtml);
}
