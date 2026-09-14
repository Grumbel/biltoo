// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboardshortcutsdialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMetaObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

QString plainActionTitle(const QAction *action)
{
    if (!action) {
        return {};
    }
    QString t = action->text();
    t.remove(QLatin1Char('&'));
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QChar(0x2026))) {
        t.chop(1);
    }
    return t.trimmed();
}

QString shortcutsNative(const QAction *action)
{
    if (!action) {
        return {};
    }
    const QList<QKeySequence> seqs = action->shortcuts();
    QStringList parts;
    if (!seqs.isEmpty()) {
        for (const QKeySequence &s : seqs) {
            if (!s.isEmpty()) {
                parts.append(s.toString(QKeySequence::NativeText));
            }
        }
    } else if (!action->shortcut().isEmpty()) {
        parts.append(action->shortcut().toString(QKeySequence::NativeText));
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

KeyboardShortcutsDialog::KeyboardShortcutsDialog(const QList<QAction *> &actions,
                                                 const QList<QString> &actionCategories,
                                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    setModal(true);
    resize(640, 480);

    auto *root = new QVBoxLayout(this);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(tr("Shortcut or command…"));
    filterRow->addWidget(m_filter, 1);
    root->addLayout(filterRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Category"), tr("Shortcut"), tr("Command")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setShowGrid(true);
    root->addWidget(m_table, 1);

    // Build rows (sorting on after fill).
    m_table->setSortingEnabled(false);
    QSet<QAction *> seen;
    const int n = actions.size();
    for (int i = 0; i < n; ++i) {
        QAction *act = actions.at(i);
        if (!act || act->isSeparator() || seen.contains(act)) {
            continue;
        }
        const QString keys = shortcutsNative(act);
        if (keys.isEmpty()) {
            continue;
        }
        seen.insert(act);

        const QString category = (i < actionCategories.size() && !actionCategories.at(i).isEmpty())
                                     ? actionCategories.at(i)
                                     : tr("Other");
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *catItem = new QTableWidgetItem(category);
        auto *keyItem = new QTableWidgetItem(keys);
        auto *cmdItem = new QTableWidgetItem(plainActionTitle(act));
        // Store action pointer for activation / help.
        const QVariant ptr = QVariant::fromValue(static_cast<void *>(act));
        catItem->setData(Qt::UserRole, ptr);
        keyItem->setData(Qt::UserRole, ptr);
        cmdItem->setData(Qt::UserRole, ptr);
        if (!act->isEnabled()) {
            const QString tip = act->statusTip().isEmpty()
                                    ? tr("Currently unavailable")
                                    : act->statusTip();
            catItem->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            keyItem->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            cmdItem->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            catItem->setToolTip(tip);
            keyItem->setToolTip(tip);
            cmdItem->setToolTip(tip);
        } else if (!act->statusTip().isEmpty()) {
            cmdItem->setToolTip(act->statusTip());
        }
        m_table->setItem(row, 0, catItem);
        m_table->setItem(row, 1, keyItem);
        m_table->setItem(row, 2, cmdItem);
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(0, Qt::AscendingOrder);

    auto *hint = new QLabel(
        tr("Select a row to show Help. Double-click or press Enter to run the command "
           "(when it is enabled)."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(hint);

    auto *buttons = new QDialogButtonBox(this);
    auto *runBtn = buttons->addButton(tr("&Run"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    root->addWidget(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &KeyboardShortcutsDialog::onFilterTextChanged);
    connect(m_table, &QTableWidget::currentCellChanged, this, &KeyboardShortcutsDialog::onCurrentCellChanged);
    connect(m_table, &QTableWidget::itemActivated, this, &KeyboardShortcutsDialog::onItemActivated);
    connect(runBtn, &QPushButton::clicked, this, &KeyboardShortcutsDialog::runCurrent);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
        // Emit initial highlight after the dialog is shown (caller may connect first).
        QMetaObject::invokeMethod(this, [this]() {
            if (QAction *a = actionAtRow(m_table->currentRow())) {
                emit actionHighlighted(a);
            }
        }, Qt::QueuedConnection);
    }
}

QAction *KeyboardShortcutsDialog::actionAtRow(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return nullptr;
    }
    QTableWidgetItem *item = m_table->item(row, 0);
    if (!item) {
        return nullptr;
    }
    return static_cast<QAction *>(item->data(Qt::UserRole).value<void *>());
}

void KeyboardShortcutsDialog::onCurrentCellChanged(int currentRow, int, int, int)
{
    if (QAction *a = actionAtRow(currentRow)) {
        emit actionHighlighted(a);
    }
}

void KeyboardShortcutsDialog::onItemActivated(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }
    QAction *a = static_cast<QAction *>(item->data(Qt::UserRole).value<void *>());
    if (a) {
        emit actionActivated(a);
    }
}

void KeyboardShortcutsDialog::runCurrent()
{
    if (QAction *a = actionAtRow(m_table ? m_table->currentRow() : -1)) {
        emit actionActivated(a);
    }
}

void KeyboardShortcutsDialog::onFilterTextChanged(const QString &text)
{
    const QString needle = text.trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool match = needle.isEmpty();
        if (!match) {
            for (int c = 0; c < m_table->columnCount(); ++c) {
                QTableWidgetItem *it = m_table->item(r, c);
                if (it && it->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(r, !match);
    }
}

void KeyboardShortcutsDialog::refreshEnabledStyles()
{
    // Reserved if we reopen the dialog without rebuilding; rows are static for now.
}
