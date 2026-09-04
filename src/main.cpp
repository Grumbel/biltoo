// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "metadatapanel.h"
#include "imageloader.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QFileInfo>
#include <QIcon>
#include <QStatusBar>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    ImageLoader::init(argv[0]);

    // Must be set before QApplication: vsync + depth/stencil for QOpenGLWidget
    // viewports. Swap interval 1 = block on display refresh (true vsync).
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSwapInterval(1);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("biltoo"));
    QApplication::setApplicationDisplayName(QStringLiteral("Biltoo"));
#ifndef BILTOO_VERSION
#  define BILTOO_VERSION "0.0.0"
#endif
    QApplication::setApplicationVersion(QStringLiteral(BILTOO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("biltoo"));
    QApplication::setOrganizationDomain(QStringLiteral("biltoo.local"));
    QGuiApplication::setDesktopFileName(QStringLiteral("biltoo"));

    // Embedded resource first (works from ./build/biltoo without install).
    // Theme icon only if the theme actually provides sizes (fromTheme can
    // return a non-null empty icon when the name is missing from the theme).
    QIcon appIcon(QStringLiteral(":/icons/biltoo.svg"));
    {
        const QIcon theme = QIcon::fromTheme(QStringLiteral("biltoo"));
        if (!theme.isNull() && !theme.availableSizes().isEmpty()) {
            appIcon = theme;
        }
    }
    QApplication::setWindowIcon(appIcon);

    // AUDIT M25: install UI + Qt base translators when .qm files are present.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }
    QTranslator appTranslator;
    const QStringList appTrPaths = {
        QStringLiteral(":/i18n"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
        QStringLiteral("%1/biltoo/translations")
            .arg(QLibraryInfo::path(QLibraryInfo::PrefixPath) + QStringLiteral("/share")),
    };
    for (const QString &dir : appTrPaths) {
        if (appTranslator.load(QLocale(), QStringLiteral("biltoo"), QStringLiteral("_"), dir)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main",
            "Biltoo — Image, Gallery, and Workspace image viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QCoreApplication::translate("main",
            "Image files, directories, or a .biltoo project to open"),
        QStringLiteral("[file|dir|project...]"));

    // --- Input / session ---
    QCommandLineOption recursiveOption(
        QStringList() << QStringLiteral("r") << QStringLiteral("recursive"),
        QCoreApplication::translate("main",
            "Recurse into subdirectories when a directory is given"));
    parser.addOption(recursiveOption);

    QCommandLineOption startAtOption(
        QStringList() << QStringLiteral("start-at"),
        QCoreApplication::translate("main", "Start at the N-th image (1-based)"),
        QStringLiteral("N"));
    parser.addOption(startAtOption);

    QCommandLineOption sortOption(
        QStringList() << QStringLiteral("sort"),
        QCoreApplication::translate("main",
            "Sort session by file name or modification time"),
        QStringLiteral("name|mtime"));
    parser.addOption(sortOption);

    QCommandLineOption modeOption(
        QStringList() << QStringLiteral("mode"),
        QCoreApplication::translate("main",
            "Start in Image, Gallery (masonry), or Workspace mode"),
        QStringLiteral("image|gallery|workspace"));
    parser.addOption(modeOption);

    // --- Playback ---
    QCommandLineOption slideshowOption(
        QStringList() << QStringLiteral("slideshow"),
        QCoreApplication::translate("main", "Start a slideshow after loading images"));
    parser.addOption(slideshowOption);

    QCommandLineOption intervalOption(
        QStringList() << QStringLiteral("interval"),
        QCoreApplication::translate("main",
            "Slideshow interval in milliseconds (default: 3000)"),
        QStringLiteral("ms"));
    parser.addOption(intervalOption);

    // --- Window / chrome ---
    QCommandLineOption fullscreenOption(
        QStringList() << QStringLiteral("f") << QStringLiteral("fullscreen"),
        QCoreApplication::translate("main", "Start in fullscreen mode"));
    parser.addOption(fullscreenOption);

    QCommandLineOption thumbnailsOption(
        QStringList() << QStringLiteral("thumbnails"),
        QCoreApplication::translate("main", "Force show the thumbnail bar"));
    parser.addOption(thumbnailsOption);

    QCommandLineOption noThumbnailsOption(
        QStringList() << QStringLiteral("no-thumbnails"),
        QCoreApplication::translate("main", "Force hide the thumbnail bar"));
    parser.addOption(noThumbnailsOption);

    QCommandLineOption debugOption(
        QStringList() << QStringLiteral("debug"),
        QCoreApplication::translate("main",
            "Verbose diagnostics (e.g. libexiv2 metadata warnings with file path)"));
    parser.addOption(debugOption);

    parser.process(app);

    // Quiet Exiv2 IFD noise by default; --debug shows warnings with file path.
    configureMetadataLibraryLogging(parser.isSet(debugOption));

    const QStringList files = parser.positionalArguments();

    int startAt = 0;
    if (parser.isSet(startAtOption)) {
        bool ok = false;
        const int n = parser.value(startAtOption).toInt(&ok);
        if (ok && n >= 1) {
            startAt = n - 1;
        }
    }

    MainWindow window;
    window.setRecursive(parser.isSet(recursiveOption));

    if (parser.isSet(sortOption)) {
        const QString sort = parser.value(sortOption).toLower();
        if (sort == QLatin1String("mtime") || sort == QLatin1String("date")
            || sort == QLatin1String("time")) {
            window.setSortMode(MainWindow::SortMode::MTime);
        } else {
            window.setSortMode(MainWindow::SortMode::Name);
        }
    }

    if (parser.isSet(intervalOption)) {
        bool ok = false;
        const int ms = parser.value(intervalOption).toInt(&ok);
        if (ok && ms >= 0) {
            window.setSlideshowIntervalMs(ms);
        }
    }

    if (parser.isSet(noThumbnailsOption)) {
        window.setNoThumbnailsForced(true);
    } else if (parser.isSet(thumbnailsOption)) {
        window.setThumbnailsForced(true);
    }

    if (parser.isSet(fullscreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    const QString cliMode = parser.isSet(modeOption)
        ? parser.value(modeOption)
        : QString();

    // Workspace before load so loadFiles can place the full session on the canvas
    // (same path as Preferences "Start in workspace mode").
    if (cliMode.compare(QLatin1String("workspace"), Qt::CaseInsensitive) == 0
        || cliMode.compare(QLatin1String("work"), Qt::CaseInsensitive) == 0) {
        window.applyCliViewMode(QStringLiteral("workspace"));
    } else if (cliMode.compare(QLatin1String("image"), Qt::CaseInsensitive) == 0
               || cliMode.compare(QLatin1String("classic"), Qt::CaseInsensitive) == 0) {
        // Override Preferences start-in-workspace for this launch.
        window.applyCliViewMode(QStringLiteral("image"));
    }

    if (!files.isEmpty()) {
        // .biltoo projects are not images — open via project load, not expandPaths.
        QStringList projectArgs;
        QStringList otherArgs;
        for (const QString &arg : files) {
            const QFileInfo fi(arg);
            if (fi.suffix().compare(QLatin1String("biltoo"), Qt::CaseInsensitive) == 0) {
                projectArgs.append(arg);
            } else {
                otherArgs.append(arg);
            }
        }

        if (!projectArgs.isEmpty()) {
            if (projectArgs.size() > 1) {
                qWarning().noquote()
                    << QCoreApplication::translate(
                           "main",
                           "Multiple project files given; only the first will be opened: %1")
                           .arg(projectArgs.first());
            }
            if (!otherArgs.isEmpty()) {
                qWarning().noquote()
                    << QCoreApplication::translate(
                           "main",
                           "Ignoring non-project arguments when opening a project.");
            }
            QString err;
            if (!window.openProjectFile(projectArgs.first(), &err)) {
                qWarning().noquote() << err;
                if (window.statusBar()) {
                    window.statusBar()->showMessage(err, 10000);
                }
            }
            // Project file already restores its stored mode; CLI --mode still applies after.
            if (!cliMode.isEmpty()) {
                window.applyCliViewMode(cliMode);
            }
        } else {
            window.loadFiles(files, startAt);
            if (cliMode.compare(QLatin1String("gallery"), Qt::CaseInsensitive) == 0) {
                window.applyCliViewMode(QStringLiteral("gallery"));
            } else if (cliMode.compare(QLatin1String("workspace"), Qt::CaseInsensitive) == 0
                       || cliMode.compare(QLatin1String("work"), Qt::CaseInsensitive) == 0) {
                // Ensure full session on canvas if load raced preference/mode.
                window.applyCliViewMode(QStringLiteral("workspace"));
            }
            if (parser.isSet(slideshowOption)) {
                window.startSlideshow();
            }
        }
    } else if (!cliMode.isEmpty()) {
        window.applyCliViewMode(cliMode);
    }

    return app.exec();
}
