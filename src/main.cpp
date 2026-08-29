// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("qimgview");
    QApplication::setApplicationDisplayName("QImgView");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("QImgView");

    QCommandLineParser parser;
    parser.setApplicationDescription("Classic Qt image viewer with workspace semantics");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "Image files to open", "[file...]");

    QCommandLineOption fullscreenOption(QStringList() << "f" << "fullscreen",
                                        "Start in fullscreen mode");
    parser.addOption(fullscreenOption);

    QCommandLineOption fitOption(QStringList() << "fit",
                                 "Fit image to window on load");
    parser.addOption(fitOption);

    parser.process(app);

    const QStringList files = parser.positionalArguments();

    MainWindow window;
    if (parser.isSet(fullscreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    if (!files.isEmpty()) {
        window.loadFiles(files);
    }

    return app.exec();
}
