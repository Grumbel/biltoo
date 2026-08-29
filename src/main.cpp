// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qimgview"));
    QApplication::setApplicationDisplayName(QStringLiteral("QImgView"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("QImgView"));
    QApplication::setOrganizationDomain(QStringLiteral("qimgview.local"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qimgview"));

    // Prefer the installed theme icon; fall back to the embedded SVG.
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("qimgview"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/icons/qimgview.svg"));
    }
    QApplication::setWindowIcon(appIcon);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Classic Qt image viewer with workspace semantics"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("Image files or directories to open"),
        QStringLiteral("[file|dir...]"));

    QCommandLineOption fullscreenOption(
        QStringList() << QStringLiteral("f") << QStringLiteral("fullscreen"),
        QStringLiteral("Start in fullscreen mode"));
    parser.addOption(fullscreenOption);

    QCommandLineOption fitOption(
        QStringList() << QStringLiteral("fit"),
        QStringLiteral("Fit image to window on load (default behaviour)"));
    parser.addOption(fitOption);

    QCommandLineOption startAtOption(
        QStringList() << QStringLiteral("start-at"),
        QStringLiteral("Start at the N-th image (1-based)"),
        QStringLiteral("N"));
    parser.addOption(startAtOption);

    QCommandLineOption recursiveOption(
        QStringList() << QStringLiteral("r") << QStringLiteral("recursive"),
        QStringLiteral("Recurse into subdirectories when a directory is given"));
    parser.addOption(recursiveOption);

    QCommandLineOption sortOption(
        QStringList() << QStringLiteral("sort"),
        QStringLiteral("Sort images by name or mtime (default: name)"),
        QStringLiteral("name|mtime"));
    parser.addOption(sortOption);

    QCommandLineOption slideshowOption(
        QStringList() << QStringLiteral("slideshow"),
        QStringLiteral("Start a slideshow after loading images"));
    parser.addOption(slideshowOption);

    QCommandLineOption intervalOption(
        QStringList() << QStringLiteral("interval"),
        QStringLiteral("Slideshow interval in milliseconds (default: 3000)"),
        QStringLiteral("ms"));
    parser.addOption(intervalOption);

    QCommandLineOption thumbnailsOption(
        QStringList() << QStringLiteral("thumbnails"),
        QStringLiteral("Force show the thumbnail bar"));
    parser.addOption(thumbnailsOption);

    QCommandLineOption noThumbnailsOption(
        QStringList() << QStringLiteral("no-thumbnails"),
        QStringLiteral("Force hide the thumbnail bar"));
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
