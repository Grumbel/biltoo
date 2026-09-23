// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "slideshow/slideshowclocks.h"
#include "viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "session/sessionexpand.h"
#include "ttfp_trace.h"
#include "projectfile.h"
#include "archivepath.h"
#include "pagepath.h"
#include "epublayoutdialog.h"
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

// Navigation actions, location bar, EPUB/PDF open helpers (split from mainwindow_session).

namespace {

QString imageFileDialogFilter()
{
    QStringList imagePatterns;
    for (const QString &suffix : ImageLoader::imageSuffixes()) {
        imagePatterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    QStringList archivePatterns;
    for (const QString &suffix : ArchivePath::archiveSuffixes()) {
        // Compound suffixes like tar.gz → *.tar.gz
        archivePatterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    const QString images = imagePatterns.join(QLatin1Char(' '));
    const QString archives = archivePatterns.join(QLatin1Char(' '));
    // First filter is the dialog default — include archives/PDFs/EPUBs so containers are visible.
    return QObject::tr(
               "Images, archives, PDF, EPUB and DjVu (%1 %2 *.pdf *.epub *.djvu *.djv);;"
               "Images only (%1);;Archives only (%2);;PDF documents (*.pdf);;"
               "EPUB books (*.epub);;DjVu documents (*.djvu *.djv);;All Files (*)")
        .arg(images, archives);
}

} // namespace

void MainWindow::updateNavPrevNextSlideshowActions(bool hasFiles, bool hasMany)
{
    // Prev/Next are Image mode only. Slideshow may start from Gallery (enters
    // Image mode on start); still unavailable in Workspace.
    const bool imageNav = hasMany && m_imageView && m_imageView->isImageMode();
    // Single-image sessions may still run a (short) slideshow dwell / motion.
    const bool canSlideshow = hasFiles && m_imageView && !m_imageView->isWorkspaceMode();
    m_previousAct->setEnabled(imageNav);
    m_nextAct->setEnabled(imageNav);
    const QString imageNavReason = tr("Available in Image mode when the session has more than one image.");
    if (m_previousAct) {
        m_previousAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_nextAct) {
        m_nextAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_epubLayoutAct) {
        bool epub = false;
        if (hasFiles) {
            const int i = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                              ? m_currentIndex
                              : 0;
            const QString pth = m_session.paths().at(i);
            if (PagePath::isPageRef(pth)) {
                const PagePath::Ref r = PagePath::parse(pth);
                epub = r.valid && r.isEpub();
            } else {
                epub = PagePath::isEpubFile(pth) || PagePath::isEpubLayoutOnly(pth);
            }
        }
        m_epubLayoutAct->setEnabled(epub);
        m_epubLayoutAct->setProperty(
            "biltooDisabledHelp",
            tr("EPUB Layout is available when the current item is an EPUB book or page."));
    }
    if (m_pdfEmbeddedImagesAct) {
        bool pdf = false;
        if (hasFiles && ThumtooCache::isAvailable()) {
            const int i = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                              ? m_currentIndex
                              : 0;
            const QString pth = m_session.paths().at(i);
            if (PagePath::isPageRef(pth)) {
                const PagePath::Ref r = PagePath::parse(pth);
                pdf = r.valid && !r.isEpub() && PagePath::isPdfFile(r.pdfPath);
            } else if (PagePath::isPdfImageRef(pth) || PagePath::isPdfImagesCollection(pth)) {
                pdf = true;
            } else {
                pdf = PagePath::isPdfFile(pth);
            }
        }
        m_pdfEmbeddedImagesAct->setEnabled(pdf);
        m_pdfEmbeddedImagesAct->setProperty(
            "biltooDisabledHelp",
            tr("Available when the current item is a PDF (page or file) and thumtoo is present."));
    }
    if (m_firstAct) {
        m_firstAct->setEnabled(imageNav);
        m_firstAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_lastAct) {
        m_lastAct->setEnabled(imageNav);
        m_lastAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    m_slideshowAct->setEnabled(canSlideshow);
    if (m_slideshowAct) {
        if (canSlideshow) {
            m_slideshowAct->setStatusTip(tr("Space: pause/resume · Esc: leave slideshow and fullscreen"));
            m_slideshowAct->setProperty("biltooDisabledHelp", QString());
        } else if (m_imageView && m_imageView->isWorkspaceMode()) {
            const QString r = tr("Slideshow is not available in Workspace mode.");
            m_slideshowAct->setStatusTip(r);
            m_slideshowAct->setProperty("biltooDisabledHelp", r);
        } else {
            const QString r = tr("Open at least one image to use the slideshow.");
            m_slideshowAct->setStatusTip(r);
            m_slideshowAct->setProperty("biltooDisabledHelp", r);
        }
    }
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(imageNav);
    }
    // Stop only when a running slideshow becomes invalid (empty session or
    // Workspace). Idle stopSlideshow is a no-op for the timer, but still
    // emitted statusChanged via restoreImageFramingAfterSlideshow → re-entered
    // here forever (stack overflow / SEGV in QToolButton). Single-image
    // sessions may keep a short dwell/motion loop.
    // Do not use transient "no canvas items" mid LoadReplace as a stop signal.
    if (isSlideshowSession()
        && (m_session.paths().isEmpty()
            || (m_imageView && m_imageView->isWorkspaceMode()))) {
        stopSlideshow();
    }
}

void MainWindow::updateNavTransformCropActions(bool canTransform)
{
    // Rotate/flip: Image (current), Workspace (selection), Gallery (selection)
    const QString transformReason = tr(
        "Rotate and flip need a current Image, or a selection in Gallery or Workspace.");
    for (QAction *act : {m_rotateLeftAct, m_rotateRightAct, m_flipHAct, m_flipVAct}) {
        if (act) {
            act->setEnabled(canTransform);
            act->setProperty("biltooDisabledHelp", transformReason);
        }
    }
    if (m_resetContentAppearanceAct) {
        m_resetContentAppearanceAct->setEnabled(
            canTransform && m_imageView && m_imageView->targetHasContentAppearance());
    }
    // Crop: Image mode, or exactly one Gallery/Workspace selection.
    if (m_cropAct) {
        const bool canCrop = m_imageView && m_imageView->hasSingleCropTarget();
        m_cropAct->setEnabled(canCrop);
        m_cropAct->setProperty(
            "biltooDisabledHelp",
            tr("Crop needs Image mode, or exactly one selected tile in Gallery or Workspace."));
        if (!canCrop && m_imageView && m_imageView->hostCrop().active()) {
            m_imageView->hostCrop().cancelCrop();
        }
    }
    // Placement resets are Workspace-only (content transforms use Image menu).
    const bool canResetPlacement = canTransform && m_imageView
        && m_imageView->isWorkspaceMode();
    for (QAction *act : {m_resetScaleAct, m_resetRotationAct, m_resetShearAct}) {
        if (act) {
            act->setEnabled(canResetPlacement);
        }
    }
}

void MainWindow::updateNavZoomAndSelectionActions(bool hasFiles, bool hasItem)
{
    // Zoom: Image / Workspace with content; Image with files loading also OK
    const bool canZoom = hasItem
                         || (m_imageView && m_imageView->isImageMode() && hasFiles);
    const QString zoomReason = tr(
        "Zoom commands need content on the canvas (Image or Workspace), or an open session in Image mode.");
    for (QAction *act : {m_zoomInAct, m_zoomOutAct, m_zoom1to1Act, m_zoomFitAct, m_zoomFillAct,
                         m_zoomRegionAct}) {
        if (act) {
            act->setEnabled(canZoom);
            act->setProperty("biltooDisabledHelp", zoomReason);
        }
    }

    if (m_selectAllAct) {
        m_selectAllAct->setEnabled(hasFiles);
    }
    if (m_hideThumbLabelsAct) {
        m_hideThumbLabelsAct->setEnabled(m_thumbnailBar != nullptr);
    }
    if (m_toggleHudAct) {
        m_toggleHudAct->setEnabled(true);
    }
    if (m_openSelectionNewWindowAct) {
        const bool canOpenSel = !pathsFromUiSelection().isEmpty();
        m_openSelectionNewWindowAct->setEnabled(canOpenSel);
    }
}

void MainWindow::updateNavigationActions()
{
    const bool hasFiles = !m_session.paths().isEmpty();
    const bool hasMany = m_session.paths().size() > 1;
    const bool hasItem = m_imageView && m_imageView->itemCount() > 0;
    const bool canTransform = m_imageView && m_imageView->hasTransformTargets();

    updateNavPrevNextSlideshowActions(hasFiles, hasMany);
    updateNavTransformCropActions(canTransform);
    updateNavZoomAndSelectionActions(hasFiles, hasItem);
}



void MainWindow::onThumbnailActivated(int index)
{
    // Double-click / Enter on the filmstrip: open Image mode on that index
    // (from Gallery or Workspace). Not used for plain Image-mode single-click.
    if (index < 0 || index >= m_session.size()) {
        return;
    }
    openSessionIndexInImageMode(index);
    onSlideshowUserNavigated();
}

void MainWindow::onThumbnailNavigated(int index)
{
    // Image-mode filmstrip single-click: switch the displayed session image.
    if (index < 0 || index >= m_session.size()) {
        return;
    }
    if (!isImageMode()) {
        // Gallery/Workspace navigation is multi-select only; open via activate.
        return;
    }
    setCurrentIndex(index);
    onSlideshowUserNavigated();
}

void MainWindow::syncLocationBarText()
{
    if (!m_locationEdit) {
        return;
    }
    QString text;
    if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        text = m_session.paths().at(m_currentIndex);
    } else if (!m_session.paths().isEmpty()) {
        text = m_session.paths().first();
    }
    // Avoid fighting the user while they are typing.
    if (m_locationEdit->hasFocus()) {
        return;
    }
    m_locationEdit->setText(text);
}

void MainWindow::setLocationBarPinned(bool pinned)
{
    m_locationBarPinned = pinned;
    if (m_showLocationBarAct && m_showLocationBarAct->isChecked() != pinned) {
        QSignalBlocker block(m_showLocationBarAct);
        m_showLocationBarAct->setChecked(pinned);
    }
    if (!m_locationBar) {
        return;
    }
    if (pinned) {
        syncLocationBarText();
        m_locationBar->setVisible(true);
    } else {
        // Explicit unpin via the menu: always hide and release focus.
        // (Previously we kept the bar if the line edit had focus, which made
        // the toggle appear broken and left Escape with nothing to cancel.)
        if (m_locationEdit) {
            m_locationEdit->clearFocus();
        }
        m_locationBar->setVisible(false);
        if (m_imageView) {
            m_imageView->setFocus(Qt::OtherFocusReason);
        }
    }
}

void MainWindow::openLocation()
{
    if (!m_locationBar || !m_locationEdit) {
        return;
    }
    syncLocationBarText();
    // If empty (no session), still show so the user can type a path.
    if (m_locationEdit->text().isEmpty()
        && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        m_locationEdit->setText(m_session.paths().at(m_currentIndex));
    } else if (m_locationEdit->text().isEmpty() && !m_session.paths().isEmpty()) {
        m_locationEdit->setText(m_session.paths().first());
    }
    m_locationBar->setVisible(true);
    m_locationEdit->setFocus(Qt::ShortcutFocusReason);
    m_locationEdit->selectAll();
}

void MainWindow::cancelLocationBar()
{
    if (!m_locationBar || !m_locationEdit) {
        return;
    }
    m_locationEdit->clearFocus();
    if (!m_locationBarPinned) {
        m_locationBar->setVisible(false);
    } else {
        syncLocationBarText();
    }
    if (m_imageView) {
        m_imageView->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::commitLocationBar()
{
    if (!m_locationEdit) {
        return;
    }
    const QString trimmed = m_locationEdit->text().trimmed();
    if (trimmed.isEmpty()) {
        cancelLocationBar();
        return;
    }
    // Accept file:// URLs and plain paths; page/archive refs pass through.
    QString path = trimmed;
    if (path.startsWith(QLatin1String("file:"))) {
        const QUrl url(path);
        if (url.isLocalFile()) {
            const int pipe = path.indexOf(QLatin1String("//"), 7);
            const QString local = url.toLocalFile();
            if (pipe > 0) {
                path = local + path.mid(pipe);
            } else {
                path = local;
            }
        }
    }

    // If the user strips //page:N (or //epub:…//page:N) back to the bare
    // document path, open the full expanded session — not a single page.
    // Prefer starting at the page they were on when it belongs to the same file.
    int startAt = 0;
    if (!PagePath::isPageRef(path)) {
        const QFileInfo info(path);
        const QString abs = info.exists() ? info.absoluteFilePath() : path;
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            const PagePath::Ref cur = PagePath::parse(m_session.paths().at(m_currentIndex));
            if (cur.valid) {
                const QFileInfo curInfo(cur.pdfPath);
                const QString curAbs =
                    curInfo.exists() ? curInfo.absoluteFilePath() : cur.pdfPath;
                if (curAbs == abs || cur.pdfPath == path) {
                    startAt = cur.page - 1; // 0-based index into expanded pages
                }
            }
        }
    }

    loadFiles(QStringList{path}, startAt);
    if (!m_locationBarPinned && m_locationBar) {
        m_locationEdit->clearFocus();
        m_locationBar->setVisible(false);
    }
    if (m_imageView) {
        m_imageView->setFocus(Qt::OtherFocusReason);
    }
}


bool MainWindow::resolveEpubLayoutTarget(QString *epubFile, QString *layoutParams,
                                         int *keepPage) const
{
    if (m_session.paths().isEmpty()) {
        return false;
    }
    const int idx = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                        ? m_currentIndex
                        : 0;
    const QString curPath = m_session.paths().at(idx);

    if (PagePath::isPageRef(curPath)) {
        const PagePath::Ref ref = PagePath::parse(curPath);
        if (!ref.valid || !ref.isEpub()) {
            return false;
        }
        *epubFile = ref.pdfPath;
        *layoutParams = ref.epubLayoutParams;
        *keepPage = ref.page;
        return true;
    }
    if (PagePath::isEpubLayoutOnly(curPath)) {
        *epubFile = PagePath::documentFilePath(curPath);
        *layoutParams = PagePath::epubLayoutParamsOf(curPath);
        *keepPage = 1;
        return true;
    }
    if (PagePath::isEpubFile(curPath)) {
        *epubFile = curPath;
        layoutParams->clear();
        *keepPage = 1;
        return true;
    }
    return false;
}

void MainWindow::rewriteEpubSessionPaths(const QString &epubFile, const QString &newParams,
                                         int keepPage)
{
    QStringList paths = m_session.paths();
    const int idx = (m_currentIndex >= 0 && m_currentIndex < paths.size())
                        ? m_currentIndex
                        : 0;
    int newIndex = idx;
    const QFileInfo epubInfo(epubFile);
    const QString epubAbs =
        epubInfo.exists() ? epubInfo.absoluteFilePath() : epubFile;

    for (int i = 0; i < paths.size(); ++i) {
        if (!PagePath::isPageRef(paths.at(i))) {
            continue;
        }
        const PagePath::Ref r = PagePath::parse(paths.at(i));
        if (!r.valid || !r.isEpub()) {
            continue;
        }
        const QFileInfo fi(r.pdfPath);
        const QString abs = fi.exists() ? fi.absoluteFilePath() : r.pdfPath;
        if (abs != epubAbs && r.pdfPath != epubFile) {
            continue;
        }
        paths[i] = PagePath::makeEpubRef(r.pdfPath, r.page, newParams);
        if (r.page == keepPage) {
            newIndex = i;
        }
    }

    const int startAt = (newIndex >= 0 && newIndex < paths.size()) ? newIndex : 0;
    // Full reload so thumtoo picks up new layout URIs (reflow + cache keys).
    loadFiles(paths, startAt);
}

void MainWindow::showEpubLayoutDialog()
{
    QString epubFile;
    QString layoutParams;
    int keepPage = 1;
    if (!resolveEpubLayoutTarget(&epubFile, &layoutParams, &keepPage)) {
        if (statusBar()) {
            statusBar()->showMessage(tr("EPUB Layout applies to EPUB pages only."), 4000);
        }
        return;
    }

    EpubLayoutDialog dlg(this);
    dlg.setLayoutParams(layoutParams);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString newParams = dlg.layoutParams();
    if (newParams == layoutParams) {
        return;
    }

    // Rewrite every session page that belongs to this EPUB.
    rewriteEpubSessionPaths(epubFile, newParams, keepPage);
    if (statusBar()) {
        statusBar()->showMessage(tr("EPUB layout applied."), 3000);
    }
}

void MainWindow::openPdfAsEmbeddedImages()
{
    if (!ThumtooCache::isAvailable()) {
        if (statusBar()) {
            statusBar()->showMessage(
                tr("PDF embedded images require thumtoo."), 4000);
        }
        return;
    }
    if (m_session.paths().isEmpty()) {
        return;
    }
    const int idx = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                        ? m_currentIndex
                        : 0;
    const QString cur = m_session.paths().at(idx);
    QString doc;
    if (PagePath::isPageRef(cur)) {
        const PagePath::Ref r = PagePath::parse(cur);
        if (!r.valid || r.isEpub()) {
            if (statusBar()) {
                statusBar()->showMessage(tr("PDF Embedded Images applies to PDFs only."), 4000);
            }
            return;
        }
        doc = r.pdfPath;
    } else if (PagePath::isPdfImageRef(cur) || PagePath::isPdfImagesCollection(cur)) {
        doc = PagePath::documentFilePath(cur);
    } else if (PagePath::isPdfFile(cur)) {
        doc = cur;
    } else {
        if (statusBar()) {
            statusBar()->showMessage(tr("PDF Embedded Images applies to PDFs only."), 4000);
        }
        return;
    }
    if (doc.isEmpty()) {
        return;
    }
    const QString collection = PagePath::makePdfImagesCollection(doc);
    if (collection.isEmpty()) {
        return;
    }
    // Already a pure //pdfimages session for this file — re-expand to refresh.
    loadFiles(QStringList{collection});
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Opening embedded images from “%1”…").arg(QFileInfo(doc).fileName()),
            3000);
    }
}

void MainWindow::openFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Open Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        loadFiles(files);
    }
}

void MainWindow::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Add Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        appendFiles(files);
    }
}

void MainWindow::openDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"));
    if (!dir.isEmpty()) {
        loadFiles({dir});
    }
}


void MainWindow::reloadFromDisk()
{
    if (!m_imageView) {
        return;
    }
    // Gallery F5: re-decode + explicit pack (same as pressing a layout action).
    // Image mode: only the focused file. Workspace: tiles in place, no pack.
    const bool relayout = m_imageView->isGalleryMode();
    m_imageView->reloadFromDisk(relayout);
    updateStatus();
}

void MainWindow::hardReloadFromDisk()
{
    if (!m_imageView) {
        return;
    }
    const bool relayout = m_imageView->isGalleryMode();
    m_imageView->hardReloadFromDisk(relayout);
    updateStatus();
}

void MainWindow::goPrevious()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    int idx = m_currentIndex - 1;
    if (idx < 0) {
        idx = m_session.paths().size() - 1;
    }
    setCurrentIndex(idx);
    onSlideshowUserNavigated();
}

void MainWindow::goNext()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    int idx = m_currentIndex + 1;
    if (idx >= m_session.paths().size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
    onSlideshowUserNavigated();
}

void MainWindow::goFirst()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    setCurrentIndex(0);
    onSlideshowUserNavigated();
}

void MainWindow::goLast()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    setCurrentIndex(m_session.paths().size() - 1);
    onSlideshowUserNavigated();
}

