// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "slideshow/slideshowclocks.h"
#include "view/viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "host/thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "session/sessionexpand.h"
#include "util/ttfp_trace.h"
#include "session/projectfile.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "shell/epublayoutdialog.h"
#include "workspace/workspacebackgrounddialog.h"
#include "imageitem.h"
#include "display/imagecache.h"
#include <QFileInfo>
#include <QUrl>
#include <QPointer>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QDebug>

#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <functional>

// Path expand progress and background expand (split from mainwindow_session).

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
    return SessionExpand::expandPathList(paths, m_recursive);
}

bool MainWindow::pathsNeedBackgroundExpand(const QStringList &paths) const
{
    // Heuristic only — never QFileInfo::isFile/isDir/exists or
    // ThumtooCache::isAvailable() here. Those hit the disk (or open the
    // thumtoo client) on the GUI thread and can stall for a long time on a
    // spinning-up USB drive *before* any "Indexing…" / "Opening…" status.
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // Already session leaves — expand is pure string work, safe on GUI.
        if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
            || PagePath::isPdfImageRef(path)) {
            continue;
        }
        // Containers, directories (no image suffix), plain images, or unknown:
        // always expand on a worker (suffix checks do not stat).
        return true;
    }
    return false;
}

void MainWindow::setExpandProgressMessage(const QString &message)
{
    if (statusBar()) {
        statusBar()->showMessage(message, 0);
    }
    if (m_imageView) {
        // Centre progress suppresses the empty-session invite during expand.
        m_imageView->setCentreProgress(
            message.isEmpty() ? tr("Working…") : message);
    }
}

void MainWindow::setExpandProgressBusy(bool busy)
{
    if (m_statusProgress) {
        if (busy) {
            m_statusProgress->setRange(0, 0);
            m_statusProgress->show();
        } else {
            m_statusProgress->hide();
            m_statusProgress->setRange(0, 1);
            m_statusProgress->setValue(0);
        }
    }
    if (!busy && m_imageView) {
        // finishApplyExpandedLoad clears busy *after* enterGalleryMode, which may
        // already own the centre HUD for size-resolve — do not wipe that.
        if (!m_imageView->hostGallerySizeResolve().active()) {
            m_imageView->clearCentreProgress();
        }
    }
}

void MainWindow::setExpandProgress(int current, int total, const QString &message)
{
    if (statusBar()) {
        statusBar()->showMessage(message, 0);
    }
    if (m_statusProgress) {
        if (total > 0) {
            m_statusProgress->setRange(0, total);
            m_statusProgress->setValue(ViewTransform::clampedProgress(current, total));
            m_statusProgress->show();
        } else {
            m_statusProgress->setRange(0, 0);
            m_statusProgress->show();
        }
    }
    if (m_imageView) {
        const QString title = message.isEmpty() ? tr("Working…") : message;
        const QString detail = (total > 0)
            ? tr("%1 / %2").arg(current).arg(total)
            : QString();
        // When message already contains N/M, avoid duplicating the detail line.
        if (!detail.isEmpty() && message.contains(QLatin1Char('/'))) {
            m_imageView->setCentreProgress(title);
        } else {
            m_imageView->setCentreProgress(title, detail);
        }
    }
}

void MainWindow::applyExpandedPathsResult(const QStringList &images, bool append, int startAt,
                                          const QStringList &sourcePaths)
{
    if (images.isEmpty()) {
        setExpandProgressBusy(false);
        if (statusBar()) {
            statusBar()->showMessage(
                SessionExpand::emptyResultMessage(sourcePaths, append), 8000);
        }
        return;
    }
    // Expand worker done — drop Opening/Indexing HUD before apply/layout.
    setExpandProgressBusy(false);
    if (statusBar() && statusBar()->currentMessage().startsWith(tr("Opening"))) {
        statusBar()->clearMessage();
    }
    // Do not set "Opening N images…" here — finishApplyExpandedLoad shows it
    // only when durable sizes are still missing (warm index stays silent).
    if (append) {
        applyExpandedAppend(images);
    } else {
        applyExpandedLoad(images, startAt);
    }
}

void MainWindow::expandPathsInBackground(const QStringList &paths, bool append, int startAt)
{
    const quint64 gen = ++m_expandGeneration;
    const bool recursive = m_recursive;
    // Replace loads already clear the filmstrip in loadFiles; append keeps it.
    // Direct callers with append=false still need an immediate empty strip.
    if (!append && m_thumbnailBar && m_thumbnailBar->count() > 0) {
        m_thumbnailBar->setSession(QStringList(), QVector<SessionImageId>());
    }
    setExpandProgressBusy(true);
    // Shown immediately on the GUI thread — no disk I/O before this.
    setExpandProgressMessage(tr("Opening…"));

    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, paths, append, startAt, gen, recursive]() {
        QElapsedTimer reportClock;
        reportClock.start();
        qint64 lastReportMs = -1000;
        // Rate-limit GUI posts (~8 Hz) so listing huge archives does not flood
        // the event queue and freeze the UI that way.
        const SessionExpand::ReportFn report = [guard, gen, &reportClock, &lastReportMs](
                                          const QString &msg, int current, int total) {
            if (!guard) {
                return;
            }
            const qint64 now = reportClock.elapsed();
            if (now - lastReportMs < 120 && current >= 0 && total > 0 && current < total) {
                return;
            }
            lastReportMs = now;
            QMetaObject::invokeMethod(guard.data(), [guard, gen, msg, current, total]() {
                MainWindow *const window = guard.data();
                if (!window || gen != window->m_expandGeneration) {
                    return;
                }
                if (total > 0) {
                    window->setExpandProgress(current, total, msg);
                } else {
                    window->setExpandProgressBusy(true);
                    window->setExpandProgressMessage(msg);
                }
            }, Qt::QueuedConnection);
        };
        const SessionExpand::CancelFn cancel = [guard, gen]() {
            MainWindow *const window = guard.data();
            return !window || gen != window->m_expandGeneration;
        };

        const QStringList images = SessionExpand::expandPathList(paths, recursive, report, cancel);
        if (cancel()) {
            return;
        }

        QMetaObject::invokeMethod(guard.data(), [guard, gen, images, append, startAt, paths]() {
            MainWindow *const window = guard.data();
            if (!window || gen != window->m_expandGeneration) {
                return;
            }
            window->applyExpandedPathsResult(images, append, startAt, paths);
        }, Qt::QueuedConnection);
    });
}



