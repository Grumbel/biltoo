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
#include <QVector>
#include <QByteArrayList>

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


QStringList imageMimeTypes()
{
    QStringList out;
    for (const QString &m : supportedMimeTypes()) {
        if (m.startsWith(QLatin1String("image/"))) {
            out.append(m);
        }
    }
    return out;
}

QStringList archiveMimeTypes()
{
    QStringList out;
    for (const QString &m : supportedMimeTypes()) {
        if (!m.startsWith(QLatin1String("image/"))) {
            out.append(m);
        }
    }
    return out;
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

#ifdef BILTOO_HAVE_GIO
/**
 * XDG mimeapps.list: [Default Applications] values are an ordered list of
 * desktop ids (semicolon-separated). GLib's g_app_info_set_as_default_for_type
 * writes a *single* id and drops the rest. We prepend biltoo and keep others.
 * Clear removes only biltoo from that list (no reset_type_associations wipe).
 */
static QString userMimeappsPath()
{
    const char *dir = g_get_user_config_dir();
    if (!dir || !dir[0]) {
        return {};
    }
    return QString::fromUtf8(dir) + QStringLiteral("/mimeapps.list");
}

static QStringList keyFileStringList(GKeyFile *kf, const char *group, const char *key)
{
    QStringList out;
    gsize len = 0;
    GError *err = nullptr;
    gchar **list = g_key_file_get_string_list(kf, group, key, &len, &err);
    if (err) {
        g_error_free(err);
        return out;
    }
    if (list) {
        for (gsize i = 0; i < len; ++i) {
            if (list[i] && list[i][0]) {
                out.append(QString::fromUtf8(list[i]));
            }
        }
        g_strfreev(list);
    }
    return out;
}

static void keyFileSetStringList(GKeyFile *kf, const char *group, const char *key,
                                 const QStringList &ids)
{
    if (ids.isEmpty()) {
        g_key_file_remove_key(kf, group, key, nullptr);
        return;
    }
    QByteArrayList utf8;
    utf8.reserve(ids.size());
    for (const QString &id : ids) {
        utf8.append(id.toUtf8());
    }
    QVector<const gchar *> ptrs;
    ptrs.reserve(utf8.size());
    for (const QByteArray &b : utf8) {
        ptrs.append(b.constData());
    }
    g_key_file_set_string_list(kf, group, key, ptrs.constData(),
                               static_cast<gsize>(ptrs.size()));
}

/** Prepend @p desktopId in Default Applications; ensure Added Associations. */
static bool mimeappsPrependDefault(const QString &mimeType, const QString &desktopId,
                                   QString *errorMessage)
{
    const QString path = userMimeappsPath();
    if (path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate(
                "DefaultApps", "Could not resolve user config directory.");
        }
        return false;
    }
    const QByteArray pathUtf8 = path.toUtf8();
    const QByteArray mimeUtf8 = mimeType.toUtf8();
    const char *mimeKey = mimeUtf8.constData();

    GKeyFile *kf = g_key_file_new();
    // Missing file is fine (first association).
    g_key_file_load_from_file(kf, pathUtf8.constData(),
                              static_cast<GKeyFileFlags>(G_KEY_FILE_KEEP_COMMENTS
                                                         | G_KEY_FILE_KEEP_TRANSLATIONS),
                              nullptr);

    QStringList defaults = keyFileStringList(kf, "Default Applications", mimeKey);
    defaults.removeAll(desktopId);
    defaults.prepend(desktopId);
    keyFileSetStringList(kf, "Default Applications", mimeKey, defaults);

    QStringList added = keyFileStringList(kf, "Added Associations", mimeKey);
    if (!added.contains(desktopId)) {
        added.prepend(desktopId);
        keyFileSetStringList(kf, "Added Associations", mimeKey, added);
    }

    // Drop from Removed Associations if present (re-enable).
    QStringList removed = keyFileStringList(kf, "Removed Associations", mimeKey);
    if (removed.removeAll(desktopId) > 0) {
        keyFileSetStringList(kf, "Removed Associations", mimeKey, removed);
    }

    GError *err = nullptr;
    const gboolean ok = g_key_file_save_to_file(kf, pathUtf8.constData(), &err);
    g_key_file_free(kf);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = err ? QString::fromUtf8(err->message)
                                : QCoreApplication::translate(
                                      "DefaultApps", "Failed to write mimeapps.list.");
        }
        if (err) {
            g_error_free(err);
        }
        return false;
    }
    return true;
}

/** Remove @p desktopId from Default Applications (and Added Associations). */
static bool mimeappsRemoveDefault(const QString &mimeType, const QString &desktopId,
                                  QString *errorMessage)
{
    const QString path = userMimeappsPath();
    if (path.isEmpty()) {
        return true;
    }
    const QByteArray pathUtf8 = path.toUtf8();
    const QByteArray mimeUtf8 = mimeType.toUtf8();
    const char *mimeKey = mimeUtf8.constData();

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, pathUtf8.constData(),
                                   static_cast<GKeyFileFlags>(G_KEY_FILE_KEEP_COMMENTS
                                                              | G_KEY_FILE_KEEP_TRANSLATIONS),
                                   nullptr)) {
        g_key_file_free(kf);
        return true; // nothing to clear
    }

    QStringList defaults = keyFileStringList(kf, "Default Applications", mimeKey);
    defaults.removeAll(desktopId);
    keyFileSetStringList(kf, "Default Applications", mimeKey, defaults);

    QStringList added = keyFileStringList(kf, "Added Associations", mimeKey);
    if (added.removeAll(desktopId) > 0) {
        keyFileSetStringList(kf, "Added Associations", mimeKey, added);
    }

    GError *err = nullptr;
    const gboolean ok = g_key_file_save_to_file(kf, pathUtf8.constData(), &err);
    g_key_file_free(kf);
    if (!ok) {
        if (errorMessage) {
            *errorMessage = err ? QString::fromUtf8(err->message)
                                : QCoreApplication::translate(
                                      "DefaultApps", "Failed to write mimeapps.list.");
        }
        if (err) {
            g_error_free(err);
        }
        return false;
    }
    return true;
}
#endif // BILTOO_HAVE_GIO

bool setDefaultForType(const QString &mimeType, QString *errorMessage)
{
#ifdef BILTOO_HAVE_GIO
    // Ensure the desktop file is visible to GIO (installed / local share).
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
    g_object_unref(desk);

    // Prepend to the ordered default list; do not replace other handlers.
    return mimeappsPrependDefault(mimeType, desktopFileId(), errorMessage);
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
    // Demote Biltoo only — keep other apps in the default list.
    return mimeappsRemoveDefault(mimeType, desktopFileId(), errorMessage);
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
