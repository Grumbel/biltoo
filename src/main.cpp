// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qimgview"));
    QApplication::setApplicationDisplayName(QStringLiteral("QImgView"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("QImgView"));
    QApplication::setOrganizationDomain(QStringLiteral("qimgview.local"));

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

    parser.process(app);

    const QStringList files = parser.positionalArguments();

    int startAt = 0;
    if (parser.isSet(startAtOption)) {
        bool ok = false;
        const int n = parser.value(startAtOption).toInt(&ok);
        if (ok && n >= 1) {
            startAt = n - 1; // convert to 0-based
        }
    }

    MainWindow window;
    if (parser.isSet(fullscreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    if (!files.isEmpty()) {
        window.loadFiles(files, startAt);
    }

    return app.exec();
}
