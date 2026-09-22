// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionreorderdialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleSessionId = Qt::UserRole + 1;
} // namespace

SessionReorderDialog::SessionReorderDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Reorder Session"));
    setModal(true);
    resize(480, 420);

    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        tr("Drag rows or use the buttons to change session order. "
           "Filmstrip, Gallery, slideshow, and export follow this order."),
        this));

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setAlternatingRowColors(true);
    m_list->setUniformItemSizes(true);
    root->addWidget(m_list, 1);

    auto *btnRow = new QHBoxLayout;
    m_upBtn = new QPushButton(tr("Move &Up"), this);
    m_downBtn = new QPushButton(tr("Move &Down"), this);
    m_startBtn = new QPushButton(tr("Move to &Start"), this);
    m_endBtn = new QPushButton(tr("Move to &End"), this);
    btnRow->addWidget(m_upBtn);
    btnRow->addWidget(m_downBtn);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_endBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    // GNOME 2 HIG: Cancel left, OK right (do not rely on QDialogButtonBox order).
    auto *buttons = new QDialogButtonBox(this);
    QPushButton *cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton *okBtn = buttons->addButton(QDialogButtonBox::Ok);
    cancelBtn->setText(tr("&Cancel"));
    okBtn->setText(tr("&OK"));
    okBtn->setDefault(true);
    root->addWidget(buttons);

    connect(m_upBtn, &QPushButton::clicked, this, [this]() { moveSelection(-1); });
    connect(m_downBtn, &QPushButton::clicked, this, [this]() { moveSelection(1); });
    connect(m_startBtn, &QPushButton::clicked, this, [this]() { moveSelectionToEdge(true); });
    connect(m_endBtn, &QPushButton::clicked, this, [this]() { moveSelectionToEdge(false); });
    connect(m_list, &QListWidget::currentRowChanged, this, &SessionReorderDialog::onCurrentRowChanged);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &SessionReorderDialog::updateMoveButtons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateMoveButtons();
}

void SessionReorderDialog::setSession(const QStringList &paths,
                                      const QVector<SessionImageId> &ids)
{
    m_list->clear();
    const int n = qMin(paths.size(), ids.size());
    for (int i = 0; i < n; ++i) {
        const QString &path = paths.at(i);
        const QString label = QFileInfo(path).fileName().isEmpty()
            ? path
            : QFileInfo(path).fileName();
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(kRolePath, path);
        item->setData(kRoleSessionId, QVariant::fromValue(static_cast<qint64>(ids.at(i))));
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                       | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setToolTip(path);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    updateMoveButtons();
}

QStringList SessionReorderDialog::orderedPaths() const
{
    QStringList out;
    out.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        out.append(m_list->item(i)->data(kRolePath).toString());
    }
    return out;
}

QVector<SessionImageId> SessionReorderDialog::orderedIds() const
{
    QVector<SessionImageId> out;
    out.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        out.append(static_cast<SessionImageId>(
            m_list->item(i)->data(kRoleSessionId).toLongLong()));
    }
    return out;
}

void SessionReorderDialog::moveSelection(int delta)
{
    if (!m_list || delta == 0) {
        return;
    }
    QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    // Work on sorted rows; move as a block.
    QList<int> rows;
    rows.reserve(selected.size());
    for (QListWidgetItem *it : selected) {
        rows.append(m_list->row(it));
    }
    std::sort(rows.begin(), rows.end());
    if (delta < 0) {
        if (rows.first() <= 0) {
            return;
        }
        for (int r : rows) {
            QListWidgetItem *item = m_list->takeItem(r);
            m_list->insertItem(r - 1, item);
            item->setSelected(true);
        }
        m_list->setCurrentRow(rows.first() - 1);
    } else {
        if (rows.last() >= m_list->count() - 1) {
            return;
        }
        // Move from bottom so indices stay valid.
        for (int i = rows.size() - 1; i >= 0; --i) {
            const int r = rows.at(i);
            QListWidgetItem *item = m_list->takeItem(r);
            m_list->insertItem(r + 1, item);
            item->setSelected(true);
        }
        m_list->setCurrentRow(rows.last() + 1);
    }
    updateMoveButtons();
}

void SessionReorderDialog::moveSelectionToEdge(bool toStart)
{
    if (!m_list) {
        return;
    }
    QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    QList<int> rows;
    for (QListWidgetItem *it : selected) {
        rows.append(m_list->row(it));
    }
    std::sort(rows.begin(), rows.end());
    if (toStart) {
        if (rows.first() == 0 && rows.size() == rows.last() - rows.first() + 1
            && rows.last() == rows.size() - 1) {
            // Already a contiguous block at the start — still re-pack for gaps.
        }
        QList<QListWidgetItem *> taken;
        for (int i = rows.size() - 1; i >= 0; --i) {
            taken.prepend(m_list->takeItem(rows.at(i)));
        }
        for (int i = 0; i < taken.size(); ++i) {
            m_list->insertItem(i, taken.at(i));
            taken.at(i)->setSelected(true);
        }
        m_list->setCurrentRow(0);
    } else {
        QList<QListWidgetItem *> taken;
        for (int i = rows.size() - 1; i >= 0; --i) {
            taken.prepend(m_list->takeItem(rows.at(i)));
        }
        for (QListWidgetItem *item : taken) {
            m_list->addItem(item);
            item->setSelected(true);
        }
        m_list->setCurrentRow(m_list->count() - taken.size());
    }
    updateMoveButtons();
}

void SessionReorderDialog::onCurrentRowChanged()
{
    updateMoveButtons();
}

void SessionReorderDialog::updateMoveButtons()
{
    if (!m_list) {
        return;
    }
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty()) {
        m_upBtn->setEnabled(false);
        m_downBtn->setEnabled(false);
        m_startBtn->setEnabled(false);
        m_endBtn->setEnabled(false);
        return;
    }
    QList<int> rows;
    for (QListWidgetItem *it : selected) {
        rows.append(m_list->row(it));
    }
    std::sort(rows.begin(), rows.end());
    const int first = rows.first();
    const int last = rows.last();
    m_upBtn->setEnabled(first > 0);
    m_downBtn->setEnabled(last < m_list->count() - 1);
    m_startBtn->setEnabled(first > 0);
    m_endBtn->setEnabled(last < m_list->count() - 1);
}
