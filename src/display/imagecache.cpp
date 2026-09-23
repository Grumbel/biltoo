// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/imagecache.h"
#include "host/imageloader.h"
#include "display/displayquality.h"

#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include <cstdio>
#include <cstdlib>

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

/** Access order for LRU eviction (front = oldest). */
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

/** Running approximate ARGB32 cost of map() entries (KiB). */
qint64 &totalCostKiB()
{
    static qint64 c = 0;
    return c;
}

/**
 * Hard ceiling on path entries so a flood of tiny underlays cannot grow the
 * hash unboundedly when each is only a few KiB.
 */
constexpr int kHardMaxEntries = 8192;

qint64 budgetKiB()
{
    static qint64 cached = -1;
    if (cached >= 0) {
        return cached;
    }
    qint64 mib = kDefaultBudgetMiB;
    if (const char *e = std::getenv("BILTOO_IMAGECACHE_MIB"); e && e[0]) {
        char *end = nullptr;
        const long v = std::strtol(e, &end, 10);
        if (end != e && v > 0 && v < 65536) {
            mib = v;
        }
    }
    cached = mib * 1024; // MiB → KiB
    return cached;
}

QString ensureKey(const QString &path, int maxEdge)
{
    return path + QLatin1Char('\n') + QString::number(maxEdge);
}

void touchUnlocked(const QString &path)
{
    QStringList &ord = order();
    ord.removeAll(path);
    ord.append(path);
}

void removeEntryUnlocked(const QString &key)
{
    QHash<QString, QImage> &m = map();
    const auto it = m.constFind(key);
    if (it == m.cend()) {
        return;
    }
    totalCostKiB() -= rgbaCostKiB(*it);
    if (totalCostKiB() < 0) {
        totalCostKiB() = 0;
    }
    m.erase(it);
    order().removeAll(key);
}

/**
 * Evict until totalCost + needKiB fits the budget (and entry count ≤ hard max).
 * Prefer dropping large samples first so Gallery underlays (EMB/LQIP ≤
 * kEmbeddedUnderlayMaxEdge) survive mode switches and materialize.
 */
void ensureRoomUnlocked(int needKiB)
{
    QHash<QString, QImage> &m = map();
    QStringList &ord = order();
    const qint64 limit = budgetKiB();

    auto pickVictim = [&](bool largeOnly) -> int {
        for (int i = 0; i < ord.size(); ++i) {
            const QImage &img = m.value(ord.at(i));
            if (img.isNull()) {
                return i;
            }
            const bool large =
                longEdge(img) > DisplayQuality::kEmbeddedUnderlayMaxEdge;
            if (!largeOnly || large) {
                return i;
            }
        }
        return -1;
    };

    while (!ord.isEmpty()
           && (totalCostKiB() + needKiB > limit || m.size() >= kHardMaxEntries)) {
        int victim = pickVictim(true);
        if (victim < 0) {
            victim = pickVictim(false); // all underlays — still enforce budget
        }
        if (victim < 0) {
            break;
        }
        removeEntryUnlocked(ord.at(victim));
    }
}

} // namespace

bool debugOverlayEnabled()
{
    auto on = [](const char *v) {
        return v && v[0] && v[0] != '0';
    };
    return on(std::getenv("THUMTOO_DEBUG_OVERLAY"))
        || on(std::getenv("BILTOO_DEBUG_OVERLAY"));
}

void stampDebugOverlayIfEnabled(QImage *image, const QString &label,
                                const QString &forceTag)
{
    Q_UNUSED(label);
    if (!image || image->isNull() || !debugOverlayEnabled()) {
        return;
    }
    if (image->format() != QImage::Format_RGB32
        && image->format() != QImage::Format_ARGB32
        && image->format() != QImage::Format_ARGB32_Premultiplied) {
        *image = image->convertToFormat(QImage::Format_ARGB32);
    }
    QPainter p(image);
    if (!p.isActive()) {
        return;
    }
    const int w = image->width();
    const int h = image->height();
    // Cyan border distinguishes host ImageCache samples from thumtoo TILE stamps.
    const int border = DisplayQuality::debugStampBorderPx(w, h);
    p.setPen(QPen(QColor(0, 220, 255), border));
    p.setBrush(Qt::NoBrush);
    p.drawRect(border / 2, border / 2, w - border, h - border);

    // HOST = process ImageCache soft/sample (not durable tiles). LQIP = ≤96 edge.
    // No filename / pixel size — those made small filmstrip cells unreadable.
    const int le = qMax(w, h);
    QString tag = forceTag;
    if (tag.isEmpty()) {
        tag = (le <= DisplayQuality::kLqipMaxEdge)
            ? QStringLiteral("LQIP")
            : QStringLiteral("HOST");
    }

    QFont f = p.font();
    f.setBold(true);
    f.setStyleHint(QFont::SansSerif);
    f.setFamily(QStringLiteral("Sans Serif"));
    // Cover most of the sample so the tag is obvious when stretched in Gallery.
    f.setPixelSize(qBound(12, qMin(w, h) / 3, 96));
    p.setFont(f);
    p.setPen(QColor(0, 0, 0, 200));
    p.drawText(QRect(0, 0, w, h).adjusted(1, 1, 1, 1), Qt::AlignCenter, tag);
    p.setPen(QColor(0, 255, 220));
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, tag);
    p.end();
}

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
    touchUnlocked(path);
    return img;
}

void put(const QString &path, const QImage &image)
{
    put(path, image, QString());
}

void put(const QString &path, const QImage &image, const QString &forceTag)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    QImage stored = clampToMaxEdge(image, kDisplayMaxEdge);
    if (stored.isNull()) {
        return;
    }
    // Stamp every sample that enters the host cache so HQ upgrades keep the
    // watermark (soft→PreferCache was replacing stamped soft with clean HQ).
    stampDebugOverlayIfEnabled(&stored, QFileInfo(path).fileName(), forceTag);
    const int incoming = longEdge(stored);
    const int incomingCost = rgbaCostKiB(stored);

    QMutexLocker lock(&mutex());
    QHash<QString, QImage> &m = map();

    if (m.contains(path)) {
        if (longEdge(m.value(path)) >= incoming) {
            touchUnlocked(path);
            return;
        }
        // Replace smaller sample: drop old cost, make room for the delta.
        const int oldCost = rgbaCostKiB(m.value(path));
        totalCostKiB() -= oldCost;
        if (totalCostKiB() < 0) {
            totalCostKiB() = 0;
        }
        ensureRoomUnlocked(incomingCost);
        m.insert(path, stored);
        totalCostKiB() += incomingCost;
        touchUnlocked(path);
        return;
    }

    ensureRoomUnlocked(incomingCost);
    m.insert(path, stored);
    totalCostKiB() += incomingCost;
    order().append(path);
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
    totalCostKiB() = 0;
}

void remove(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QMutexLocker lock(&mutex());
    removeEntryUnlocked(path);
    // Drop in-flight ensure keys for this path (key is path + '\n' + edge).
    QSet<QString> &flight = inFlight();
    const QString prefix = path + QLatin1Char('\n');
    for (auto it = flight.begin(); it != flight.end(); ) {
        if (it->startsWith(prefix) || *it == path) {
            it = flight.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace ImageCache
