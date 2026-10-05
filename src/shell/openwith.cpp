// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// GIO before Qt: GLib structs use a field named "signals", which collides
// with Qt's signals macro if Qt headers are included first.
#ifdef BILTOO_HAVE_GIO
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#endif

#include "shell/openwith.h"

#include "host/archivepath.h"
#include "host/pagepath.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace OpenWith {
namespace {

QString stripFileUrl(const QString &path)
{
    if (path.startsWith(QLatin1String("file:"))) {
        const QUrl url(path);
        if (url.isLocalFile()) {
            return url.toLocalFile();
        }
    }
    return path;
}

// ---------------------------------------------------------------------------
// Desktop-file fallback (no GIO): same XDG sources a file manager uses.
// ---------------------------------------------------------------------------

QStringList xdgDataDirs()
{
    QStringList dirs;
    const QString home = QDir::homePath();
    dirs << (home + QStringLiteral("/.local/share"));
    const QByteArray xdg = qgetenv("XDG_DATA_DIRS");
    if (!xdg.isEmpty()) {
        for (const QByteArray &part : xdg.split(':')) {
            if (!part.isEmpty()) {
                dirs << QString::fromLocal8Bit(part);
            }
        }
    } else {
        dirs << QStringLiteral("/usr/local/share") << QStringLiteral("/usr/share");
    }
    return dirs;
}

QStringList mimeappsListPaths()
{
    QStringList out;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    out << (config + QStringLiteral("/mimeapps.list"));
    out << (QDir::homePath() + QStringLiteral("/.config/mimeapps.list"));
    for (const QString &d : xdgDataDirs()) {
        out << (d + QStringLiteral("/applications/mimeapps.list"));
        out << (d + QStringLiteral("/applications/mimeinfo.cache"));
    }
    return out;
}

QString findDesktopFile(const QString &id)
{
    if (id.isEmpty()) {
        return {};
    }
    if (QFileInfo::exists(id) && id.endsWith(QLatin1String(".desktop"))) {
        return id;
    }
    const QString name =
        id.endsWith(QLatin1String(".desktop")) ? id : (id + QStringLiteral(".desktop"));
    for (const QString &d : xdgDataDirs()) {
        const QString path = d + QStringLiteral("/applications/") + name;
        if (QFileInfo::exists(path)) {
            return path;
        }
        // Vendor subdirs (e.g. kde/, gnome/).
        const QDir appDir(d + QStringLiteral("/applications"));
        if (!appDir.exists()) {
            continue;
        }
        const QStringList matches =
            appDir.entryList(QStringList{name}, QDir::Files, QDir::Name);
        if (!matches.isEmpty()) {
            return appDir.filePath(matches.first());
        }
        // Nested: applications/foo/bar.desktop for id foo-bar.desktop is rare;
        // recursive walk is expensive — stick to flat + one level.
        for (const QString &sub : appDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString nested = appDir.filePath(sub + QLatin1Char('/') + name);
            if (QFileInfo::exists(nested)) {
                return nested;
            }
        }
    }
    return {};
}

App parseDesktopFile(const QString &path)
{
    App app;
    if (path.isEmpty()) {
        return app;
    }
    QSettings ini(path, QSettings::IniFormat);
    // QSettings treats .desktop as INI; group is "Desktop Entry".
    ini.beginGroup(QStringLiteral("Desktop Entry"));
    if (ini.value(QStringLiteral("NoDisplay")).toBool()
        || ini.value(QStringLiteral("Hidden")).toBool()) {
        return app;
    }
    const QString type = ini.value(QStringLiteral("Type")).toString();
    if (!type.isEmpty() && type != QLatin1String("Application")) {
        return app;
    }
    app.id = QFileInfo(path).fileName();
    app.name = ini.value(QStringLiteral("Name")).toString();
    app.icon = ini.value(QStringLiteral("Icon")).toString();
    // Exec is not stored on App; launch_desktop uses path lookup by id.
    Q_UNUSED(ini.value(QStringLiteral("Exec")));
    ini.endGroup();
    if (app.name.isEmpty()) {
        app = App{};
    }
    return app;
}

QString desktopExecLine(const QString &desktopPath)
{
    QSettings ini(desktopPath, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("Desktop Entry"));
    return ini.value(QStringLiteral("Exec")).toString();
}

QStringList expandExec(const QString &exec, const QString &localPath)
{
    // Minimal freedesktop Exec field code expansion for a single file.
    QStringList tokens;
    QString cur;
    bool inQuote = false;
    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && c.isSpace()) {
            if (!cur.isEmpty()) {
                tokens.append(cur);
                cur.clear();
            }
            continue;
        }
        if (c == QLatin1Char('%') && i + 1 < exec.size()) {
            const QChar code = exec.at(++i);
            if (code == QLatin1Char('%')) {
                cur.append(QLatin1Char('%'));
            } else if (code == QLatin1Char('f') || code == QLatin1Char('u')
                       || code == QLatin1Char('F') || code == QLatin1Char('U')) {
                cur.append(localPath);
            } else if (code == QLatin1Char('c') || code == QLatin1Char('k')
                       || code == QLatin1Char('i') || code == QLatin1Char('d')
                       || code == QLatin1Char('n') || code == QLatin1Char('v')
                       || code == QLatin1Char('m')) {
                // Drop unsupported codes and their values.
            } else {
                // Unknown: keep literal.
                cur.append(QLatin1Char('%'));
                cur.append(code);
            }
            continue;
        }
        cur.append(c);
    }
    if (!cur.isEmpty()) {
        tokens.append(cur);
    }
    // If no path field code was present, append the path (common for simple Exec).
    bool hasPath = false;
    for (const QString &t : tokens) {
        if (t == localPath) {
            hasPath = true;
            break;
        }
    }
    if (!hasPath) {
        tokens.append(localPath);
    }
    return tokens;
}

bool launchDesktopId(const QString &desktopId, const QString &localPath)
{
    const QString path = findDesktopFile(desktopId);
    if (path.isEmpty()) {
        return false;
    }
    const QString exec = desktopExecLine(path);
    if (exec.isEmpty()) {
        return false;
    }
    const QStringList tokens = expandExec(exec, localPath);
    if (tokens.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(tokens.first(), tokens.mid(1));
}

QStringList desktopIdsFromMimeapps(const QString &mimeType)
{
    QStringList ids;
    QSet<QString> seen;
    const QStringList sections = {
        QStringLiteral("Default Applications"),
        QStringLiteral("Added Associations"),
        QStringLiteral("MIME Cache"),
    };
    for (const QString &listPath : mimeappsListPaths()) {
        if (!QFileInfo::exists(listPath)) {
            continue;
        }
        QSettings ini(listPath, QSettings::IniFormat);
        for (const QString &section : sections) {
            ini.beginGroup(section);
            const QString raw = ini.value(mimeType).toString();
            ini.endGroup();
            if (raw.isEmpty()) {
                continue;
            }
            for (const QString &id : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QString trimmed = id.trimmed();
                if (trimmed.isEmpty() || seen.contains(trimmed)) {
                    continue;
                }
                seen.insert(trimmed);
                ids.append(trimmed);
            }
        }
    }
    return ids;
}

/** Scan applications/*.desktop for MimeType= containing @p mimeType. */
QStringList desktopIdsFromDesktopScan(const QString &mimeType)
{
    QStringList ids;
    QSet<QString> seen;
    for (const QString &d : xdgDataDirs()) {
        const QDir appDir(d + QStringLiteral("/applications"));
        if (!appDir.exists()) {
            continue;
        }
        QStringList files = appDir.entryList(QStringList{QStringLiteral("*.desktop")},
                                             QDir::Files);
        for (const QString &sub : appDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir subDir(appDir.filePath(sub));
            for (const QString &f :
                 subDir.entryList(QStringList{QStringLiteral("*.desktop")}, QDir::Files)) {
                files.append(sub + QLatin1Char('/') + f);
            }
        }
        for (const QString &rel : files) {
            const QString full = appDir.filePath(rel);
            QSettings ini(full, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));
            if (ini.value(QStringLiteral("NoDisplay")).toBool()
                || ini.value(QStringLiteral("Hidden")).toBool()) {
                continue;
            }
            const QString mimes = ini.value(QStringLiteral("MimeType")).toString();
            if (mimes.isEmpty()) {
                continue;
            }
            const QStringList parts = mimes.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            if (!parts.contains(mimeType)) {
                continue;
            }
            const QString id = QFileInfo(full).fileName();
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            ids.append(id);
        }
    }
    return ids;
}

QVector<App> appsForMimeFallback(const QString &mimeType)
{
    QVector<App> out;
    QSet<QString> seen;
    auto addId = [&](const QString &id) {
        if (id.isEmpty() || seen.contains(id)) {
            return;
        }
        const App app = parseDesktopFile(findDesktopFile(id));
        if (app.name.isEmpty()) {
            return;
        }
        seen.insert(id);
        out.append(app);
    };
    for (const QString &id : desktopIdsFromMimeapps(mimeType)) {
        addId(id);
    }
    for (const QString &id : desktopIdsFromDesktopScan(mimeType)) {
        addId(id);
    }
    std::sort(out.begin(), out.end(), [](const App &a, const App &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

#ifdef BILTOO_HAVE_GIO
QVector<App> appsForMimeGio(const QString &mimeType)
{
    QVector<App> out;
    GList *list = g_app_info_get_all_for_type(mimeType.toUtf8().constData());
    for (GList *l = list; l != nullptr; l = l->next) {
        GAppInfo *info = static_cast<GAppInfo *>(l->data);
        if (!info || !g_app_info_should_show(info)) {
            continue;
        }
        App app;
        if (const char *id = g_app_info_get_id(info)) {
            app.id = QString::fromUtf8(id);
        }
        if (const char *name = g_app_info_get_display_name(info)) {
            app.name = QString::fromUtf8(name);
        }
        if (app.name.isEmpty()) {
            continue;
        }
        // Icon name when GThemedIcon.
        GIcon *icon = g_app_info_get_icon(info);
        if (icon && G_IS_THEMED_ICON(icon)) {
            const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            if (names && names[0]) {
                app.icon = QString::fromUtf8(names[0]);
            }
        }
        out.append(app);
    }
    g_list_free_full(list, g_object_unref);
    std::sort(out.begin(), out.end(), [](const App &a, const App &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

bool launchGio(const App &app, const QString &localPath)
{
    GAppInfo *info = nullptr;
    if (!app.id.isEmpty()) {
        info = G_APP_INFO(g_desktop_app_info_new(app.id.toUtf8().constData()));
    }
    if (!info) {
        // Fallback: match by display name among MIME handlers.
        const QString mime = mimeForLocalPath(localPath);
        GList *list = g_app_info_get_all_for_type(mime.toUtf8().constData());
        for (GList *l = list; l != nullptr; l = l->next) {
            GAppInfo *cand = static_cast<GAppInfo *>(l->data);
            const char *name = g_app_info_get_display_name(cand);
            if (name && app.name == QString::fromUtf8(name)) {
                info = G_APP_INFO(g_object_ref(cand));
                break;
            }
        }
        g_list_free_full(list, g_object_unref);
    }
    if (!info) {
        return launchDesktopId(app.id, localPath);
    }
    GFile *file = g_file_new_for_path(localPath.toUtf8().constData());
    GList *files = g_list_append(nullptr, file);
    GError *err = nullptr;
    const gboolean ok = g_app_info_launch(info, files, nullptr, &err);
    if (err) {
        g_error_free(err);
    }
    g_list_free_full(files, g_object_unref);
    g_object_unref(info);
    return ok == TRUE;
}
#endif

} // namespace

QString openableLocalPath(const QString &sessionPath)
{
    if (sessionPath.isEmpty()) {
        return {};
    }
    if (ArchivePath::isArchiveRef(sessionPath)) {
        const QString arch = ArchivePath::archiveFilePath(sessionPath);
        return stripFileUrl(arch);
    }
    // Page refs, //text, //pdfimage, //epub layout-only, file:// …
    const QString doc = PagePath::documentFilePath(sessionPath);
    if (!doc.isEmpty()) {
        return stripFileUrl(doc);
    }
    return stripFileUrl(sessionPath);
}

QString mimeForLocalPath(const QString &localPath)
{
    if (localPath.isEmpty()) {
        return QStringLiteral("application/octet-stream");
    }
#ifdef BILTOO_HAVE_GIO
    GFile *file = g_file_new_for_path(localPath.toUtf8().constData());
    GFileInfo *info =
        g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                          G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    QString mime;
    if (info) {
        const char *ct = g_file_info_get_content_type(info);
        if (ct) {
            mime = QString::fromUtf8(ct);
        }
        g_object_unref(info);
    }
    g_object_unref(file);
    if (!mime.isEmpty()) {
        return mime;
    }
#endif
    QMimeDatabase db;
    return db.mimeTypeForFile(localPath, QMimeDatabase::MatchExtension).name();
}

QVector<App> appsForMime(const QString &mimeType)
{
    if (mimeType.isEmpty()) {
        return {};
    }
#ifdef BILTOO_HAVE_GIO
    return appsForMimeGio(mimeType);
#else
    return appsForMimeFallback(mimeType);
#endif
}

QVector<App> appsForLocalPath(const QString &localPath)
{
    return appsForMime(mimeForLocalPath(localPath));
}

bool launch(const App &app, const QString &localPath)
{
    if (localPath.isEmpty() || app.name.isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(localPath)) {
        return false;
    }
#ifdef BILTOO_HAVE_GIO
    return launchGio(app, localPath);
#else
    return launchDesktopId(app.id, localPath);
#endif
}

bool openDefault(const QString &localPath)
{
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        return false;
    }
#ifdef BILTOO_HAVE_GIO
    const QString mime = mimeForLocalPath(localPath);
    GAppInfo *info =
        g_app_info_get_default_for_type(mime.toUtf8().constData(), FALSE);
    if (info) {
        GFile *file = g_file_new_for_path(localPath.toUtf8().constData());
        GList *files = g_list_append(nullptr, file);
        GError *err = nullptr;
        const gboolean ok = g_app_info_launch(info, files, nullptr, &err);
        if (err) {
            g_error_free(err);
        }
        g_list_free_full(files, g_object_unref);
        g_object_unref(info);
        if (ok) {
            return true;
        }
    }
#endif
    return QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
}

bool openWithCommandDialog(QWidget *parent, const QString &localPath)
{
    if (localPath.isEmpty()) {
        return false;
    }
    bool ok = false;
    const QString cmd = QInputDialog::getText(
        parent, QObject::tr("Open With"),
        QObject::tr("Command (file path will be appended if not present):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || cmd.trimmed().isEmpty()) {
        return false;
    }
    // Shell-style: allow "gimp %f" or "gimp" — expand %f/%u if present.
    QStringList tokens;
    if (cmd.contains(QLatin1Char('%'))) {
        tokens = expandExec(cmd.trimmed(), localPath);
    } else {
        // Split on spaces (simple; quoted paths in command are rare for this UI).
        tokens = QProcess::splitCommand(cmd.trimmed());
        tokens.append(localPath);
    }
    if (tokens.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(tokens.first(), tokens.mid(1));
}

void populateMenu(QMenu *menu, const QString &localPath, QWidget *parent)
{
    if (menu == nullptr) {
        return;
    }
    menu->setProperty("biltoo_open_with_path", localPath);
    // Invalidate any previously built app list when the path changes.
    menu->setProperty("biltoo_open_with_filled", false);
    menu->clear();

    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        auto *none = menu->addAction(QObject::tr("No file to open"));
        none->setEnabled(false);
        return;
    }

    auto *placeholder = menu->addAction(QObject::tr("…"));
    placeholder->setEnabled(false);

    // Install the deferred builder once per QMenu instance.
    if (!menu->property("biltoo_open_with_hooked").toBool()) {
        menu->setProperty("biltoo_open_with_hooked", true);
        QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, parent] {
            if (menu->property("biltoo_open_with_filled").toBool()) {
                return;
            }
            menu->setProperty("biltoo_open_with_filled", true);
            menu->clear();

            const QString path = menu->property("biltoo_open_with_path").toString();
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                auto *none = menu->addAction(QObject::tr("No file to open"));
                none->setEnabled(false);
                return;
            }

            const QVector<App> apps = appsForLocalPath(path);
            if (apps.isEmpty()) {
                auto *none = menu->addAction(QObject::tr("No applications available"));
                none->setEnabled(false);
            } else {
                for (const App &app : apps) {
                    QAction *act = menu->addAction(app.name);
                    if (!app.icon.isEmpty()) {
                        act->setIcon(QIcon::fromTheme(app.icon));
                    }
                    const App captured = app;
                    QObject::connect(act, &QAction::triggered, menu, [captured, path] {
                        launch(captured, path);
                    });
                }
                menu->addSeparator();
            }

            QAction *other = menu->addAction(QObject::tr("Other Application…"));
            QObject::connect(other, &QAction::triggered, menu, [parent, path] {
                openWithCommandDialog(parent, path);
            });
        });
    }
}

bool openContainingFolder(const QString &localPath)
{
    if (localPath.isEmpty()) {
        return false;
    }
    const QFileInfo fi(localPath);
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (dir.isEmpty() || !QFileInfo::exists(dir)) {
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace OpenWith
