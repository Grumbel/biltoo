// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HELPPANEL_H
#define HELPPANEL_H

#include <QWidget>

class QAction;
class QLabel;
class QTextBrowser;

/**
 * Side panel that shows detailed help for the action currently hovered
 * in a menu/toolbar, or last triggered. Longer than statusTip/toolTip;
 * content comes from QAction::whatsThis() when set, otherwise a short
 * fallback based on statusTip.
 *
 * Disabled actions are supported: show a “Currently unavailable” note with
 * reason from property @c biltooDisabledHelp, else statusTip, else generic.
 * (QAction::hovered often skips disabled items — MainWindow uses menu/toolbar
 * event filters and QMenu::hovered so hover still reaches this panel.)
 */
class HelpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPanel(QWidget *parent = nullptr);

    /** Idle / empty state (no action selected yet). */
    void clear();

    /** Show title, shortcuts, disabled note, and body for @p action. */
    void showAction(const QAction *action);

    /** Show a free-form help topic (modes, filmstrip, …) not tied to a QAction. */
    void showTopic(const QString &title, const QString &bodyHtml,
                   const QString &shortcutsLine = QString());

private:
    static QString plainActionTitle(const QAction *action);
    static QString shortcutsLine(const QAction *action);
    static QString bodyHtmlForAction(const QAction *action);
    static QString disabledReasonForAction(const QAction *action);

    QLabel *m_title = nullptr;
    QLabel *m_shortcuts = nullptr;
    QLabel *m_disabledNote = nullptr;
    QTextBrowser *m_body = nullptr;
};

#endif // HELPPANEL_H
