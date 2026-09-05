// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DEFAULTAPPS_H
#define DEFAULTAPPS_H

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Linux default-application helpers via GLib GIO (optional at build time).
 * Without BILTOO_HAVE_GIO, queries return empty and setDefaultForType fails.
 */
namespace DefaultApps {

struct MimeStatus {
    QString mimeType;
    QString label;           // short UI label
    QString currentAppId;    // desktop id of current default, if any
    QString currentAppName;  // display name
    bool isUs = false;     // true when current default is biltoo.desktop
};

/** MIME types we advertise in biltoo.desktop and offer in Preferences. */
QStringList supportedMimeTypes();

/** Desktop file id used for associations (must match installed .desktop). */
QString desktopFileId();

bool isAvailable();

MimeStatus statusForType(const QString &mimeType);

QVector<MimeStatus> statusForSupportedTypes();

/**
 * Move biltoo to the front of the ordered default-handler list for @p mimeType
 * (XDG mimeapps.list [Default Applications]). Other handlers stay in the list.
 * Returns true on success; @p errorMessage receives a translated reason on failure.
 */
bool setDefaultForType(const QString &mimeType, QString *errorMessage = nullptr);

/** Set default for every type in @p mimeTypes; returns count succeeded. */
int setDefaultForTypes(const QStringList &mimeTypes, QStringList *errors = nullptr);

/**
 * Remove biltoo from the default-handler list for @p mimeType without wiping
 * other apps' associations. The next entry (if any) becomes preferred.
 * Returns true on success; @p errorMessage receives a translated reason on failure.
 */
bool clearDefaultForType(const QString &mimeType, QString *errorMessage = nullptr);

/** Clear user defaults for every type in @p mimeTypes; returns count succeeded. */
int clearDefaultForTypes(const QStringList &mimeTypes, QStringList *errors = nullptr);

} // namespace DefaultApps

#endif // DEFAULTAPPS_H
