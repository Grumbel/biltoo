// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// libvips pulls in GLib, which has struct fields named "signals". Qt defines
// signals as a macro — include vips before any Qt headers.
#ifdef QIMGVIEW_HAVE_VIPS
#include <vips/vips.h>
#endif

#include "imageloader.h"
#include "archivepath.h"
#include "archivereader.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
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

#ifdef QIMGVIEW_HAVE_VIPS
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

#ifdef QIMGVIEW_HAVE_VIPS
    // 3) libvips buffer path (WebP/HEIF/etc. when Qt plugins miss buffer loads).
    image = loadWithVipsBuffer(bytes, maxEdge);
    if (!image.isNull()) {
        return image;
    }
#endif
    return {};
}

QImage loadArchiveRef(const QString &path, int maxEdge)
{
    if (!ArchiveReader::isAvailable()) {
        qWarning("ImageLoader: archive path but libarchive not built in: %s",
                 qPrintable(path));
        return {};
    }
    const ArchivePath::Ref ref = ArchivePath::parse(path);
    if (!ref.valid) {
        qWarning("ImageLoader: invalid archive ref: %s", qPrintable(path));
        return {};
    }
    const QByteArray bytes = ArchiveReader::readMember(ref.archivePath, ref.memberPath);
    if (bytes.isEmpty()) {
        qWarning("ImageLoader: empty archive member %s from %s",
                 qPrintable(ref.memberPath), qPrintable(ref.archivePath));
        return {};
    }
    const QString suffix = QFileInfo(ref.memberPath).suffix().toLower();
    QImage image = decodeFromBytes(bytes, suffix, maxEdge);
    if (image.isNull()) {
        const QByteArray head = bytes.left(12).toHex(' ');
        qWarning("ImageLoader: decode failed for %s (%d bytes, suffix=%s, head=%s)",
                 qPrintable(path), bytes.size(), qPrintable(suffix), head.constData());
    }
    return image;
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

#ifdef QIMGVIEW_HAVE_VIPS

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

#endif // QIMGVIEW_HAVE_VIPS

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

#ifdef QIMGVIEW_HAVE_VIPS

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

#endif // QIMGVIEW_HAVE_VIPS

} // namespace

QSize probeSize(const QString &path)
{
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (!ref.valid || !ArchiveReader::isAvailable()) {
            return {};
        }
        const QByteArray bytes = ArchiveReader::readMember(ref.archivePath, ref.memberPath);
        if (bytes.isEmpty()) {
            return {};
        }
        const QString suffix = QFileInfo(ref.memberPath).suffix().toLower();
        for (const QString &fmt : formatCandidates(suffix)) {
            QByteArray data = bytes;
            QBuffer buffer(&data);
            if (!buffer.open(QIODevice::ReadOnly)) {
                continue;
            }
            QImageReader reader(&buffer);
            reader.setAutoTransform(true);
            reader.setFormat(fmt.toLatin1());
            const QSize sz = reader.size();
            if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
                return sz;
            }
        }
#ifdef QIMGVIEW_HAVE_VIPS
        {
            VipsImage *in = vips_image_new_from_buffer(bytes.constData(), size_t(bytes.size()),
                                                       "", nullptr);
            if (in) {
                const QSize sz(vips_image_get_width(in), vips_image_get_height(in));
                g_object_unref(in);
                if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
                    return sz;
                }
            }
        }
#endif
        return {};
    }
    if (path.isEmpty() || !QFile::exists(path)) {
        return {};
    }
    const QSize qt = probeSizeWithQt(path);
    if (qt.isValid()) {
        return qt;
    }
#ifdef QIMGVIEW_HAVE_VIPS
    return probeSizeWithVips(path);
#else
    return {};
#endif
}

void init(const char *argv0)
{
#ifdef QIMGVIEW_HAVE_VIPS
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
#ifdef QIMGVIEW_HAVE_VIPS
    return true;
#else
    return false;
#endif
}

QImage load(const QString &path)
{
    if (ArchivePath::isArchiveRef(path)) {
        return loadArchiveRef(path, 0);
    }
    QImage image = loadWithQt(path, 0);
    if (!image.isNull()) {
        return image;
    }
#ifdef QIMGVIEW_HAVE_VIPS
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
    if (ArchivePath::isArchiveRef(path)) {
        return loadArchiveRef(path, maxEdge);
    }

    QImage image = loadWithQt(path, maxEdge);
    if (!image.isNull()) {
        return image;
    }
#ifdef QIMGVIEW_HAVE_VIPS
    return loadWithVips(path, maxEdge);
#else
    return {};
#endif
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
