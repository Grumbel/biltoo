// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONLISTSTORE_H
#define SESSIONLISTSTORE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

/**
 * Persistent session path lists (Recent Sessions + Bookshelf).
 *
 * Application state under XDG state (not QSettings config). Mutations schedule
 * a debounced write; flush on quit.
 */
class SessionListStore : public QObject
{
    Q_OBJECT
public:
    explicit SessionListStore(QObject *parent = nullptr);

    static QString stateDirectory();
    static QString storeFilePath();

    void load();
    void flush();
    void scheduleSave();

    QList<QStringList> sessionHistory() const { return m_history; }
    QList<QStringList> bookshelf() const { return m_bookshelf; }

    void replaceSessionHistory(const QList<QStringList> &history);
    void replaceBookshelf(const QList<QStringList> &shelf);

private:
    bool writeToDisk() const;
    bool readFromDisk();
    void importLegacyQSettingsIfNeeded();

    QList<QStringList> m_history;
    QList<QStringList> m_bookshelf;
    QTimer m_saveTimer;
    mutable bool m_dirty = false;
};

#endif
