// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/keyboardshortcutspanel.h"

#include <QAction>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
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
    QStringList parts;
    const QList<QKeySequence> seqs = action->shortcuts();
    if (!seqs.isEmpty()) {
        for (const QKeySequence &s : seqs) {
            if (!s.isEmpty()) {
                parts.append(s.toString(QKeySequence::NativeText));
            }
        }
    } else if (!action->shortcut().isEmpty()) {
        parts.append(action->shortcut().toString(QKeySequence::NativeText));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

KeyboardShortcutsPanel::KeyboardShortcutsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *note = new QLabel(
        tr("<p style='margin:0 0 6px 0'><b>Viewer chords</b> such as "
           "<code>R</code>, <code>C</code>, <code>H</code>, <code>Z</code>, "
           "<code>Q</code>, and <code>Space</code> apply when the image view "
           "has focus — not while typing in the Location or Search bar. "
           "<code>Esc</code> cancels crop, leaves slideshow, exits fullscreen, "
           "then returns from Image mode.</p>"),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    root->addWidget(note);

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

    auto *btnRow = new QHBoxLayout;
    auto *runBtn = new QPushButton(tr("Run"), this);
    runBtn->setToolTip(tr("Run the selected command (Enter or double-click also works)"));
    btnRow->addWidget(runBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    connect(m_filter, &QLineEdit::textChanged, this, &KeyboardShortcutsPanel::onFilterTextChanged);
    connect(m_table, &QTableWidget::currentCellChanged, this,
            &KeyboardShortcutsPanel::onCurrentCellChanged);
    connect(m_table, &QTableWidget::itemActivated, this, &KeyboardShortcutsPanel::onItemActivated);
    connect(runBtn, &QPushButton::clicked, this, &KeyboardShortcutsPanel::runCurrent);
}

void KeyboardShortcutsPanel::setActions(const QList<QAction *> &actions,
                                        const QList<QString> &actionCategories)
{
    if (!m_table) {
        return;
    }
    const QString filter = m_filter ? m_filter->text() : QString();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    QSet<QAction *> seen;
    const int n = actions.size();
    for (int i = 0; i < n; ++i) {
        QAction *act = actions.at(i);
        if (!act || act->isSeparator() || seen.contains(act)) {
            continue;
        }
        seen.insert(act);
        const QString keys = shortcutsNative(act);
        if (keys.isEmpty()) {
            continue;
        }
        QString cat = (i < actionCategories.size()) ? actionCategories.at(i) : QString();
        if (cat.isEmpty()) {
            cat = tr("Other");
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *catItem = new QTableWidgetItem(cat);
        auto *keyItem = new QTableWidgetItem(keys);
        auto *cmdItem = new QTableWidgetItem(plainActionTitle(act));
        catItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(act)));
        keyItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(act)));
        cmdItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(act)));
        if (!act->isEnabled()) {
            const QColor mid = palette().color(QPalette::Mid);
            catItem->setForeground(mid);
            keyItem->setForeground(mid);
            cmdItem->setForeground(mid);
        }
        m_table->setItem(row, 0, catItem);
        m_table->setItem(row, 1, keyItem);
        m_table->setItem(row, 2, cmdItem);
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(0, Qt::AscendingOrder);
    if (!filter.isEmpty()) {
        onFilterTextChanged(filter);
    }
}

QAction *KeyboardShortcutsPanel::actionAtRow(int row) const
{
    if (!m_table || row < 0 || row >= m_table->rowCount()) {
        return nullptr;
    }
    QTableWidgetItem *it = m_table->item(row, 0);
    if (!it) {
        return nullptr;
    }
    return static_cast<QAction *>(it->data(Qt::UserRole).value<void *>());
}

void KeyboardShortcutsPanel::onCurrentCellChanged(int currentRow, int, int, int)
{
    if (QAction *act = actionAtRow(currentRow)) {
        emit actionHighlighted(act);
    }
}

void KeyboardShortcutsPanel::onItemActivated(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }
    if (QAction *act = actionAtRow(item->row())) {
        emit actionActivated(act);
    }
}

void KeyboardShortcutsPanel::runCurrent()
{
    if (!m_table) {
        return;
    }
    if (QAction *act = actionAtRow(m_table->currentRow())) {
        emit actionActivated(act);
    }
}

void KeyboardShortcutsPanel::onFilterTextChanged(const QString &text)
{
    if (!m_table) {
        return;
    }
    const QString needle = text.trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool match = needle.isEmpty();
        if (!match) {
            for (int c = 0; c < 3; ++c) {
                if (QTableWidgetItem *it = m_table->item(r, c)) {
                    if (it->text().contains(needle, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
        }
        m_table->setRowHidden(r, !match);
    }
}
