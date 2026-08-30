// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// GIO before Qt: GLib structs use a field named "signals", which collides
// with Qt's signals macro if Qt headers are included first.
#ifdef QIMGVIEW_HAVE_GIO
#  include <gio/gio.h>
#  include <gio/gdesktopappinfo.h>
#endif

#include "defaultapps.h"

#include <QCoreApplication>
#include <QHash>

namespace DefaultApps {

QString desktopFileId()
{
    return QStringLiteral("qimgview.desktop");
}

QStringList supportedMimeTypes()
{
    // Keep in sync with data/qimgview.desktop MimeType=
    return {
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
    };
    return labels.value(mime, mime);
}

bool isAvailable()
{
#ifdef QIMGVIEW_HAVE_GIO
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

#ifdef QIMGVIEW_HAVE_GIO
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
#ifdef QIMGVIEW_HAVE_GIO
    GDesktopAppInfo *desk = g_desktop_app_info_new(desktopFileId().toUtf8().constData());
    if (!desk) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate(
                "DefaultApps",
                "Could not find %1. Is QImgView installed system-wide?")
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
#ifdef QIMGVIEW_HAVE_GIO
    // Only reset when QImgView is the current default. Resetting while another
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
