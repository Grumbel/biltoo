// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef OPENWITH_H
#define OPENWITH_H

#include <QString>
#include <QVector>
#include <QWidget>

class QMenu;

/**
 * Open a local file with a user-chosen application (XDG / GIO).
 *
 * Lists every associated handler for the file's MIME type — not only the
 * default — the same way a file manager "Open with…" menu works.
 *
 * Session paths that are page refs (PDF/EPUB//page:N) or archive members
 * are resolved to the container file on disk before MIME lookup and launch.
 */
namespace OpenWith {

struct App {
    QString id;   /**< Desktop id (e.g. org.gnome.eog.desktop) or synthetic. */
    QString name; /**< Display name. */
    QString icon; /**< Theme icon name when known. */
};

/**
 * Resolve a session path to a local filesystem path suitable for opening.
 * Page refs → document container; archive members → archive container;
 * plain paths → local path (file:// stripped). Empty if not openable.
 */
QString openableLocalPath(const QString &sessionPath);

/** MIME type for a local path (extension-first; GIO content when available). */
QString mimeForLocalPath(const QString &localPath);

/** All applications registered for @p mimeType (defaults + associations). */
QVector<App> appsForMime(const QString &mimeType);

/** appsForMime(mimeForLocalPath(@p localPath)). */
QVector<App> appsForLocalPath(const QString &localPath);

/** Launch @p app with a single local file path. */
bool launch(const App &app, const QString &localPath);

/** Default handler for the path's MIME type (GIO / xdg-open fallback). */
bool openDefault(const QString &localPath);

/**
 * Prompt for a shell command and run it with @p localPath as argument.
 * Returns true if the user accepted and the process was started.
 */
bool openWithCommandDialog(QWidget *parent, const QString &localPath);

/**
 * Populate @p menu with associated apps + "Other Application…".
 * Defers MIME/desktop scan until aboutToShow (responsive right-click).
 * Clears and rebuilds the menu; safe to call repeatedly.
 */
void populateMenu(QMenu *menu, const QString &localPath, QWidget *parent);

/**
 * Open the parent directory of @p localPath in the default file manager.
 */
bool openContainingFolder(const QString &localPath);

} // namespace OpenWith

#endif // OPENWITH_H
