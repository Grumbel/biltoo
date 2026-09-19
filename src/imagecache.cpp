// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecache.h"
#include "imageloader.h"
#include "displayquality.h"

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

constexpr int kMaxEntries = 384;

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

bool debugOverlayEnabled()
{
    auto on = [](const char *v) {
        return v && v[0] && v[0] != '0';
    };
    return on(std::getenv("THUMTOO_DEBUG_OVERLAY"))
        || on(std::getenv("BILTOO_DEBUG_OVERLAY"));
}

void stampDebugOverlayIfEnabled(QImage *image, const QString &label)
{
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
    // Single cyan plate (not a repeated grid) — readable when soft is stretched
    // into large Gallery cells. Distinct from thumtoo magenta/yellow TILE stamps.
    const int border = DisplayQuality::debugStampBorderPx(w, h);
    p.setPen(QPen(QColor(0, 220, 255), border));
    p.setBrush(Qt::NoBrush);
    p.drawRect(border / 2, border / 2, w - border, h - border);

    QStringList lines;
    // Host ImageCache sample. Gallery must not show these as underlay (>LQIP);
    // stamp marks accidental soft/HOST plates vs thumtoo TILE stamps.
    const int le = qMax(w, h);
    if (le <= DisplayQuality::kLqipMaxEdge) {
        lines << QStringLiteral("LQIP");
    } else {
        lines << QStringLiteral("HOST-SAMPLE"); // not product underlay in Gallery
    }
    if (!label.isEmpty()) {
        lines << label;
    }
    lines << QStringLiteral("%1×%2").arg(w).arg(h);
    lines << QStringLiteral("le=%1").arg(le);

    QFont f = p.font();
    f.setBold(true);
    // Fixed readable band; avoid tiny soft fonts and avoid huge stretched ones.
    f.setPixelSize(DisplayQuality::debugStampFontPx(w, h));
    p.setFont(f);
    const QFontMetrics fm(f);
    int blockW = 0;
    for (const QString &line : lines) {
        blockW = qMax(blockW, fm.horizontalAdvance(line));
    }
    const int lineH = fm.height();
    const int pad = 4;
    const int blockH = lineH * lines.size() + pad * 2;
    blockW += pad * 2;

    // One plate, bottom-right (thumtoo soft stamps stay top-left).
    const int x = qMax(border + 2, w - border - blockW - 4);
    const int y = qMax(border + 2, h - border - blockH - 4);
    p.fillRect(QRect(x, y, blockW, blockH), QColor(0, 0, 0, 180));
    p.setPen(QColor(0, 255, 220));
    for (int i = 0; i < lines.size(); ++i) {
        p.drawText(QPoint(x + pad, y + pad + (i + 1) * lineH - fm.descent()),
                   lines.at(i));
    }
    p.end();
    static bool once = false;
    if (!once) {
        once = true;
        fprintf(stderr, "biltoo: DEBUG_OVERLAY host watermark (cyan plate, BR)\n");
    }
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
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    QImage stored = clampToMaxEdge(image, kDisplayMaxEdge);
    if (stored.isNull()) {
        return;
    }
    // Stamp every sample that enters the host cache so HQ upgrades keep the
    // watermark (soft→PreferCache was replacing stamped soft with clean HQ).
    stampDebugOverlayIfEnabled(&stored, QFileInfo(path).fileName());
    const int incoming = longEdge(stored);

    QMutexLocker lock(&mutex());
    QHash<QString, QImage> &m = map();

    if (m.contains(path)) {
        if (longEdge(m.value(path)) >= incoming) {
            touchUnlocked(path);
            return;
        }
        m.insert(path, stored);
        touchUnlocked(path);
        return;
    }

    if (m.size() >= kMaxEntries) {
        evictOldestUnlocked();
    }
    m.insert(path, stored);
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
}

void remove(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QMutexLocker lock(&mutex());
    map().remove(path);
    order().removeAll(path);
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
