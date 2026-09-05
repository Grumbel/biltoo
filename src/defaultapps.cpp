// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// GIO before Qt: GLib structs use a field named "signals", which collides
// with Qt's signals macro if Qt headers are included first.
#ifdef BILTOO_HAVE_GIO
#  include <gio/gio.h>
#  include <gio/gdesktopappinfo.h>
#endif

#include "defaultapps.h"

#include <QCoreApplication>
#include <QHash>

namespace DefaultApps {

QString desktopFileId()
{
    return QStringLiteral("biltoo.desktop");
}

QStringList supportedMimeTypes()
{
    // Keep in sync with data/biltoo.desktop MimeType=
    return {
        // Images
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/png"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp"),
        QStringLiteral("image/webp"),
        QStringLiteral("image/tiff"),
        QStringLiteral("image/svg+xml"),
        QStringLiteral("image/x-xpixmap"),
        QStringLiteral("image/x-portable-anymap"),
        QStringLiteral("image/x-portable-bitmap"),
        QStringLiteral("image/x-portable-graymap"),
        QStringLiteral("image/x-portable-pixmap"),
        QStringLiteral("image/x-tga"),
        QStringLiteral("image/avif"),
        QStringLiteral("image/heic"),
        QStringLiteral("image/heif"),
        QStringLiteral("image/jxl"),
        // Archives (libarchive session expand)
        QStringLiteral("application/zip"),
        QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-rar"),
        QStringLiteral("application/x-rar-compressed"),
        QStringLiteral("application/x-tar"),
        QStringLiteral("application/x-compressed-tar"),
        QStringLiteral("application/x-gtar"),
        QStringLiteral("application/gzip"),
        QStringLiteral("application/x-bzip-compressed-tar"),
        QStringLiteral("application/x-bzip2"),
        QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-xz"),
        QStringLiteral("application/x-zstd-compressed-tar"),
        QStringLiteral("application/zstd"),
        QStringLiteral("application/x-cpio"),
        QStringLiteral("application/x-iso9660-image"),
        QStringLiteral("application/x-cd-image"),
        QStringLiteral("application/vnd.ms-cab-compressed"),
        QStringLiteral("application/x-archive"),
        QStringLiteral("application/x-ar"),
    };
}

static QString labelForMime(const QString &mime)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("image/jpeg"), QObject::tr("JPEG")},
        {QStringLiteral("image/png"), QObject::tr("PNG")},
        {QStringLiteral("image/gif"), QObject::tr("GIF")},
        {QStringLiteral("image/bmp"), QObject::tr("BMP")},
        {QStringLiteral("image/webp"), QObject::tr("WebP")},
        {QStringLiteral("image/tiff"), QObject::tr("TIFF")},
        {QStringLiteral("image/svg+xml"), QObject::tr("SVG")},
        {QStringLiteral("image/x-xpixmap"), QObject::tr("XPM")},
        {QStringLiteral("image/x-portable-anymap"), QObject::tr("PNM")},
        {QStringLiteral("image/x-portable-bitmap"), QObject::tr("PBM")},
        {QStringLiteral("image/x-portable-graymap"), QObject::tr("PGM")},
        {QStringLiteral("image/x-portable-pixmap"), QObject::tr("PPM")},
        {QStringLiteral("image/x-tga"), QObject::tr("TGA")},
        {QStringLiteral("image/avif"), QObject::tr("AVIF")},
        {QStringLiteral("image/heic"), QObject::tr("HEIC")},
        {QStringLiteral("image/heif"), QObject::tr("HEIF")},
        {QStringLiteral("image/jxl"), QObject::tr("JPEG XL")},
        {QStringLiteral("application/zip"), QObject::tr("ZIP archive")},
        {QStringLiteral("application/x-7z-compressed"), QObject::tr("7z archive")},
        {QStringLiteral("application/vnd.rar"), QObject::tr("RAR archive")},
        {QStringLiteral("application/x-rar"), QObject::tr("RAR archive (legacy)")},
        {QStringLiteral("application/x-rar-compressed"), QObject::tr("RAR archive (compressed)")},
        {QStringLiteral("application/x-tar"), QObject::tr("Tar archive")},
        {QStringLiteral("application/x-compressed-tar"), QObject::tr("Tar.gz archive")},
        {QStringLiteral("application/x-gtar"), QObject::tr("GNU tar archive")},
        {QStringLiteral("application/gzip"), QObject::tr("Gzip")},
        {QStringLiteral("application/x-bzip-compressed-tar"), QObject::tr("Tar.bz2 archive")},
        {QStringLiteral("application/x-bzip2"), QObject::tr("Bzip2")},
        {QStringLiteral("application/x-xz-compressed-tar"), QObject::tr("Tar.xz archive")},
        {QStringLiteral("application/x-xz"), QObject::tr("XZ")},
        {QStringLiteral("application/x-zstd-compressed-tar"), QObject::tr("Tar.zst archive")},
        {QStringLiteral("application/zstd"), QObject::tr("Zstd")},
        {QStringLiteral("application/x-cpio"), QObject::tr("Cpio archive")},
        {QStringLiteral("application/x-iso9660-image"), QObject::tr("ISO image")},
        {QStringLiteral("application/x-cd-image"), QObject::tr("CD image")},
        {QStringLiteral("application/vnd.ms-cab-compressed"), QObject::tr("CAB archive")},
        {QStringLiteral("application/x-archive"), QObject::tr("Unix archive (.a)")},
        {QStringLiteral("application/x-ar"), QObject::tr("AR archive")},
    };
    return labels.value(mime, mime);
}

bool isAvailable()
{
#ifdef BILTOO_HAVE_GIO
    return true;
#else
    return false;
#endif
}

MimeStatus statusForType(const QString &mimeType)
{
    MimeStatus s;
    s.mimeType = mimeType;
    s.label = labelForMime(mimeType);
    s.isUs = false;

#ifdef BILTOO_HAVE_GIO
    GAppInfo *info = g_app_info_get_default_for_type(mimeType.toUtf8().constData(), FALSE);
    if (info) {
        const char *id = g_app_info_get_id(info);
        const char *name = g_app_info_get_display_name(info);
        if (id) {
            s.currentAppId = QString::fromUtf8(id);
        }
        if (name) {
            s.currentAppName = QString::fromUtf8(name);
        }
        if (s.currentAppId == desktopFileId()) {
            s.isUs = true;
        }
        g_object_unref(info);
    }
#else
    Q_UNUSED(mimeType);
#endif
    return s;
}

QVector<MimeStatus> statusForSupportedTypes()
{
    QVector<MimeStatus> out;
    const QStringList types = supportedMimeTypes();
    out.reserve(types.size());
    for (const QString &m : types) {
        out.append(statusForType(m));
    }
    return out;
}

bool setDefaultForType(const QString &mimeType, QString *errorMessage)
{
#ifdef BILTOO_HAVE_GIO
    GDesktopAppInfo *desk = g_desktop_app_info_new(desktopFileId().toUtf8().constData());
    if (!desk) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate(
                "DefaultApps",
                "Could not find %1. Is Biltoo installed system-wide?")
                .arg(desktopFileId());
        }
        return false;
    }

    GError *err = nullptr;
    const gboolean ok = g_app_info_set_as_default_for_type(
        G_APP_INFO(desk), mimeType.toUtf8().constData(), &err);
    g_object_unref(desk);

    if (!ok) {
        if (errorMessage) {
            if (err) {
                *errorMessage = QString::fromUtf8(err->message);
            } else {
                *errorMessage = QCoreApplication::translate(
                    "DefaultApps", "Failed to set default application.");
            }
        }
        if (err) {
            g_error_free(err);
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(mimeType);
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate(
            "DefaultApps",
            "Setting default applications requires GLib GIO (not enabled in this build).");
    }
    return false;
#endif
}

int setDefaultForTypes(const QStringList &mimeTypes, QStringList *errors)
{
    int ok = 0;
    for (const QString &m : mimeTypes) {
        QString err;
        if (setDefaultForType(m, &err)) {
            ++ok;
        } else if (errors) {
            errors->append(QStringLiteral("%1: %2").arg(m, err));
        }
    }
    return ok;
}

bool clearDefaultForType(const QString &mimeType, QString *errorMessage)
{
#ifdef BILTOO_HAVE_GIO
    // Only reset when Biltoo is the current default. Resetting while another
    // app is default would wipe that user choice with no benefit to us.
    // g_app_info_reset_type_associations removes all user overrides for the type;
    // GIO has no API to demote a single app while keeping other overrides.
    if (!statusForType(mimeType).isUs) {
        Q_UNUSED(errorMessage);
        return true;
    }
    g_app_info_reset_type_associations(mimeType.toUtf8().constData());
    Q_UNUSED(errorMessage);
    return true;
#else
    Q_UNUSED(mimeType);
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate(
            "DefaultApps",
            "Clearing default applications requires GLib GIO (not enabled in this build).");
    }
    return false;
#endif
}

int clearDefaultForTypes(const QStringList &mimeTypes, QStringList *errors)
{
    int ok = 0;
    for (const QString &m : mimeTypes) {
        QString err;
        if (clearDefaultForType(m, &err)) {
            ++ok;
        } else if (errors) {
            errors->append(QStringLiteral("%1: %2").arg(m, err));
        }
    }
    return ok;
}

} // namespace DefaultApps
