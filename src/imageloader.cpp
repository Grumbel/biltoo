// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// libvips pulls in GLib, which has struct fields named "signals". Qt defines
// signals as a macro — include vips before any Qt headers.
#ifdef BILTOO_HAVE_VIPS
#include <vips/vips.h>
#endif

#include "imageloader.h"
#include "thumtoocache.h"
#include "imagecache.h"
#include "archivepath.h"
#include "pagepath.h"

#include <QBuffer>
#include <cstring>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QSize>
#include <QImageReader>

namespace ImageLoader {
namespace {

QImage scaleToMaxEdge(QImage image, int maxEdge)
{
    if (image.isNull() || maxEdge <= 0) {
        return image;
    }
    if (image.width() <= maxEdge && image.height() <= maxEdge) {
        return image;
    }
    return image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QStringList formatCandidates(const QString &hint)
{
    QStringList out;
    const QString h = hint.toLower();
    if (!h.isEmpty()) {
        out.append(h);
    }
    // Common aliases / plugin names Qt may register under.
    if (h == QLatin1String("jpg") || h == QLatin1String("jpe")) {
        out.append(QStringLiteral("jpeg"));
    } else if (h == QLatin1String("jpeg")) {
        out.append(QStringLiteral("jpg"));
    } else if (h == QLatin1String("tif")) {
        out.append(QStringLiteral("tiff"));
    } else if (h == QLatin1String("tiff")) {
        out.append(QStringLiteral("tif"));
    }
    // Sniff magic when suffix is missing or plugins ignore auto-detect.
    return out;
}

QImage decodeWithQtReader(const QByteArray &bytes, const QByteArray &format, int maxEdge)
{
    QByteArray data = bytes;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (!format.isEmpty()) {
        reader.setFormat(format);
    } else {
        reader.setDecideFormatFromContent(true);
    }
    if (maxEdge > 0) {
        const QSize size = reader.size();
        if (size.isValid()) {
            QSize scaled = size;
            scaled.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
            if (scaled != size) {
                reader.setScaledSize(scaled);
            }
        }
    }
    return reader.read();
}

#ifdef BILTOO_HAVE_VIPS
// Forward-declared: defined with other vips helpers below.
QImage loadWithVipsBuffer(const QByteArray &bytes, int maxEdge);
#endif

QImage decodeFromBytes(const QByteArray &bytes, const QString &formatHint, int maxEdge)
{
    if (bytes.isEmpty()) {
        return {};
    }

    // 1) Explicit format hints first — WebP/JPEG plugins often need this for buffers.
    for (const QString &fmt : formatCandidates(formatHint)) {
        QImage image = QImage::fromData(bytes, fmt.toLatin1().constData());
        if (!image.isNull()) {
            return scaleToMaxEdge(image, maxEdge);
        }
        image = decodeWithQtReader(bytes, fmt.toLatin1(), maxEdge);
        if (!image.isNull()) {
            return image;
        }
    }

    // 2) Content sniff / no format.
    QImage image = QImage::fromData(bytes);
    if (!image.isNull()) {
        return scaleToMaxEdge(image, maxEdge);
    }
    image = decodeWithQtReader(bytes, QByteArray(), maxEdge);
    if (!image.isNull()) {
        return image;
    }

#ifdef BILTOO_HAVE_VIPS
    // 3) libvips buffer path (WebP/HEIF/etc. when Qt plugins miss buffer loads).
    image = loadWithVipsBuffer(bytes, maxEdge);
    if (!image.isNull()) {
        return image;
    }
#endif
    return {};
}

/** Archive member bytes via thumtoo only (no biltoo libarchive path). */
QByteArray readArchiveMemberBytes(const QString &path)
{
    return ThumtooCache::readArchiveMemberBytes(path);
}

QImage loadArchiveRef(const QString &path, int maxEdge)
{
    const ArchivePath::Ref ref = ArchivePath::parse(path);
    if (!ref.valid) {
        qWarning("ImageLoader: invalid archive ref: %s", qPrintable(path));
        return {};
    }
    const QByteArray bytes = readArchiveMemberBytes(path);
    if (bytes.isEmpty()) {
        qWarning("ImageLoader: empty archive member %s from %s",
                 qPrintable(ref.memberPath), qPrintable(ref.archivePath));
        return {};
    }
    const QString suffix = QFileInfo(ref.memberPath).suffix().toLower();
    QImage image = decodeFromBytes(bytes, suffix, maxEdge);
    if (image.isNull()) {
        const QByteArray head = bytes.left(12).toHex(' ');
        qWarning("ImageLoader: decode failed for %s (%lld bytes, suffix=%s, head=%s)",
                 qPrintable(path), static_cast<qlonglong>(bytes.size()),
                 qPrintable(suffix), head.constData());
    }
    return image;
}

QImage loadPageRef(const QString &path, int maxEdge)
{
    const PagePath::Ref ref = PagePath::parse(path);
    if (!ref.valid) {
        qWarning("ImageLoader: invalid page ref: %s", qPrintable(path));
        return {};
    }
    const int edge = maxEdge > 0 ? maxEdge : 2048;

    {
        const QByteArray ladder = ThumtooCache::cachedLadderBytes(path, edge);
        if (!ladder.isEmpty()) {
#ifdef BILTOO_HAVE_VIPS
            QImage fromLadder = loadWithVipsBuffer(ladder, 0);
            if (!fromLadder.isNull()) {
                return scaleToMaxEdge(fromLadder, maxEdge);
            }
#endif
            QImage qtImg;
            if (qtImg.loadFromData(ladder)) {
                return scaleToMaxEdge(qtImg, maxEdge);
            }
        }
    }

    // Direct raster via ThumtooCache (no thumtoo/pdf.hpp in this TU).
    if (ThumtooCache::isAvailable()) {
        QImage img = ThumtooCache::rasterizePdfPage(ref.pdfPath, ref.page, edge);
        if (!img.isNull()) {
            ThumtooCache::schedulePixels(path, edge);
            return scaleToMaxEdge(img, maxEdge);
        }
        ThumtooCache::schedulePixels(path, edge);
        return {};
    }
    qWarning("ImageLoader: cannot load PDF page (no thumtoo/Poppler): %s", qPrintable(path));
    return {};
}

QImage loadWithQt(const QString &path, int maxEdge)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (maxEdge > 0) {
        const QSize size = reader.size();
        if (size.isValid()) {
            QSize scaled = size;
            scaled.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
            if (scaled != size) {
                reader.setScaledSize(scaled);
            }
        }
    }
    return reader.read();
}

#ifdef BILTOO_HAVE_VIPS

QImage vipsToQImage(VipsImage *inImage)
{
    if (!inImage) {
        return {};
    }

    VipsImage *srgb = nullptr;
    VipsImage *u8 = nullptr;
    VipsImage *joined = nullptr;
    VipsImage *current = inImage;

    // Prefer sRGB for display; ignore failure (some images are already suitable)
    if (vips_colourspace(current, &srgb, VIPS_INTERPRETATION_sRGB, nullptr) == 0) {
        current = srgb;
    }

    if (vips_cast(current, &u8, VIPS_FORMAT_UCHAR, nullptr) != 0) {
        if (srgb) {
            g_object_unref(srgb);
        }
        return {};
    }
    current = u8;

    const int bands = vips_image_get_bands(current);
    if (bands == 1) {
        // Expand grey → RGB
        VipsImage *parts[] = {current, current, current};
        if (vips_bandjoin(parts, &joined, 3, nullptr) != 0) {
            g_object_unref(u8);
            if (srgb) {
                g_object_unref(srgb);
            }
            return {};
        }
        current = joined;
    } else if (bands == 2) {
        // Grey + alpha → RGBA (duplicate grey into RGB channels + alpha).
        VipsImage *grey = nullptr;
        VipsImage *alpha = nullptr;
        if (vips_extract_band(current, &grey, 0, nullptr) != 0
            || vips_extract_band(current, &alpha, 1, nullptr) != 0) {
            if (grey) {
                g_object_unref(grey);
            }
            if (alpha) {
                g_object_unref(alpha);
            }
            g_object_unref(u8);
            if (srgb) {
                g_object_unref(srgb);
            }
            return {};
        }
        VipsImage *rgbParts[] = {grey, grey, grey, alpha};
        if (vips_bandjoin(rgbParts, &joined, 4, nullptr) != 0) {
            g_object_unref(grey);
            g_object_unref(alpha);
            g_object_unref(u8);
            if (srgb) {
                g_object_unref(srgb);
            }
            return {};
        }
        g_object_unref(grey);
        g_object_unref(alpha);
        current = joined;
    } else if (bands != 3 && bands != 4) {
        // Unsupported band count for display
        g_object_unref(u8);
        if (srgb) {
            g_object_unref(srgb);
        }
        return {};
    }

    const int width = vips_image_get_width(current);
    const int height = vips_image_get_height(current);
    const int outBands = vips_image_get_bands(current);
    size_t length = 0;
    void *data = vips_image_write_to_memory(current, &length);
    if (!data) {
        if (joined) {
            g_object_unref(joined);
        }
        g_object_unref(u8);
        if (srgb) {
            g_object_unref(srgb);
        }
        return {};
    }

    const QImage::Format format = (outBands == 4) ? QImage::Format_RGBA8888
                                                  : QImage::Format_RGB888;
    const int bpl = width * outBands;
    QImage wrapped(static_cast<const uchar *>(data), width, height, bpl, format);
    // Deep-copy so we own the pixels independently of the vips buffer
    QImage copy = wrapped.copy();
    g_free(data);

    if (joined) {
        g_object_unref(joined);
    }
    g_object_unref(u8);
    if (srgb) {
        g_object_unref(srgb);
    }
    return copy;
}

QImage loadWithVips(const QString &path, int maxEdge)
{
    const QByteArray file = QFile::encodeName(path);

    VipsImage *in = nullptr;
    if (maxEdge > 0) {
        // High-quality shrink-on-load when possible
        if (vips_thumbnail(file.constData(), &in, maxEdge,
                           "size", VIPS_SIZE_DOWN,
                           nullptr) != 0) {
            return {};
        }
    } else {
        in = vips_image_new_from_file(file.constData(), nullptr);
        if (!in) {
            return {};
        }
    }

    // AUDIT M18: match Qt QImageReader::setAutoTransform — honour EXIF orientation.
    VipsImage *rotated = nullptr;
    if (vips_autorot(in, &rotated, nullptr) == 0 && rotated) {
        g_object_unref(in);
        in = rotated;
    }

    QImage result = vipsToQImage(in);
    g_object_unref(in);
    return result;
}

QImage loadWithVipsBuffer(const QByteArray &bytes, int maxEdge)
{
    if (bytes.isEmpty()) {
        return {};
    }

    VipsImage *in = nullptr;
    if (maxEdge > 0) {
        if (vips_thumbnail_buffer(const_cast<void *>(static_cast<const void *>(bytes.constData())),
                                  size_t(bytes.size()), &in, maxEdge,
                                  "size", VIPS_SIZE_DOWN,
                                  nullptr) != 0) {
            in = nullptr;
        }
    }
    if (!in) {
        in = vips_image_new_from_buffer(bytes.constData(), size_t(bytes.size()), "",
                                        nullptr);
        if (!in) {
            return {};
        }
    }

    VipsImage *rotated = nullptr;
    if (vips_autorot(in, &rotated, nullptr) == 0 && rotated) {
        g_object_unref(in);
        in = rotated;
    }

    QImage result = vipsToQImage(in);
    g_object_unref(in);
    // maxEdge already applied via thumbnail_buffer when possible; otherwise scale.
    if (maxEdge > 0 && !result.isNull()
        && (result.width() > maxEdge || result.height() > maxEdge)) {
        result = result.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }
    return result;
}

#endif // BILTOO_HAVE_VIPS

QSize probeSizeWithQt(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize s = reader.size();
    if (s.isValid() && s.width() > 0 && s.height() > 0) {
        return s;
    }
    return {};
}

#ifdef BILTOO_HAVE_VIPS

QSize probeSizeWithVips(const QString &path)
{
    const QByteArray file = QFile::encodeName(path);
    // Header-first open — no full rasterisation for typical formats.
    VipsImage *in = vips_image_new_from_file(file.constData(),
                                             "access", VIPS_ACCESS_SEQUENTIAL,
                                             nullptr);
    if (!in) {
        in = vips_image_new_from_file(file.constData(), nullptr);
    }
    if (!in) {
        return {};
    }

    int w = vips_image_get_width(in);
    int h = vips_image_get_height(in);

    // Match load-time vips_autorot: orientations 5–8 swap display axes.
    int orientation = 0;
    if (vips_image_get_typeof(in, VIPS_META_ORIENTATION) != 0) {
        vips_image_get_int(in, VIPS_META_ORIENTATION, &orientation);
    }
    g_object_unref(in);

    if (orientation >= 5 && orientation <= 8) {
        const int tmp = w;
        w = h;
        h = tmp;
    }
    if (w <= 0 || h <= 0) {
        return {};
    }
    return QSize(w, h);
}

#endif // BILTOO_HAVE_VIPS

} // namespace

QSize probeSize(const QString &path)
{
    // Prefer durable thumtoo index (no source I/O) when a size is already known.
    if (const QSize cached = ThumtooCache::cachedSize(path); cached.isValid()) {
        return cached;
    }
    // Warm the cache in the background for the next visit (not Unsupported).
    if (!ThumtooCache::isUnsupported(path)) {
        ThumtooCache::scheduleProbe(path);
    }

    // When thumtoo is available, do not open the source for size — sizeReady
    // delivers the result. Avoids dual I/O (probe worker + Qt/vips/extract).
    if (ThumtooCache::isAvailable()) {
        return {};
    }

    if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)) {
        // No thumtoo: cannot size container pages without backend support.
        return {};
    }
    if (path.isEmpty() || !QFile::exists(path)) {
        return {};
    }
    const QSize qt = probeSizeWithQt(path);
    if (qt.isValid()) {
        return qt;
    }
#ifdef BILTOO_HAVE_VIPS
    return probeSizeWithVips(path);
#else
    return {};
#endif
}

void init(const char *argv0)
{
#ifdef BILTOO_HAVE_VIPS
    if (VIPS_INIT(argv0)) {
        // Non-fatal: Qt-only decode still works
        g_warning("VIPS_INIT failed; continuing without libvips");
    }
#else
    Q_UNUSED(argv0);
#endif
}

bool hasVips()
{
#ifdef BILTOO_HAVE_VIPS
    return true;
#else
    return false;
#endif
}

#ifdef BILTOO_HAVE_VIPS
/** Internal: single VIPS smartcrop attention peak (normalized). */
static bool attentionPointVipsPeak(const QImage &image, QPointF *normalizedOut)
{
    if (!normalizedOut || image.isNull()) {
        return false;
    }
    QImage src = image;
    constexpr int kMaxEdge = 512;
    if (src.width() > kMaxEdge || src.height() > kMaxEdge) {
        src = src.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    src = src.convertToFormat(QImage::Format_RGB888);
    if (src.isNull() || src.bytesPerLine() < src.width() * 3) {
        return false;
    }
    const int width = src.width();
    const int height = src.height();
    const int bands = 3;
    const size_t rowBytes = size_t(width) * size_t(bands);
    const size_t nbytes = rowBytes * size_t(height);
    void *buf = g_malloc(nbytes);
    if (!buf) {
        return false;
    }
    for (int y = 0; y < height; ++y) {
        memcpy(static_cast<uchar *>(buf) + size_t(y) * rowBytes,
               src.constScanLine(y), rowBytes);
    }
    VipsImage *in = vips_image_new_from_memory_copy(buf, nbytes, width, height, bands,
                                                    VIPS_FORMAT_UCHAR);
    g_free(buf);
    if (!in) {
        return false;
    }
    const int cropEdge = qBound(8, qMin(width, height) / 3, qMin(width, height));
    VipsImage *out = nullptr;
    int attentionX = 0;
    int attentionY = 0;
    if (vips_smartcrop(in, &out, cropEdge, cropEdge,
                       "interesting", VIPS_INTERESTING_ATTENTION,
                       "attention_x", &attentionX,
                       "attention_y", &attentionY,
                       nullptr) != 0
        || !out) {
        g_object_unref(in);
        return false;
    }
    *normalizedOut = QPointF(qBound(0.0, qreal(attentionX) / qreal(width), 1.0),
                             qBound(0.0, qreal(attentionY) / qreal(height), 1.0));
    g_object_unref(out);
    g_object_unref(in);
    return true;
}
#endif

bool attentionPoints(const QImage &image, QVector<QPointF> *normalizedOut, int maxPoints)
{
    if (!normalizedOut || image.isNull() || image.width() < 8 || image.height() < 8) {
        return false;
    }
    maxPoints = qBound(1, maxPoints, 16);
    normalizedOut->clear();

    constexpr int kMaxEdge = 256;
    QImage src = image;
    if (src.width() > kMaxEdge || src.height() > kMaxEdge) {
        src = src.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    src = src.convertToFormat(QImage::Format_Grayscale8);
    const int w = src.width();
    const int h = src.height();
    if (w < 8 || h < 8) {
        return false;
    }

    QVector<float> score(w * h, 0.0f);
    constexpr int kR = 3;
    for (int y = 0; y < h; ++y) {
        const uchar *row = src.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int n = 0;
            for (int dy = -kR; dy <= kR; ++dy) {
                const int yy = qBound(0, y + dy, h - 1);
                const uchar *r2 = src.constScanLine(yy);
                for (int dx = -kR; dx <= kR; ++dx) {
                    const int xx = qBound(0, x + dx, w - 1);
                    sum += r2[xx];
                    ++n;
                }
            }
            const float mean = float(sum) / float(n);
            score[y * w + x] = qAbs(float(row[x]) - mean);
        }
    }

#ifdef BILTOO_HAVE_VIPS
    {
        QPointF vipsPt;
        if (attentionPointVipsPeak(image, &vipsPt)) {
            const int px = qBound(0, int(vipsPt.x() * (w - 1)), w - 1);
            const int py = qBound(0, int(vipsPt.y() * (h - 1)), h - 1);
            score[py * w + px] = qMax(score[py * w + px], 255.0f);
        }
    }
#endif

    struct Peak { float s; int x; int y; };
    QVector<Peak> peaks;
    constexpr int kNms = 12;
    for (int y = kNms; y < h - kNms; ++y) {
        for (int x = kNms; x < w - kNms; ++x) {
            const float s = score[y * w + x];
            if (s < 8.0f) {
                continue;
            }
            bool isMax = true;
            for (int dy = -kNms; dy <= kNms && isMax; ++dy) {
                for (int dx = -kNms; dx <= kNms; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    if (score[(y + dy) * w + (x + dx)] > s) {
                        isMax = false;
                        break;
                    }
                }
            }
            if (isMax) {
                peaks.append({s, x, y});
            }
        }
    }
    std::sort(peaks.begin(), peaks.end(),
              [](const Peak &a, const Peak &b) { return a.s > b.s; });

    for (const Peak &pk : peaks) {
        if (normalizedOut->size() >= maxPoints) {
            break;
        }
        const QPointF n(qreal(pk.x) / qreal(qMax(1, w - 1)),
                        qreal(pk.y) / qreal(qMax(1, h - 1)));
        bool near = false;
        for (const QPointF &ex : *normalizedOut) {
            if (QLineF(ex, n).length() < 0.08) {
                near = true;
                break;
            }
        }
        if (!near) {
            normalizedOut->append(n);
        }
    }

    if (normalizedOut->isEmpty()) {
        normalizedOut->append(QPointF(0.5, 0.5));
    }
    return true;
}

bool attentionPoint(const QImage &image, QPointF *normalizedOut)
{
    if (!normalizedOut) {
        return false;
    }
    QVector<QPointF> pts;
    if (!attentionPoints(image, &pts, 1) || pts.isEmpty()) {
        return false;
    }
    *normalizedOut = pts.first();
    return true;
}

bool autoTrimRect(const QImage &image, const QRect &searchWithin, QRect *trimmedOut,
                  int colorThreshold, int noisePercent)
{
    if (!trimmedOut || image.isNull()) {
        return false;
    }
    const QRect imgRect(0, 0, image.width(), image.height());
    QRect area = searchWithin.intersected(imgRect).normalized();
    if (area.width() < 2 || area.height() < 2) {
        return false;
    }
    colorThreshold = qBound(0, colorThreshold, 255);
    noisePercent = qBound(0, noisePercent, 50);

    QImage src = image;
    if (src.format() != QImage::Format_RGB32
        && src.format() != QImage::Format_ARGB32
        && src.format() != QImage::Format_ARGB32_Premultiplied) {
        src = src.convertToFormat(QImage::Format_ARGB32);
    }

    auto sample = [&](int x, int y, int *r, int *g, int *b) {
        const QRgb px = src.pixel(x, y);
        *r = qRed(px);
        *g = qGreen(px);
        *b = qBlue(px);
        // Treat near-transparent as background contribution of pure white so
        // scans with alpha still trim empty margins.
        if (qAlpha(px) < 8) {
            *r = *g = *b = 255;
        }
    };

    // Border samples for median background (edges of the search rect).
    QVector<int> rs, gs, bs;
    rs.reserve(area.width() * 2 + area.height() * 2);
    gs.reserve(rs.capacity());
    bs.reserve(rs.capacity());
    auto push = [&](int x, int y) {
        int r, g, b;
        sample(x, y, &r, &g, &b);
        rs.append(r);
        gs.append(g);
        bs.append(b);
    };
    for (int x = area.left(); x <= area.right(); ++x) {
        push(x, area.top());
        if (area.height() > 1) {
            push(x, area.bottom());
        }
    }
    for (int y = area.top() + 1; y <= area.bottom() - 1; ++y) {
        push(area.left(), y);
        if (area.width() > 1) {
            push(area.right(), y);
        }
    }
    if (rs.isEmpty()) {
        return false;
    }
    auto medianOf = [](QVector<int> v) -> int {
        std::sort(v.begin(), v.end());
        return v.at(v.size() / 2);
    };
    const int bgR = medianOf(rs);
    const int bgG = medianOf(gs);
    const int bgB = medianOf(bs);

    auto isContent = [&](int x, int y) -> bool {
        int r, g, b;
        sample(x, y, &r, &g, &b);
        const int dr = qAbs(r - bgR);
        const int dg = qAbs(g - bgG);
        const int db = qAbs(b - bgB);
        return qMax(dr, qMax(dg, db)) > colorThreshold;
    };

    auto columnHasContent = [&](int x, int y0, int y1) -> bool {
        const int h = y1 - y0 + 1;
        if (h <= 0) {
            return false;
        }
        int hits = 0;
        for (int y = y0; y <= y1; ++y) {
            if (isContent(x, y)) {
                ++hits;
            }
        }
        // Need more than noisePercent of the column to count as content.
        return hits * 100 > noisePercent * h;
    };
    auto rowHasContent = [&](int y, int x0, int x1) -> bool {
        const int w = x1 - x0 + 1;
        if (w <= 0) {
            return false;
        }
        int hits = 0;
        for (int x = x0; x <= x1; ++x) {
            if (isContent(x, y)) {
                ++hits;
            }
        }
        return hits * 100 > noisePercent * w;
    };

    int left = area.left();
    int right = area.right();
    int top = area.top();
    int bottom = area.bottom();

    while (left < right && !columnHasContent(left, top, bottom)) {
        ++left;
    }
    while (right > left && !columnHasContent(right, top, bottom)) {
        --right;
    }
    while (top < bottom && !rowHasContent(top, left, right)) {
        ++top;
    }
    while (bottom > top && !rowHasContent(bottom, left, right)) {
        --bottom;
    }

    if (left > right || top > bottom) {
        return false;
    }
    *trimmedOut = QRect(QPoint(left, top), QPoint(right, bottom));
    return trimmedOut->isValid() && !trimmedOut->isEmpty();
}


QImage load(const QString &path)
{
    if (PagePath::isPageRef(path)) {
        return loadPageRef(path, 0);
    }
    if (ArchivePath::isArchiveRef(path)) {
        return loadArchiveRef(path, 0);
    }
    QImage image = loadWithQt(path, 0);
    if (!image.isNull()) {
        return image;
    }
#ifdef BILTOO_HAVE_VIPS
    return loadWithVips(path, 0);
#else
    return {};
#endif
}

QImage loadThumbnail(const QString &path, int maxEdge)
{
    if (maxEdge <= 0) {
        return load(path);
    }

    // Durable ladder first (no source I/O). Decode JXL/etc. via the same
    // buffer path used for archive members.
    {
        const QByteArray ladder = ThumtooCache::cachedLadderBytes(path, maxEdge);
        if (!ladder.isEmpty()) {
#ifdef BILTOO_HAVE_VIPS
            QImage fromLadder = loadWithVipsBuffer(ladder, 0);
            if (!fromLadder.isNull()) {
                if (fromLadder.width() > maxEdge || fromLadder.height() > maxEdge) {
                    fromLadder = fromLadder.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                }
                return fromLadder;
            }
#endif
            QImage qtImg;
            if (qtImg.loadFromData(ladder)) {
                if (qtImg.width() > maxEdge || qtImg.height() > maxEdge) {
                    qtImg = qtImg.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
                }
                return qtImg;
            }
        }
        // Miss: ask thumtoo to build the level in the background for next time.
        ThumtooCache::schedulePixels(path, maxEdge);
    }

    if (PagePath::isPageRef(path)) {
        if (ThumtooCache::isAvailable()) {
            return {};
        }
        return loadPageRef(path, maxEdge);
    }
    if (ArchivePath::isArchiveRef(path)) {
        // With thumtoo: do not extract+decode here (doubles work with schedulePixels).
        // ladderReady / a later pass fills the filmstrip cell from the durable ladder.
        if (ThumtooCache::isAvailable()) {
            return {};
        }
        return loadArchiveRef(path, maxEdge);
    }

    QImage image = loadWithQt(path, maxEdge);
    if (!image.isNull()) {
        return image;
    }
#ifdef BILTOO_HAVE_VIPS
    return loadWithVips(path, maxEdge);
#else
    return {};
#endif
}


QImage cachedThumbnail(const QString &path)
{
    return ImageCache::get(path);
}

void putCachedThumbnail(const QString &path, const QImage &image)
{
    ImageCache::put(path, image);
}

QImage loadThumbnailCached(const QString &path, int maxEdge)
{
    // Prefer a ready frame; schedule upgrade via ensure when too small.
    QImage hit = ImageCache::get(path, maxEdge);
    if (!hit.isNull()) {
        return hit;
    }
    return ImageCache::ensure(path, maxEdge);
}

QStringList imageSuffixes()
{
    static const QStringList base = {
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("bmp"),  QStringLiteral("gif"),  QStringLiteral("webp"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("svg"),
        QStringLiteral("xpm"),  QStringLiteral("pbm"),  QStringLiteral("pgm"),
        QStringLiteral("ppm"),  QStringLiteral("ico"),  QStringLiteral("xbm"),
        // Common formats often provided by libvips / system codecs
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
        QStringLiteral("jxl"),  QStringLiteral("jp2"),  QStringLiteral("j2k"),
        QStringLiteral("exr"),  QStringLiteral("hdr"),  QStringLiteral("pic"),
        QStringLiteral("tga"),  QStringLiteral("pcx"),  QStringLiteral("psd"),
        QStringLiteral("dds"),  QStringLiteral("fits"), QStringLiteral("fit"),
        QStringLiteral("vips"),
        // KDE KImageFormats plugins (runtime): GIMP XCF, Krita, OpenRaster, …
        QStringLiteral("xcf"),  QStringLiteral("kra"),  QStringLiteral("ora")
    };

    static const QStringList suffixes = [] {
        QStringList list = base;
        for (const QByteArray &fmt : QImageReader::supportedImageFormats()) {
            const QString s = QString::fromLatin1(fmt).toLower();
            if (!list.contains(s)) {
                list.append(s);
            }
        }
        return list;
    }();

    return suffixes;
}

bool isImageFile(const QString &path)
{
    if (PagePath::isPageRef(path)) {
        return PagePath::parse(path).valid;
    }
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (!ref.valid) {
            return false;
        }
        const QString suffix = QFileInfo(ref.memberPath).suffix().toLower();
        if (suffix.isEmpty()) {
            return false;
        }
        return imageSuffixes().contains(suffix);
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        return false;
    }
    return imageSuffixes().contains(suffix);
}

} // namespace ImageLoader
