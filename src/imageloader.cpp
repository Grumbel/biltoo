// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// libvips pulls in GLib, which has struct fields named "signals". Qt defines
// signals as a macro — include vips before any Qt headers.
#ifdef QIMGVIEW_HAVE_VIPS
#include <vips/vips.h>
#endif

#include "imageloader.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

namespace ImageLoader {
namespace {

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
        // Grey + alpha → RGBA (duplicate grey into RGB)
        VipsImage *rgb = nullptr;
        VipsImage *parts[] = {current, current, current};
        // Extract band 0 three times is wrong for bandjoin of same image with 2 bands
        // Use bandmean / extract
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

    QImage result = vipsToQImage(in);
    g_object_unref(in);
    return result;
}

#endif // QIMGVIEW_HAVE_VIPS

} // namespace

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
        QStringLiteral("vips")
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
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        return false;
    }
    return imageSuffixes().contains(suffix);
}

} // namespace ImageLoader
