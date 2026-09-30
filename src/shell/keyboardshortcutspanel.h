// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef KEYBOARDSHORTCUTSPANEL_H
#define KEYBOARDSHORTCUTSPANEL_H

#include <QWidget>
#include <QList>
#include <QString>

class QAction;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;
class QShowEvent;

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
     * Optional @p shortcutOverride supplies display text when the action's
     * live shortcut list is empty (e.g. temporary fullscreen chords).
     */
    void setActions(const QList<QAction *> &actions,
                    const QList<QString> &actionCategories,
                    const QList<QString> &shortcutOverrides = {});

signals:
    /** Emitted when the panel becomes visible so the host can (re)fill rows. */
    void refreshRequested();
    void actionHighlighted(QAction *action);
    void actionActivated(QAction *action);

protected:
    void showEvent(QShowEvent *event) override;

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
