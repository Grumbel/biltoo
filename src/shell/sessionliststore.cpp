// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/sessionliststore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr int kStoreVersion = 1;
constexpr int kDebounceMs = 400;

QJsonArray listsToJson(const QList<QStringList> &lists)
{
    QJsonArray outer;
    for (const QStringList &entry : lists) {
        QJsonArray inner;
        for (const QString &p : entry) {
            inner.append(p);
        }
        outer.append(inner);
    }
    return outer;
}

QList<QStringList> listsFromJson(const QJsonArray &outer)
{
    QList<QStringList> out;
    out.reserve(outer.size());
    for (const QJsonValue &v : outer) {
        if (!v.isArray()) {
            continue;
        }
        QStringList entry;
        const QJsonArray inner = v.toArray();
        entry.reserve(inner.size());
        for (const QJsonValue &p : inner) {
            const QString s = p.toString();
            if (!s.isEmpty()) {
                entry.append(s);
            }
        }
        if (!entry.isEmpty()) {
            out.append(entry);
        }
    }
    return out;
}

} // namespace

SessionListStore::SessionListStore(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kDebounceMs);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() {
        if (m_dirty) {
            writeToDisk();
        }
    });
}

QString SessionListStore::stateDirectory()
{
    // XDG state only. Do not use QStandardPaths::AppStateLocation — absent in
    // some Qt 6 builds (compile error even when QT_VERSION claims 6.7+).
    QString xdg = qEnvironmentVariable("XDG_STATE_HOME");
    if (xdg.isEmpty()) {
        xdg = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return xdg + QStringLiteral("/biltoo");
}

QString SessionListStore::storeFilePath()
{
    return stateDirectory() + QStringLiteral("/session-lists.json");
}

void SessionListStore::load()
{
    m_history.clear();
    m_bookshelf.clear();
    m_dirty = false;
    if (!readFromDisk()) {
        importLegacyQSettingsIfNeeded();
        if (!m_history.isEmpty() || !m_bookshelf.isEmpty()) {
            m_dirty = true;
            flush();
        }
    }
}

void SessionListStore::flush()
{
    m_saveTimer.stop();
    if (m_dirty || !QFile::exists(storeFilePath())) {
        writeToDisk();
    }
}

void SessionListStore::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
}

void SessionListStore::replaceSessionHistory(const QList<QStringList> &history)
{
    m_history = history;
    scheduleSave();
}

void SessionListStore::replaceBookshelf(const QList<QStringList> &shelf)
{
    m_bookshelf = shelf;
    scheduleSave();
}

bool SessionListStore::readFromDisk()
{
    QFile f(storeFilePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(0) < 1) {
        return false;
    }
    m_history = listsFromJson(root.value(QStringLiteral("sessionHistory")).toArray());
    m_bookshelf = listsFromJson(root.value(QStringLiteral("bookshelf")).toArray());
    m_dirty = false;
    return true;
}

bool SessionListStore::writeToDisk() const
{
    if (!QDir().mkpath(stateDirectory())) {
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kStoreVersion);
    root.insert(QStringLiteral("sessionHistory"), listsToJson(m_history));
    root.insert(QStringLiteral("bookshelf"), listsToJson(m_bookshelf));
    QSaveFile file(storeFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }
    m_dirty = false;
    return true;
}

void SessionListStore::importLegacyQSettingsIfNeeded()
{
    QSettings settings;
    const int histCount = settings.beginReadArray(QStringLiteral("sessionHistory"));
    for (int i = 0; i < histCount; ++i) {
        settings.setArrayIndex(i);
        const QStringList paths = settings.value(QStringLiteral("paths")).toStringList();
        if (!paths.isEmpty()) {
            m_history.append(paths);
        }
    }
    settings.endArray();
    const int shelfCount = settings.beginReadArray(QStringLiteral("bookshelf"));
    for (int i = 0; i < shelfCount; ++i) {
        settings.setArrayIndex(i);
        const QStringList paths = settings.value(QStringLiteral("paths")).toStringList();
        if (!paths.isEmpty()) {
            m_bookshelf.append(paths);
        }
    }
    settings.endArray();
    if (m_history.isEmpty() && m_bookshelf.isEmpty()) {
        return;
    }
    settings.remove(QStringLiteral("sessionHistory"));
    settings.remove(QStringLiteral("bookshelf"));
}
