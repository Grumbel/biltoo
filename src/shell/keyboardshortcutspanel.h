// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef KEYBOARDSHORTCUTSPANEL_H
#define KEYBOARDSHORTCUTSPANEL_H

#include <QWidget>
#include <QList>

class QAction;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

/**
 * Dockable table of application actions that have keyboard shortcuts.
 * Selecting a row highlights the command (Help panel); activating runs it.
 */
class KeyboardShortcutsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit KeyboardShortcutsPanel(QWidget *parent = nullptr);

    /**
     * Replace table rows. Parallel @p actionCategories (empty → "Other").
     * Shortcut text is read from each action at call time.
     */
    void setActions(const QList<QAction *> &actions,
                    const QList<QString> &actionCategories);

signals:
    void actionHighlighted(QAction *action);
    void actionActivated(QAction *action);

private slots:
    void onCurrentCellChanged(int currentRow, int currentColumn, int previousRow,
                              int previousColumn);
    void onItemActivated(QTableWidgetItem *item);
    void onFilterTextChanged(const QString &text);
    void runCurrent();

private:
    QAction *actionAtRow(int row) const;

    QLineEdit *m_filter = nullptr;
    QTableWidget *m_table = nullptr;
};

#endif // KEYBOARDSHORTCUTSPANEL_H
