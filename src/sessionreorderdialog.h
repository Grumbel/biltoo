// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONREORDERDIALOG_H
#define SESSIONREORDERDIALOG_H

#include "imageview_types.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QListWidget;
class QPushButton;

/**
 * Modal session order editor. Drag rows or use Move buttons; OK commits a new
 * paths ∥ SessionImageId order (host applies via undo stack).
 */
class SessionReorderDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SessionReorderDialog(QWidget *parent = nullptr);

    void setSession(const QStringList &paths, const QVector<SessionImageId> &ids);

    QStringList orderedPaths() const;
    QVector<SessionImageId> orderedIds() const;

private:
    void moveSelection(int delta);
    void moveSelectionToEdge(bool toStart);
    void updateMoveButtons();
    void onCurrentRowChanged();

    QListWidget *m_list = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_endBtn = nullptr;
};

#endif
