// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageloader.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    ImageLoader::init(argv[0]);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qimgview"));
    QApplication::setApplicationDisplayName(QStringLiteral("QImgView"));
#ifndef QIMGVIEW_VERSION
#  define QIMGVIEW_VERSION "0.0.0"
#endif
    QApplication::setApplicationVersion(QStringLiteral(QIMGVIEW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("QImgView"));
    QApplication::setOrganizationDomain(QStringLiteral("qimgview.local"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qimgview"));

    // Prefer the installed theme icon; fall back to the embedded SVG.
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("qimgview"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/icons/qimgview.svg"));
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
        QStringLiteral("%1/qimgview/translations")
            .arg(QLibraryInfo::path(QLibraryInfo::PrefixPath) + QStringLiteral("/share")),
    };
    for (const QString &dir : appTrPaths) {
        if (appTranslator.load(QLocale(), QStringLiteral("qimgview"), QStringLiteral("_"), dir)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main",
            "Classic Qt image viewer with workspace semantics"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QCoreApplication::translate("main", "Image files or directories to open"),
        QStringLiteral("[file|dir...]"));

    QCommandLineOption fullscreenOption(
        QStringList() << QStringLiteral("f") << QStringLiteral("fullscreen"),
        QCoreApplication::translate("main", "Start in fullscreen mode"));
    parser.addOption(fullscreenOption);

    // Documented flag; fit-on-load is already the default Image-mode behaviour.
    QCommandLineOption fitOption(
        QStringList() << QStringLiteral("fit"),
        QCoreApplication::translate("main",
            "Fit image to window on load (default behaviour)"));
    parser.addOption(fitOption);

    QCommandLineOption startAtOption(
        QStringList() << QStringLiteral("start-at"),
        QCoreApplication::translate("main", "Start at the N-th image (1-based)"),
        QStringLiteral("N"));
    parser.addOption(startAtOption);

    QCommandLineOption recursiveOption(
        QStringList() << QStringLiteral("r") << QStringLiteral("recursive"),
        QCoreApplication::translate("main",
            "Recurse into subdirectories when a directory is given"));
    parser.addOption(recursiveOption);

    QCommandLineOption sortOption(
        QStringList() << QStringLiteral("sort"),
        QCoreApplication::translate("main",
            "Sort images by name or mtime (default: name)"),
        QStringLiteral("name|mtime"));
    parser.addOption(sortOption);

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

    QCommandLineOption thumbnailsOption(
        QStringList() << QStringLiteral("thumbnails"),
        QCoreApplication::translate("main", "Force show the thumbnail bar"));
    parser.addOption(thumbnailsOption);

    QCommandLineOption noThumbnailsOption(
        QStringList() << QStringLiteral("no-thumbnails"),
        QCoreApplication::translate("main", "Force hide the thumbnail bar"));
    parser.addOption(noThumbnailsOption);

    parser.process(app);

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
        if (ok && ms > 0) {
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

    if (!files.isEmpty()) {
        window.loadFiles(files, startAt);
        if (parser.isSet(slideshowOption)) {
            window.startSlideshow();
        }
    }

    return app.exec();
}
