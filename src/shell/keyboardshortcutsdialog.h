// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef KEYBOARDSHORTCUTSDIALOG_H
#define KEYBOARDSHORTCUTSDIALOG_H

#include <QDialog>
#include <QList>

class QAction;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

/**
 * Table of application actions that have keyboard shortcuts.
 * Selecting a row highlights the command (for the Help panel);
 * activating (Enter / double-click) runs the action when enabled.
 */
class KeyboardShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param actions Actions to list (typically those with non-empty shortcuts).
     * @param categoryForAction Optional map objectName/text → category; if empty,
     *        categories are inferred from @a actionCategories parallel list.
     * @param actionCategories Parallel to @a actions; empty string → "Other".
     */
    explicit KeyboardShortcutsDialog(const QList<QAction *> &actions,
                                     const QList<QString> &actionCategories,
                                     QWidget *parent = nullptr);

signals:
    void actionHighlighted(QAction *action);
    void actionActivated(QAction *action);

private slots:
    void onCurrentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);
    void onItemActivated(QTableWidgetItem *item);
    void onFilterTextChanged(const QString &text);
    void runCurrent();

private:
    QAction *actionAtRow(int row) const;

    QLineEdit *m_filter = nullptr;
    QTableWidget *m_table = nullptr;
};

#endif // KEYBOARDSHORTCUTSDIALOG_H
