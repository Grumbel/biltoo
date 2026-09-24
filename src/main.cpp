// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow.h"
#include "view/viewtransform.h"
#include "util/biltoo_logging.h"
#include "shell/metadatapanel.h"
#include "host/imageloader.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "tilelod/tile_lod_registry.hpp"
#include "version.h"
#include "thumtoo/version.hpp"

#include <QApplication>
#include <QThreadPool>
#include <QThread>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QCommandLineOption>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QStatusBar>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QSurfaceFormat>
#include <cstring>
#include <cstdlib>

int main(int argc, char *argv[])
{
    ImageLoader::init(argv[0]);

    // ThumtooCache::init() runs before QCommandLineParser — honour env and a
    // raw argv flag here so debug is on for Client construction.
    {
        bool want = false;
        if (const char *e = std::getenv("THUMTOO_DEBUG");
            e && e[0] && e[0] != '0') {
            want = true;
        }
        if (const char *e = std::getenv("BILTOO_THUMTOO_DEBUG");
            e && e[0] && e[0] != '0') {
            want = true;
        }
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--thumtoo-debug") == 0
                || std::strcmp(argv[i], "--debug") == 0) {
                want = true;
                break;
            }
        }
        if (want) {
            ThumtooCache::enableDebugTracing();
        }
    }

    // Before QApplication: vsync + buffers for QOpenGLWidget viewports.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSwapInterval(1);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    QApplication app(argc, argv);
    // icons.qrc is compiled into static biltoo_lib; without an explicit init the
    // linker may drop the RCC object and :/icons/* is empty at runtime.
    Q_INIT_RESOURCE(icons);
    tilelod::TileLodRegistry::instance().apply_environment_overrides();
    // Leave headroom for the GUI thread under tile/soft background load.
    {
        const int ideal = QThread::idealThreadCount();
        const int cap = ideal <= 2 ? 1 : ideal - 1;
        QThreadPool::globalInstance()->setMaxThreadCount(ViewTransform::atLeast1(cap));
    }
    // After QApplication so thumtoo callbacks can queue onto the GUI thread.
    ThumtooCache::init();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        // Tear down thumtoo first (cancels interest, bounded waitForDone, then
        // drops Client). Clearing the pool queue alone left in-flight
        // set_interest running on a destroyed Client → UAF/segfault on quit
        // when archive/NFS locator held the DB mutex.
        ImageCache::clear();
        ThumtooCache::shutdown();
    });
    QApplication::setApplicationName(QStringLiteral("biltoo"));
    QApplication::setApplicationDisplayName(QStringLiteral("Biltoo"));
    QApplication::setApplicationVersion(QStringLiteral(BILTOO_VERSION_STRING));
    QApplication::setOrganizationName(QStringLiteral("biltoo"));
    QApplication::setOrganizationDomain(QStringLiteral("biltoo.local"));
    QGuiApplication::setDesktopFileName(QStringLiteral("biltoo"));

    // App icon from embedded SVG. QIcon(":.svg") needs the svg iconengines
    // plugin on QT_PLUGIN_PATH; under nix develop the unwrapped binary often
    // lacks that, so rasterize via QSvgRenderer (linked Qt6::Svg).
    // Prefer theme when it actually provides sizes (installed + XDG_DATA_DIRS);
    // otherwise use the qrc (requires Q_INIT_RESOURCE above).
    QIcon appIcon;
    {
        const QIcon theme = QIcon::fromTheme(QStringLiteral("biltoo"));
        if (!theme.isNull() && !theme.availableSizes().isEmpty()) {
            appIcon = theme;
        } else {
            const QString res = QStringLiteral(":/icons/biltoo.svg");
            if (QFile::exists(res)) {
                const QIcon native(res);
                if (!native.isNull() && !native.availableSizes().isEmpty()) {
                    appIcon = native;
                } else {
                    QSvgRenderer renderer(res);
                    if (renderer.isValid()) {
                        for (int s : {16, 32, 48, 64, 128, 256}) {
                            QPixmap pm(s, s);
                            pm.fill(Qt::transparent);
                            QPainter p(&pm);
                            p.setRenderHint(QPainter::Antialiasing, true);
                            renderer.render(&p);
                            p.end();
                            appIcon.addPixmap(pm);
                        }
                    }
                }
            }
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
            "Sort session by name, path, or mtime (date)"),
        QStringLiteral("name|path|mtime"));
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
            "Verbose diagnostics (slideshow traces, libexiv2 metadata warnings; "
            "also enables thumtoo task traces)"));
    parser.addOption(debugOption);

    QCommandLineOption thumtooDebugOption(
        QStringList() << QStringLiteral("thumtoo-debug"),
        QCoreApplication::translate("main",
            "Trace thumtoo ladder/tile work to stderr and "
            "~/.cache/biltoo/thumtoo-debug.log"));
    parser.addOption(thumtooDebugOption);

    // --help-all is already registered by addHelpOption() (Qt shows generic
    // Qt options). We intercept it before process() to print env vars instead.
    // Do not addOption("help-all") again — that warns "option already added".

    // Rich --version: biltoo + optional features + linked thumtoo version.
    {
        const QStringList args = QCoreApplication::arguments();
        if (args.contains(QStringLiteral("--version"))
            || args.contains(QStringLiteral("-v"))) {
            QTextStream out(stdout);
            out << "biltoo " << QApplication::applicationVersion() << '\n';
            auto line = [&](const char *name, bool on) {
                out << "  " << name << ": " << (on ? "enabled" : "missing") << '\n';
            };
            line("libvips", BILTOO_FEATURE_VIPS);
            line("libexiv2", BILTOO_FEATURE_EXIV2);
            line("thumtoo", BILTOO_FEATURE_THUMTOO);
            line("thumtoo archives", BILTOO_FEATURE_ARCHIVE);
            line("libunarr (solid RAR/CBR)", BILTOO_FEATURE_THUMTOO_UNARR);
            line("MuPDF (PDF/EPUB)", BILTOO_FEATURE_THUMTOO_MUPDF);
            line("DjVuLibre", BILTOO_FEATURE_THUMTOO_DJVU);
            line("libcurl (via thumtoo)", BILTOO_FEATURE_THUMTOO_CURL);
            line("GIO", BILTOO_FEATURE_GIO);
            out << "thumtoo " << QString::fromUtf8(thumtoo::version_string().data(),
                                                   int(thumtoo::version_string().size()))
                << '\n';
            out.flush();
            return 0;
        }
        // addHelpOption() already owns --help-all; process() would show Qt's
        // generic help and exit. Intercept first and list biltoo/thumtoo env.
        if (args.contains(QStringLiteral("--help-all"))) {
            QTextStream out(stdout);
            out << parser.helpText() << '\n';
            out << QCoreApplication::translate(
                   "main", "Environment variables (debugging / limits)")
            << '\n'
            << QCoreApplication::translate(
                   "main",
                   "Flag-style: on when non-empty and not 0/f/n. "
                   "Full detail: docs/ENVIRONMENT.md")
            << "\n\n";
            out << "Runtime debug traces\n"
               "  THUMTOO_DEBUG              thumtoo ladder/tile/interest traces\n"
               "                             (stderr + ~/.cache/biltoo/thumtoo-debug.log)\n"
               "  BILTOO_THUMTOO_DEBUG       alias for THUMTOO_DEBUG\n"
               "  BILTOO_LOAD_DEBUG          ImageView load-pipeline timestamps\n"
               "  BILTOO_DEBUG_SLIDESHOW     slideshow transition traces\n"
               "  BILTOO_DEBUG_FILMSTRIP     filmstrip schedule diagnostics\n"
               "  BILTOO_DEBUG_DROP          drag-and-drop / session-append logging\n"
               "  BILTOO_DEBUG_CROP          crop-mode geometry diagnostics\n"
               "  BILTOO_DEBUG_APPEARANCE    appearance / materialize logging\n"
               "  BILTOO_MODE_DEBUG          mode-switch / empty-canvas diagnostics\n"
               "  BILTOO_PERF                paint + decode-window timings\n"
               "  BILTOO_TILE_DEBUG          tile LOD coordinator lines (~500 ms)\n"
               "  BILTOO_TTFP                time-to-first-paint traces\n"
               "  THUMTOO_DEBUG_OVERLAY      watermark decoded samples (soft vs full)\n"
               "  BILTOO_DEBUG_OVERLAY       alias for THUMTOO_DEBUG_OVERLAY\n"
               "  BILTOO_GUI_BUDGET_LOG      log GUI_BUDGET exceeds (off by default)\n"
               "  BILTOO_GUI_BUDGET_STRICT   abort when GUI_BUDGET is exceeded\n"
               "\n"
               "Thumtoo cache policy (mostly ignored on modern thumtoo)\n"
               "  THUMTOO_SOFT_LEVELS        ignored for Client writes (>=265)\n"
               "  THUMTOO_TILES_ONLY         ignored for Client writes (>=265)\n"
               "  THUMTOO_STORE_ONLY         ignored (>=262; always Store-only)\n"
               "\n"
               "Concurrency / limits\n"
               "  BILTOO_THUMTOO_PIXEL_JOBS     concurrent PreferCache/pixel jobs\n"
               "                               (default 4, range 1-32)\n"
               "  BILTOO_FILMSTRIP_THUMB_LOADS concurrent filmstrip thumb jobs\n"
               "                               (default 6, range 1-64)\n"
               "  BILTOO_TILE_RAM_MIB          TileLodRegistry RAM budget MiB\n"
               "                               (default 384)\n"
               "  BILTOO_TILE_MAX_IDLE         max zero-ref path entries retained\n"
               "                               (default 64)\n"
               "\n"
               "Paths\n"
               "  XDG_CACHE_HOME             durable cache + thumtoo-debug.log base\n"
               "  XDG_DATA_HOME              thumtoo user overlays (user.sqlite)\n"
               "  XDG_STATE_HOME             optional state base\n"
               "  HOME                       fallback when XDG_* unset\n"
               "\n"
               "CLI aliases: --debug, --thumtoo-debug\n";
            out.flush();
            return 0;
        }
    }

    parser.process(app);

    const bool debug = parser.isSet(debugOption);
    // Quiet by default; --debug enables biltoo.slideshow qCDebug + Exiv2 warnings.
    configureBiltooDebugLogging(debug);
    configureMetadataLibraryLogging(debug);

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
        } else if (sort == QLatin1String("path")) {
            window.setSortMode(MainWindow::SortMode::Path);
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

    const int rc = app.exec();
    // ~QCoreApplication waits forever on QThreadPool::waitForDone(). If a
    // thumtoo set_interest is still blocked on NFS/archive DB I/O after
    // ThumtooCache::shutdown()'s bounded wait, abandon clean destructors.
    if (QThreadPool::globalInstance()->activeThreadCount() > 0) {
        qWarning("biltoo: pool still active after quit; forcing process exit");
        std::_Exit(rc);
    }
    return rc;
}
