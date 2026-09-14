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
 */
class HelpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPanel(QWidget *parent = nullptr);

    /** Idle / empty state (no action selected yet). */
    void clear();

    /** Show title, shortcuts, and detailed body for @p action. */
    void showAction(const QAction *action);

private:
    static QString plainActionTitle(const QAction *action);
    static QString shortcutsLine(const QAction *action);
    static QString bodyHtmlForAction(const QAction *action);

    QLabel *m_title = nullptr;
    QLabel *m_shortcuts = nullptr;
    QTextBrowser *m_body = nullptr;
};

#endif // HELPPANEL_H
