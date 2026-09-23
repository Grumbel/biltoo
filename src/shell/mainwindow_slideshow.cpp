// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "slideshowclocks.h"
#include "viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "ttfp_trace.h"
#include "projectfile.h"
#include "archivepath.h"
#include "pagepath.h"
#include "epublayoutdialog.h"
#include "workspacebackgrounddialog.h"
#include "imageitem.h"
#include "imagecache.h"
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

// Slideshow interval, start/stop, and related chrome (split from mainwindow_session).

void MainWindow::clampSlideshowTransitionToInterval()
{
    if (!m_imageView) {
        return;
    }
    const int cap = m_slideshowIntervalMs;
    const int tr = m_imageView->hostSlideshow().settings().transitionDuration();
    if (cap >= 0 && tr > cap) {
        m_imageView->hostSlideshow().setSlideshowTransitionDurationMs(cap);
    }
}

void MainWindow::rearmSlideshowAfterIntervalChange(int oldInterval)
{
    if (!isSlideshowSession()) {
        return;
    }
    // Preserve normalized cycle progress under the new interval (running or
    // paused). Do not tear down phase buffers, atlases, or Ken Burns — speed
    // changes must be invisible except for timing.
    if (m_slideshowClockRunning && oldInterval != m_slideshowIntervalMs) {
        const int oldI = oldInterval > 0 ? oldInterval : 1;
        const int newI = m_slideshowIntervalMs > 0 ? m_slideshowIntervalMs : 1;
        remapSlideshowPhase(oldI, newI);
    }
    // Keep pending transition indices; only clear the cycle stamp so the next
    // tick can re-bind fade math under the new interval without a cut.
    m_slideshowTransitionCycle = -1;
    if (m_imageView) {
        // Interval-only when already active (no progress-clock restart).
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        // Retarget motion duration; keep dwell atlas and phase images.
        m_imageView->hostSlideshow().reapplySlideshowFraming();
    }
    if (!m_slideshowPaused) {
        updateSlideshowFromClock();
    }
}

void MainWindow::setSlideshowIntervalMs(int ms)
{
    // 0 ms = as fast as the event loop allows; upper bound keeps UI usable.
    const int oldInterval = m_slideshowIntervalMs;
    m_slideshowIntervalMs = SlideshowClocks::clampStoredIntervalMs(ms); // 0…3600s
    // Transition duration is the full effect (out + in); may use the whole interval.
    clampSlideshowTransitionToInterval();
    rearmSlideshowAfterIntervalChange(oldInterval);
}



namespace {

/** Human-readable slideshow dwell for HUD / status line. */
QString formatSlideshowInterval(int ms)
{
    const auto L = SlideshowClocks::intervalLabelParts(ms);
    if (ms <= 0) {
        return QCoreApplication::translate("MainWindow", "0 ms (max speed)");
    }
    if (L.useMs) {
        return QCoreApplication::translate("MainWindow", "%1 ms").arg(L.wholeMs);
    }
    if (L.exactSec) {
        return QCoreApplication::translate("MainWindow", "%1 s").arg(L.wholeSec);
    }
    return QCoreApplication::translate("MainWindow", "%1 s").arg(L.sec, 0, 'f', 1);
}

} // namespace

void MainWindow::slideshowFaster()
{
    // mpv ]: higher playback speed → shorter dwell per slide
    const int next = SlideshowClocks::intervalFaster(m_slideshowIntervalMs);
    if (next == m_slideshowIntervalMs) {
        const QString msg = tr("Slideshow already at maximum speed (0 ms)");
        if (m_imageView) {
            m_imageView->flashHud(tr("Slideshow interval"), msg);
        }
        if (statusBar()) {
            statusBar()->showMessage(msg, 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    const QString detail = formatSlideshowInterval(m_slideshowIntervalMs);
    if (m_imageView) {
        m_imageView->flashHud(tr("Slideshow interval"), detail);
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Slideshow interval: %1").arg(detail), 2000);
    }
}

void MainWindow::slideshowSlower()
{
    // mpv [: lower playback speed → longer dwell per slide
    const int next = SlideshowClocks::intervalSlower(m_slideshowIntervalMs);
    if (next == m_slideshowIntervalMs) {
        const QString msg = tr("Slideshow already at maximum interval (60 s)");
        if (m_imageView) {
            m_imageView->flashHud(tr("Slideshow interval"), msg);
        }
        if (statusBar()) {
            statusBar()->showMessage(msg, 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    const QString detail = formatSlideshowInterval(m_slideshowIntervalMs);
    if (m_imageView) {
        m_imageView->flashHud(tr("Slideshow interval"), detail);
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Slideshow interval: %1").arg(detail), 2000);
    }
}

void MainWindow::showSlideshowCursor()
{
    if (m_slideshowCursorHidden) {
        QApplication::restoreOverrideCursor();
        m_slideshowCursorHidden = false;
    }
}

void MainWindow::hideSlideshowCursor()
{
    if (!isSlideshowSession()) {
        return;
    }
    if (!m_slideshowCursorHidden) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_slideshowCursorHidden = true;
    }
}

void MainWindow::armSlideshowCursorHide()
{
    if (!m_cursorHideTimer || !isSlideshowSession()) {
        return;
    }
    // Show on activity, then hide after 1 s of inactivity (timer interval).
    showSlideshowCursor();
    m_cursorHideTimer->start();
}

bool MainWindow::isSlideshowSession() const
{
    return m_slideshowPaused || m_slideshowClockRunning;
}

void MainWindow::updateSlideshowActionUi()
{
    if (!m_slideshowAct) {
        return;
    }
    if (m_slideshowPaused) {
        m_slideshowAct->setChecked(true);
        m_slideshowAct->setText(tr("Resume &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"),
                                           QStyle::SP_MediaPlay));
        m_slideshowAct->setStatusTip(
            tr("Space: resume · Esc: leave slideshow and fullscreen"));
    } else if (m_slideshowClockRunning) {
        m_slideshowAct->setChecked(true);
        m_slideshowAct->setText(tr("Pause &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-pause"),
                                           QStyle::SP_MediaPause));
        m_slideshowAct->setStatusTip(
            tr("Space: pause · Esc: leave slideshow and fullscreen · ←/→ change slide"));
    } else {
        m_slideshowAct->setChecked(false);
        m_slideshowAct->setText(tr("Play &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"),
                                           QStyle::SP_MediaPlay));
        m_slideshowAct->setStatusTip(
            tr("Space: pause/resume · Esc: leave slideshow and fullscreen"));
    }
}

void MainWindow::onSlideshowUserNavigated()
{
    if (!isSlideshowSession() || m_slideshowAdvancing) {
        return;
    }
    if (!m_imageView) {
        return;
    }

    // Debounce neighbour preload — rapid ←/→ used to start a decode every
    // keystroke for next and prev (and each cancelled the previous job).
    const int nPaths = m_session.paths().size();
    if (nPaths > 1 && m_currentIndex >= 0) {
        const int idx = m_currentIndex;
        if (!m_slideshowPreloadTimer) {
            m_slideshowPreloadTimer = new QTimer(this);
            m_slideshowPreloadTimer->setSingleShot(true);
            m_slideshowPreloadTimer->setInterval(200);
            connect(m_slideshowPreloadTimer, &QTimer::timeout, this, [this]() {
                if (!m_imageView || m_session.paths().size() <= 1
                    || m_currentIndex < 0) {
                    return;
                }
                const int n = m_session.paths().size();
                const int i = m_currentIndex;
                m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at((i + 1) % n));
                m_imageView->hostSlideshow().preloadSlideshowImage(
                    m_session.paths().at((i - 1 + n) % n));
            });
        }
        Q_UNUSED(idx);
        m_slideshowPreloadTimer->start();
    }

    m_imageView->hostSlideshow().cancelSlideshowTransition();
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    m_slideshowTransitionCycle = -1;
    m_slideshowBaseIndex = ViewTransform::clampIndex(m_currentIndex, m_session.paths().size());
    m_slideshowPausedAccumMs = 0;

    // Always publish HUD position for this index. While playing, arm()/tick
    // also write the timeline; while paused the tick does not run, so without
    // this the clock stayed frozen on ←/→.
    const int n = m_session.paths().size();
    const int intervalMs = ViewTransform::atLeast1(m_slideshowIntervalMs);
    if (n > 0) {
        const qint64 totalMs = qint64(n) * qint64(intervalMs);
        const qint64 at = qint64(m_slideshowBaseIndex) * qint64(intervalMs);
        m_imageView->hostSlideshow().setSlideshowTimeline(at, totalMs);
    }

    if (!m_slideshowPaused) {
        armSlideshowAdvanceTimer();
        // Force pure phase to the navigated path so a mid-transition fade
        // cannot leave the previous slide on screen for a tick under ←/→.
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(m_currentIndex),
                                           QString(), -1.0);
        }
    } else {
        // Clock is frozen while paused, so updateSlideshowFromClock will not
        // push a new pure phase. Drive the composite to the navigated slide
        // or the screen stays on the previous m_slideshow.phase().fromImage until unpause.
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(m_currentIndex),
                                           QString(), -1.0);
        }
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
        m_imageView->hostSlideshow().setSlideshowPausedHud(true);
    }
    updateSlideshowActionUi();
}

void MainWindow::startSlideshow()
{
    if (m_session.paths().isEmpty() || isWorkspaceMode()) {
        m_slideshowPaused = false;
        updateSlideshowActionUi();
        return;
    }
    // Shared preview cache: every session path gets a ≥512 frame scheduled now.
    ImageCache::warm(m_session.paths(), ImageCache::kPreviewEdge);
    if (m_thumbnailBar) {
        m_thumbnailBar->setVisibleLoadsSuspended(true);
    }
    // Gallery: open the focused session image in Image mode, then advance.
    if (isGalleryMode()) {
        const SessionImageId sid = currentSessionId();
        if (sid != kInvalidSessionImageId) {
            openSessionImageInImageMode(sid);
        } else {
            QString path;
            if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
                path = m_session.paths().at(m_currentIndex);
            } else if (!m_session.paths().isEmpty()) {
                path = m_session.paths().first();
            }
            if (path.isEmpty()) {
                m_slideshowPaused = false;
                updateSlideshowActionUi();
                return;
            }
            showPathInImageMode(path);
        }
    }
    m_slideshowOwnsFullscreen = false;
    if (m_slideshowFullscreen && !isFullScreen()) {
        showFullScreen();
        m_slideshowOwnsFullscreen = true;
    }
    m_slideshowPaused = false;
    // Mark the session active *before* chrome policy. updateScrollBarPolicyForMode
    // keys off isSlideshowSession(); calling it before the clock runs left Gallery
    // AsNeeded/AlwaysOn bars visible for the whole show (Gallery → Space).
    m_slideshowClockRunning = true;
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
    qApp->installEventFilter(this);
    // Ensure free mouse moves reach the app filter during the show.
    setMouseTracking(true);
    if (m_imageView) {
        m_imageView->setMouseTracking(true);
        if (m_imageView->viewport()) {
            m_imageView->viewport()->setMouseTracking(true);
        }
    }
    armSlideshowCursorHide();
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        // Arm pure phase *before* framing/motion so the first paint is oriented
        // ContentXform sample — not an unoriented dwell underlay stand-in.
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(
                m_session.paths().at(m_currentIndex), QString(), -1.0);
        }
        // Frame + start dwell motion AFTER phase arm so prepareSlideshowMotionDwell
        // can reuse the oriented phase buffer.
        m_imageView->hostSlideshow().reapplySlideshowFraming();
        if (m_session.paths().size() > 1) {
            int n = (m_currentIndex + 1) % m_session.paths().size();
            if (n < 0) {
                n = 0;
            }
            m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at(n));
        }
        m_imageView->flashHud(tr("▶  Slideshow"),
                              formatSlideshowInterval(m_slideshowIntervalMs));
    }
    // Clock last — may immediately start a transition when pureMs==0.
    armSlideshowAdvanceTimer();
    // Re-assert after arm (and after any motion freeze) so Gallery→show never
    // leaves AsNeeded bars from the pre-session policy snapshot.
    updateScrollBarPolicyForMode();
}

void MainWindow::seekSlideshowFraction(qreal fraction)
{
    if (!isSlideshowSession() || m_session.paths().isEmpty()) {
        return;
    }
    fraction = ViewTransform::clamp01(fraction);
    const int n = m_session.paths().size();
    int intervalMs = m_slideshowIntervalMs;
    if (intervalMs <= 0) {
        intervalMs = 1;
    }
    // Unitless seek: position covers the whole session loop [0, n).
    m_slideshowBaseIndex = 0;
    m_slideshowPosition = fraction * qreal(n);
    m_slideshowPausedAccumMs = 0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (!m_slideshowPaused) {
        m_slideshowClock.start();
    }
    const int idx = int(qFloor(m_slideshowPosition)) % n;
    const qreal phaseT = m_slideshowPosition - qFloor(m_slideshowPosition);
    const qint64 totalMs = qint64(n) * qint64(intervalMs);
    const qint64 elapsedMs = SlideshowClocks::timelineElapsedMs(
        m_slideshowPosition, intervalMs, totalMs);
    if (m_imageView) {
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().setSlideshowTimeline(elapsedMs, totalMs);
        m_imageView->hostSlideshow().setSlideshowCycleProgress(phaseT);
    }
    if (idx != m_currentIndex && !m_slideshowAdvancing) {
        m_slideshowAdvancing = true;
        setCurrentIndex(idx);
        m_slideshowAdvancing = false;
    }
    if (!m_slideshowPaused) {
        updateSlideshowFromClock();
    } else if (m_imageView && idx >= 0 && idx < n) {
        m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(idx), QString(), -1.0);
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
    }
}

void MainWindow::pauseSlideshow()
{
    if (!m_slideshowClockRunning || m_slideshowPaused) {
        return;
    }
    // Fold last wall segment into unitless position, then freeze.
    if (m_slideshowClock.isValid()) {
        int intervalMs = m_slideshowIntervalMs > 0 ? m_slideshowIntervalMs : 1;
        const qint64 wallDelta = m_slideshowClock.elapsed();
        if (wallDelta > 0) {
            m_slideshowPosition += qreal(wallDelta) / qreal(intervalMs);
        }
    }
    m_slideshowPausedAccumMs = 0; // position is authoritative
    m_slideshowPaused = true;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
    if (m_imageView) {
        // Drop any in-flight live/snapshot overlay so ←/→ can show the new
        // image immediately (otherwise the hold layer masks LoadReplace).
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
        m_imageView->hostSlideshow().setSlideshowProgressPaused(true);
        m_imageView->hostSlideshow().setSlideshowPausedHud(true);
    }
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
}

void MainWindow::resumeSlideshow()
{
    if (!m_slideshowPaused) {
        return;
    }
    if (m_session.paths().isEmpty() || isWorkspaceMode()) {
        stopSlideshow();
        return;
    }
    m_slideshowPaused = false;
    // Continue the same timeline (do not reset base index / cycle).
    m_slideshowClock.start();
    m_slideshowClockRunning = true;
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().setSlideshowProgressPaused(false);
        m_imageView->flashHud(tr("▶  Slideshow"),
                              formatSlideshowInterval(m_slideshowIntervalMs));
    }
    if (m_slideshowTimer && !m_slideshowTimer->isActive()) {
        m_slideshowTimer->start();
    }
    updateSlideshowFromClock();
    armSlideshowCursorHide();
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
}

void MainWindow::stopSlideshow()
{
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowNavHot(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setVisibleLoadsSuspended(false);
    }
    // Session = playing or paused. Silent no-ops when fully idle.
    const bool wasSession = isSlideshowSession()
        || (m_slideshowAct && m_slideshowAct->isChecked());
    if (!wasSession) {
        return;
    }
    const bool announce = isSlideshowSession();

    m_slideshowPaused = false;
    m_slideshowClockRunning = false;
    m_slideshowPausedAccumMs = 0;
    m_slideshowPosition = 0.0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
    if (m_cursorHideTimer) {
        m_cursorHideTimer->stop();
    }
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().cancelSlideshowMotion();
    }
    showSlideshowCursor();
    qApp->removeEventFilter(this);
    updateSlideshowActionUi();
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowProgress(false, 0);
        // Slideshow advances the session index without loadImage (pure phase owns
        // the viewport). Leaving without a canvas load left Image mode on the
        // pre-show tile. Session flags are already cleared so LoadReplace runs.
        if (announce && isImageMode()
            && m_currentIndex >= 0
            && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostDisplayPipeline().loadImage(m_session.paths().at(m_currentIndex));
            m_imageView->hostSlideshow().restoreImageFramingAfterSlideshow();
            m_imageView->flashHud(tr("■  Slideshow stopped"));
        } else if (announce) {
            m_imageView->hostSlideshow().restoreImageFramingAfterSlideshow();
            m_imageView->flashHud(tr("■  Slideshow stopped"));
        }
    }
    updateScrollBarPolicyForMode();
    // Slideshow is separate from user fullscreen: if we entered fullscreen for
    // this show, leave it with the show. Esc then returns to a normal window.
    if (m_slideshowOwnsFullscreen && isFullScreen()) {
        m_slideshowOwnsFullscreen = false;
        showNormal();
        updateFullscreenUi();
    } else {
        m_slideshowOwnsFullscreen = false;
    }
}

void MainWindow::updateHelpPanelFromWidget(QWidget *widget, const QPoint &localPos)
{
    if (!m_helpPanel || !widget) {
        return;
    }
    QAction *act = nullptr;
    if (auto *tb = qobject_cast<QToolBar *>(widget)) {
        act = tb->actionAt(localPos);
    } else if (auto *menu = qobject_cast<QMenu *>(widget)) {
        act = menu->actionAt(localPos);
    } else if (auto *btn = qobject_cast<QToolButton *>(widget)) {
        act = btn->defaultAction();
        if (!act) {
            // Some toolbar buttons use setDefaultAction; others only QAction via actions().
            const QList<QAction *> acts = btn->actions();
            if (!acts.isEmpty()) {
                act = acts.first();
            }
        }
        // If this is a child of a toolbar, prefer toolbar actionAt in parent coords.
        if (!act) {
            if (auto *parentTb = qobject_cast<QToolBar *>(btn->parentWidget())) {
                act = parentTb->actionAt(btn->mapTo(parentTb, localPos));
            }
        }
    } else if (auto *mb = qobject_cast<QMenuBar *>(widget)) {
        act = mb->actionAt(localPos);
    }
    if (act && !act->isSeparator() && !act->menu()) {
        m_helpPanel->showAction(act);
    } else if (act && act->menu() && !act->isSeparator()) {
        // Top-level menu title: still show a short line if it has statusTip/whatsThis.
        m_helpPanel->showAction(act);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Help panel: pick up disabled toolbar/menu items (QAction::hovered skips them).
    if (m_helpPanel
        && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)) {
        if (auto *w = qobject_cast<QWidget *>(watched)) {
            QPoint pos;
            if (event->type() == QEvent::MouseMove) {
                pos = static_cast<QMouseEvent *>(event)->pos();
            } else {
                pos = static_cast<QHoverEvent *>(event)->position().toPoint();
            }
            if (qobject_cast<QToolBar *>(w) || qobject_cast<QMenu *>(w)
                || qobject_cast<QToolButton *>(w) || qobject_cast<QMenuBar *>(w)) {
                updateHelpPanelFromWidget(w, pos);
            }
        }
    }
    // Mode / filmstrip overviews when the pointer enters the canvas or strip.
    if (m_helpPanel && event->type() == QEvent::HoverEnter) {
        if (watched == m_thumbnailBar || watched == m_thumbnailDock) {
            showFilmstripHelp();
        } else if (watched == m_imageView) {
            showCurrentModeHelp();
        }
    }

    // Escape is also a WindowShortcut (fullscreen / leave Image). QLineEdit does
    // not accept ShortcutOverride for Esc, so the window shortcut wins unless we
    // claim it here first — KeyPress alone never runs.
    auto under = [](QWidget *root, QObject *obj) -> bool {
        if (!root || !obj) {
            return false;
        }
        if (obj == root) {
            return true;
        }
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            return root->isAncestorOf(w);
        }
        return false;
    };
    const bool locWatch = under(m_locationBar, watched) || watched == m_locationEdit;
    const bool searchWatch = under(m_searchBar, watched) || watched == m_searchEdit;
    if (locWatch
        && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            cancelLocationBar();
            return true;
        }
    }
    if (searchWatch
        && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            cancelSearchBar();
            return true;
        }
    }
    if (isSlideshowSession()) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::HoverMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TabletMove:
            armSlideshowCursorHide();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleSlideshow()
{
    if (m_slideshowPaused) {
        resumeSlideshow();
    } else if (m_slideshowClockRunning) {
        pauseSlideshow();
    } else {
        startSlideshow();
    }
}

QString MainWindow::historyEntryLabel(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return tr("(empty)");
    }
    auto containerDir = [](const QString &p) -> QString {
        if (PagePath::isPageRef(p)) {
            const PagePath::Ref r = PagePath::parse(p);
            return r.valid ? QFileInfo(r.pdfPath).absolutePath() : QString();
        }
        if (ArchivePath::isArchiveRef(p)) {
            const ArchivePath::Ref r = ArchivePath::parse(p);
            return r.valid ? QFileInfo(r.archivePath).absolutePath() : QString();
        }
        return QFileInfo(p).absolutePath();
    };
    const QString dir = containerDir(paths.first());
    bool sameDir = !dir.isEmpty();
    for (const QString &p : paths) {
        if (containerDir(p) != dir) {
            sameDir = false;
            break;
        }
    }
    if (paths.size() == 1) {
        return PagePath::displayName(paths.first());
    }
    if (sameDir) {
        // Prefer archive filename when all members share one container.
        if (PagePath::isPageRef(paths.first())) {
            const PagePath::Ref r = PagePath::parse(paths.first());
            if (r.valid) {
                return tr("%1 — %n page(s)", "history entry", paths.size())
                    .arg(QFileInfo(r.pdfPath).fileName());
            }
        }
        if (ArchivePath::isArchiveRef(paths.first())) {
            const ArchivePath::Ref r = ArchivePath::parse(paths.first());
            if (r.valid) {
                return tr("%1 — %n image(s)", "history entry", paths.size())
                    .arg(QFileInfo(r.archivePath).fileName());
            }
        }
        const QString folder = QFileInfo(dir).fileName();
        return tr("%1 — %n image(s)", "history entry", paths.size()).arg(folder);
    }
    return tr("%1 (+%n more)", "history entry", paths.size() - 1)
        .arg(PagePath::displayName(paths.first()));
}


QString MainWindow::historyEntryHelpHtml(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return tr("<p>This history slot is empty.</p>");
    }

    QStringList items;
    items.reserve(paths.size());
    constexpr int kListCap = 40;
    const int shown = qMin(paths.size(), kListCap);
    for (int i = 0; i < shown; ++i) {
        const QString &p = paths.at(i);
        const QString name = PagePath::displayName(p);
        // Prefer a readable name; include the session path when it differs.
        if (name == p || p.endsWith(name)) {
            items.append(QStringLiteral("<li><code>%1</code></li>").arg(p.toHtmlEscaped()));
        } else {
            items.append(QStringLiteral("<li>%1<br/><code>%2</code></li>")
                             .arg(name.toHtmlEscaped(), p.toHtmlEscaped()));
        }
    }

    QString more;
    if (paths.size() > kListCap) {
        more = tr("<p><i>…and %n more path(s) not listed here.</i></p>",
                  nullptr, paths.size() - kListCap);
    }

    return tr(
        "<p>Reopen this <b>Recent Session</b> — the full ordered list of images "
        "from a previous open (not a .biltoo project).</p>"
        "<p><b>%n file(s) / page(s)</b> will replace the current session "
        "(same as Open with those paths).</p>"
        "<ul>%1</ul>%2"
        "<p>Paths keep archive members (<code>//archive:</code>) and document "
        "pages in session form. Choosing an entry does not restore Workspace poses "
        "or project appearance — only the image list.</p>",
        nullptr,
        paths.size())
        .arg(items.join(QString()), more);
}

