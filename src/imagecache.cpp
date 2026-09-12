// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecache.h"
#include "imageloader.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

namespace ImageCache {
namespace {

QMutex &mutex()
{
    static QMutex m;
    return m;
}

QHash<QString, QImage> &map()
{
    static QHash<QString, QImage> m;
    return m;
}

/** Insertion order for FIFO eviction when the map is full. */
QStringList &order()
{
    static QStringList o;
    return o;
}

QSet<QString> &inFlight()
{
    static QSet<QString> s;
    return s;
}

constexpr int kMaxEntries = 384;

QString ensureKey(const QString &path, int maxEdge)
{
    return path + QLatin1Char('\n') + QString::number(maxEdge);
}

void evictOldestUnlocked()
{
    QHash<QString, QImage> &m = map();
    QStringList &ord = order();
    while (!ord.isEmpty() && m.size() >= kMaxEntries) {
        const QString oldest = ord.takeFirst();
        m.remove(oldest);
    }
}

} // namespace

QImage clampToMaxEdge(const QImage &image, int maxEdge)
{
    if (image.isNull() || maxEdge <= 0) {
        return image;
    }
    if (longEdge(image) <= maxEdge) {
        return image;
    }
    return image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage get(const QString &path, int minLongEdge)
{
    if (path.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&mutex());
    const QImage img = map().value(path);
    if (!adequate(img, minLongEdge)) {
        return {};
    }
    return img;
}

void put(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    const QImage stored = clampToMaxEdge(image, kDisplayMaxEdge);
    if (stored.isNull()) {
        return;
    }
    const int incoming = longEdge(stored);

    QMutexLocker lock(&mutex());
    QHash<QString, QImage> &m = map();
    QStringList &ord = order();

    if (m.contains(path)) {
        if (longEdge(m.value(path)) >= incoming) {
            return;
        }
        // Upgrade in place — keep position in insertion order.
        m.insert(path, stored);
        return;
    }

    if (m.size() >= kMaxEntries) {
        evictOldestUnlocked();
    }
    m.insert(path, stored);
    ord.append(path);
}

bool has(const QString &path, int minLongEdge)
{
    return !get(path, minLongEdge).isNull();
}

QImage ensure(const QString &path, int maxEdge)
{
    if (path.isEmpty() || maxEdge <= 0) {
        return get(path);
    }
    QImage hit = get(path, maxEdge);
    if (!hit.isNull()) {
        return hit;
    }
    // Return any smaller frame while a better one loads.
    hit = get(path);

    const QString key = ensureKey(path, maxEdge);
    {
        QMutexLocker lock(&mutex());
        if (inFlight().contains(key)) {
            return hit;
        }
        inFlight().insert(key);
    }

    QThreadPool::globalInstance()->start([path, maxEdge, key]() {
        const QImage loaded = ImageLoader::loadThumbnail(path, maxEdge);
        if (!loaded.isNull()) {
            put(path, loaded);
        }
        QMutexLocker lock(&mutex());
        inFlight().remove(key);
    });

    return hit;
}

void warm(const QStringList &paths, int maxEdge)
{
    for (const QString &path : paths) {
        if (!path.isEmpty()) {
            ensure(path, maxEdge);
        }
    }
}

void clear()
{
    QMutexLocker lock(&mutex());
    map().clear();
    order().clear();
    inFlight().clear();
}

} // namespace ImageCache
