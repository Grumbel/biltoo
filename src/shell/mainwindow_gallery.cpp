// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "imageitem.h"
#include "util/biltoo_logging.h"

void MainWindow::setLayoutFreeForm()
{
    // Free-form is Workspace mode, not a Gallery layout.
    m_workspaceModeAct->setChecked(true);
    toggleWorkspaceMode();
}

void MainWindow::populateGalleryCanvas()
{
    // DOMAIN: Gallery shows the full session list (m_session.paths()), not the
    // Workspace membership. Caller must already be in Gallery mode.
    if (!m_imageView || m_session.paths().isEmpty()) {
        return;
    }
    // Full session pack. Cancel any stale size-resolve gate left from a prior
    // Gallery visit so setWorkspacePaths cannot hide every tile under defer
    // and leave an empty canvas after Workspace.
    m_imageView->hostGallerySizeResolve().cancel();
    m_imageView->hostGalleryDecodeBook().setDeferPopulate(false);
    m_imageView->hostWorkspace().setPaths(m_session.paths(), m_session.ids());
    // Workspace→Gallery (and any cold enter) must never leave a blank canvas.
    // setWorkspacePaths may re-arm size-resolve + defer and return without
    // creating tiles. While the size gate is active, leave defer alone so
    // gate-complete still runs a full ensure (clearing defer here left a
    // single progressive cell until manual relayout).
    if (!m_imageView->isGalleryMode()) {
        return;
    }
    bool needPlaceholders = false;
    if (m_imageView->hostGallerySizeResolve().active()) {
        // Sized prefix only — ordered ensure stops at the first unresolved path.
        needPlaceholders = m_imageView->itemCount() == 0;
        if (needPlaceholders) {
            m_imageView->hostGallery().ensurePlaceholders();
        }
    } else {
        m_imageView->hostGalleryDecodeBook().setDeferPopulate(false);
        needPlaceholders = m_imageView->itemCount() == 0;
        if (!needPlaceholders) {
            needPlaceholders = true;
            for (ImageItem *item : m_imageView->liveItems()) {
                if (item && item->isVisible()) {
                    needPlaceholders = false;
                    break;
                }
            }
        }
        if (needPlaceholders) {
            m_imageView->hostGallery().ensurePlaceholders();
        }
    }
    biltooModeDbg("populateGallery live=%d visibleNeed=%d session=%d defer=%d sizeRes=%d",
                  m_imageView->itemCount(),
                  needPlaceholders ? 1 : 0,
                  static_cast<int>(m_session.paths().size()),
                  m_imageView->hostGalleryDecodeBook().isDeferPopulate() ? 1 : 0,
                  m_imageView->hostGallerySizeResolve().active() ? 1 : 0);
}


void MainWindow::enterGalleryMode(LayoutMode layout)
{
    // AUDIT M7a: slideshow ticks only make sense in Image mode.
    stopSlideshow();
    // DOMAIN: Mode := Gallery; pack all session paths with layout.
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    if (m_thumbnailBar) {
        // Same multi-select as Workspace: Ctrl/Shift on filmstrip + canvas.
        m_thumbnailBar->setMultiSelectEnabled(true);
        m_thumbnailBar->selectNoneThumbs();
    }
    m_galleryReturnLayout = layout;
    // setWorkspacePaths refuses Image mode. Enter Gallery first so the session
    // paths are actually scheduled; otherwise the first open after startup only
    // shows the single Image-mode item until Gallery is chosen again.
    //
    // Warm Image→Gallery: stash non-empty ⇒ enter() restores the same cells.
    // Skip populateGalleryCanvas (same as returnToGallery) so layout menu /
    // goToGalleryCurrentLayout cannot re-walk the full session after restash.
    // Gallery→Gallery layout switch and Workspace→Gallery still populate.
    // If session grew/shrank while in Image (append drop, etc.), live count
    // will not match — fall through to populate.
    const bool warmRestash = m_imageView->isImageMode()
        && !m_imageView->hostGallery().stashedItems().isEmpty();
    m_imageView->enterGallery(layout);
    // Apply mode policy before populate. Packing itself temporarily forces
    // AlwaysOn while measuring avail (see ImageView::applyLayout) so AsNeeded
    // does not shrink the viewport mid-pack.
    updateScrollBarPolicyForMode();
    const bool membershipOk = warmRestash
        && m_imageView->itemCount() == m_session.size();
    if (membershipOk) {
        m_imageView->hostGalleryDecodeBook().setDeferPopulate(false);
        m_imageView->hostDisplayPipeline().tickPrimaryTileLod(16);
    } else {
        populateGalleryCanvas();
    }

    syncGalleryLayoutUi(layout);
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::goToGalleryCurrentLayout()
{
    enterGalleryMode(m_galleryReturnLayout);
}

void MainWindow::syncGalleryLayoutUi(LayoutMode layout)
{
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct, m_layoutGridAct,
                         m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct,
                         m_layoutFlowAct, m_layoutFlowFillAct, m_layoutFacingAct}) {
        if (act) {
            act->setChecked(false);
        }
    }
    QAction *check = nullptr;
    switch (layout) {
    case LayoutMode::SideBySide:
        check = m_layoutSideBySideAct;
        break;
    case LayoutMode::Vertical:
        check = m_layoutVerticalAct;
        break;
    case LayoutMode::Grid:
        check = m_layoutGridAct;
        break;
    case LayoutMode::GridCrop:
        check = m_layoutGridCropAct;
        break;
    case LayoutMode::Masonry:
        check = m_layoutMasonryAct;
        break;
    case LayoutMode::MasonryRows:
        check = m_layoutMasonryRowsAct;
        break;
    case LayoutMode::MasonryFill:
        check = m_layoutMasonryFillAct;
        break;
    case LayoutMode::MasonryRowsFill:
        check = m_layoutMasonryRowsFillAct;
        break;
    case LayoutMode::Flow:
        check = m_layoutFlowAct;
        break;
    case LayoutMode::FlowFill:
        check = m_layoutFlowFillAct;
        break;
    case LayoutMode::Facing:
        check = m_layoutFacingAct;
        break;
    default:
        break;
    }
    if (check) {
        check->setChecked(true);
    }

    if (!m_galleryLayoutToolbarAct) {
        return;
    }
    // Combo face: current layout icon; primary click is still "Go to Gallery".
    if (check && !check->icon().isNull()) {
        m_galleryLayoutToolbarAct->setIcon(check->icon());
    } else {
        m_galleryLayoutToolbarAct->setIcon(resourceIcon(QStringLiteral("gallery-masonry")));
    }
    QString layoutName = check ? check->text() : tr("Masonry");
    layoutName.remove(QLatin1Char('&'));
    m_galleryLayoutToolbarAct->setToolTip(
        tr("Go to Gallery (%1) — arrow chooses layout").arg(layoutName));
    m_galleryLayoutToolbarAct->setStatusTip(
        tr("Enter Gallery with %1 layout (menu arrow picks another)").arg(layoutName));
}

void MainWindow::setLayoutSideBySide()
{
    enterGalleryMode(LayoutMode::SideBySide);
}

void MainWindow::setLayoutVertical()
{
    enterGalleryMode(LayoutMode::Vertical);
}

void MainWindow::setLayoutGrid()
{
    enterGalleryMode(LayoutMode::Grid);
}

void MainWindow::setLayoutGridCrop()
{
    enterGalleryMode(LayoutMode::GridCrop);
}

void MainWindow::setLayoutMasonry()
{
    enterGalleryMode(LayoutMode::Masonry);
}

void MainWindow::setLayoutMasonryRows()
{
    enterGalleryMode(LayoutMode::MasonryRows);
}

void MainWindow::setLayoutMasonryFill()
{
    enterGalleryMode(LayoutMode::MasonryFill);
}

void MainWindow::setLayoutMasonryRowsFill()
{
    enterGalleryMode(LayoutMode::MasonryRowsFill);
}

void MainWindow::setLayoutFlow()
{
    enterGalleryMode(LayoutMode::Flow);
}

void MainWindow::setLayoutFlowFill()
{
    enterGalleryMode(LayoutMode::FlowFill);
}

void MainWindow::setLayoutFacing()
{
    enterGalleryMode(LayoutMode::Facing);
}

void MainWindow::openSessionIndexInImageMode(int sessionIndex)
{
    if (sessionIndex < 0 || sessionIndex >= m_session.size()) {
        return;
    }
    const QString path = m_session.paths().at(sessionIndex);
    const SessionImageId sid = sessionIdAt(sessionIndex);

    // Remember where Image was opened from so Up can restore that mode.
    if (m_imageView && m_imageView->isGalleryMode()) {
        m_galleryReturnLayout = m_imageView->hostLayout().currentMode();
        m_galleryReturnActive = true;
        m_workspaceReturnActive = false;
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        m_workspaceReturnActive = true;
        m_galleryReturnActive = false;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    // Pin the target *before* leaveForImageMode. ImageController::enter loads
    // classicPath; without this, a stale session-cursor path races the real
    // target and double-click on a Workspace tile can open the wrong image.
    //
    // Also pin m_currentIndex *before* leave: Image::enter emits statusChanged →
    // updateStatus → setCurrentSessionId(currentSessionId()) which reads
    // sessionIdAt(m_currentIndex). If m_currentIndex still points at the previous
    // row, that reverts the pinned sid and Image install materializes the wrong
    // contentBake (log: path=002.jpg id=1 turns from id=1).
    m_currentIndex = sessionIndex;
    if (m_imageView) {
        m_imageView->hostImage().setClassicPath(path);
        m_imageView->setCurrentSessionId(sid);
        m_imageView->hostSlideshow().setSessionPosition(sessionIndex, m_session.size(), false);
        m_imageView->hostGallery().leaveForImageMode();
        // enter() already loadImage(path); re-affirm after setCurrentIndex chrome
        // so a same-index refresh cannot leave a blank canvas (classicPath kept).
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_thumbnailBar->selectNoneThumbs();
    }
    setCurrentIndex(sessionIndex);
    // Guarantee Image canvas has pixels after mode switch. itemCount()>0 alone
    // is not enough: a blank placeholder with classicPath already set skipped
    // a second load and left Workspace→Image empty when soft delivery failed.
    if (m_imageView && m_imageView->isImageMode() && !path.isEmpty()) {
        bool needLoad = m_imageView->itemCount() == 0
            || m_imageView->hostImage().classicPath() != path;
        if (!needLoad) {
            const QList<ImageItem *> &live = m_imageView->liveItems();
            ImageItem *primary = live.isEmpty() ? nullptr : live.first();
            if (!primary || !primary->hasDisplayPixels()
                || primary->displayPixelLongEdge() <= 0) {
                needLoad = true;
            }
        }
        if (needLoad) {
            m_imageView->hostDisplayPipeline().loadImage(path);
        }
    }
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::openSessionImageInImageMode(SessionImageId sessionId)
{
    const int idx = indexOfSessionId(sessionId);
    if (idx < 0) {
        return;
    }
    openSessionIndexInImageMode(idx);
}

void MainWindow::showPathInImageMode(const QString &path)
{
    // Path fallback for unbound tiles. Prefer a live selected/sole item so
    // duplicate paths open the correct SessionImageId (not paths().indexOf).
    if (path.isEmpty() || m_session.isEmpty()) {
        return;
    }
    if (m_imageView) {
        if (ImageItem *pref = m_imageView->findPreferredItemForPath(path)) {
            if (pref->sessionId() != kInvalidSessionImageId) {
                openSessionImageInImageMode(pref->sessionId());
                return;
            }
            {
                const int listIdx = m_imageView->sessionListIndex(pref);
                if (listIdx >= 0) {
                    openSessionIndexInImageMode(listIdx);
                    return;
                }
            }
        }
    }
    // No live preferred tile: bound session row, else pure path index.
    const int idx = indexOfPathPreferId(path);
    if (idx < 0) {
        return;
    }
    openSessionIndexInImageMode(idx);
}

void MainWindow::openGalleryItemInImageMode(const QString &path)
{
    showPathInImageMode(path);
}

void MainWindow::returnFromImageMode()
{
    if (m_workspaceReturnActive) {
        returnToWorkspace();
        return;
    }
    // Implicit default Gallery when not returning to Workspace (direct open,
    // CLI, History, or Workspace mode toggled off still have a Gallery to go up to).
    returnToGallery();
}

void MainWindow::returnToGallery()
{
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    const QString focusPath = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                  ? m_session.paths().at(m_currentIndex)
                                  : QString();
    const SessionImageId focusId = sessionIdAt(m_currentIndex);
    const LayoutMode layout = m_galleryReturnLayout;

    stopSlideshow();
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        m_thumbnailBar->selectNoneThumbs();
    }

    // Phase 3: restore arm + enter Gallery + apply pending centre in one place.
    // Warm stash restore keeps the same ImageItem* cells (pixels, tile sessions
    // via registry idle). Skipping populateGalleryCanvas avoids setWorkspacePaths
    // re-walking the full session (size gate / membership) after a mode switch.
    bool restoredStash = false;
    if (m_imageView) {
        restoredStash = m_imageView->hostGallery().returnFromImage(
            static_cast<int>(layout), focusPath, focusId);
    }
    // Membership check: session may have grown (drop-append) while in Image
    // without discarding the Gallery stash — then restash is incomplete.
    const bool membershipOk = restoredStash && m_imageView
        && m_imageView->itemCount() == m_session.size();
    if (!membershipOk) {
        populateGalleryCanvas();
    } else {
        m_imageView->hostGalleryDecodeBook().setDeferPopulate(false);
        m_imageView->hostDisplayPipeline().tickPrimaryTileLod(16);
    }
    if (m_imageView) {
        m_imageView->hostGallery().applyPendingRestore();
        QTimer::singleShot(0, this, [this, focusPath, focusId]() {
            if (!m_imageView || !m_imageView->isGalleryMode()) {
                return;
            }
            m_imageView->hostGallery().restoreViewport(focusPath, focusId);
            m_imageView->hostGallery().applyPendingRestore();
        });
    }

    m_galleryReturnLayout = layout;
    syncGalleryLayoutUi(layout);
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::returnToWorkspace()
{
    if (!m_workspaceReturnActive) {
        return;
    }
    m_workspaceReturnActive = false;
    m_galleryReturnActive = false;
    // Phase 3: restore stashed free-form tiles (or durable snapshot) in one place.
    if (m_imageView) {
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
    } else {
        enterWorkspaceMode();
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(true);
    }
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::enterWorkspaceMode()
{
    // DOMAIN: Mode := Workspace; restore free objects if snapshotted.
    if (isWorkspaceMode()) {
        return;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(true);
    }
    // Reuse toggle path so toolbar / selection seeding stays consistent.
    toggleWorkspaceMode();
}

void MainWindow::applyCliViewMode(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m.isEmpty()) {
        return;
    }
    if (m == QLatin1String("image") || m == QLatin1String("classic")) {
        if (isWorkspaceMode()) {
            if (m_workspaceModeAct) {
                m_workspaceModeAct->setChecked(false);
            }
            toggleWorkspaceMode();
        } else if (isGalleryMode()) {
            QString path;
            if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
                path = m_session.paths().at(m_currentIndex);
            } else if (!m_session.paths().isEmpty()) {
                path = m_session.paths().first();
            }
            if (!path.isEmpty()) {
                showPathInImageMode(path);
            } else if (m_imageView) {
                m_imageView->setViewMode(ImageView::ViewMode::Image);
                updateWorkspaceActionVisibility();
            }
        }
        return;
    }
    if (m == QLatin1String("gallery")) {
        enterGalleryMode(initialGalleryLayoutForOpen());
        return;
    }
    if (m == QLatin1String("workspace") || m == QLatin1String("work")) {
        // Empty Workspace by default; user places tiles or opens a project.
        enterWorkspaceMode();
        updateWorkspaceActionVisibility();
        return;
    }
}



void MainWindow::updateMasonryCountControl()
{
    if (!m_imageView || !m_masonryCountAction) {
        return;
    }
    const auto mode = m_imageView->hostLayout().currentMode();
    const bool gallery = m_imageView->isGalleryMode();
    const bool masonryCols = gallery
        && (mode == LayoutMode::Masonry
            || mode == LayoutMode::MasonryFill);
    const bool masonryRows = gallery
        && (mode == LayoutMode::MasonryRows
            || mode == LayoutMode::MasonryRowsFill);
    const bool gridCols = gallery
                          && (mode == LayoutMode::Grid
                              || mode == LayoutMode::GridCrop
                              || mode == LayoutMode::Flow
                              || mode == LayoutMode::FlowFill);
    const bool show = masonryCols || masonryRows || gridCols;
    m_masonryCountAction->setVisible(show);
    if (!show || !m_masonryCountSpin) {
        return;
    }
    if (m_masonryCountLabel) {
        m_masonryCountLabel->setText(masonryRows ? tr("Rows:") : tr("Columns:"));
    }
    const QSignalBlocker blocker(m_masonryCountSpin);
    if (masonryRows) {
        m_masonryCountSpin->setValue(m_imageView->hostLayout().masonryRowsValue());
    } else if (gridCols) {
        // Show effective columns (auto -> computed-looking default of current setting or 0 spin as min 1)
        const int g = m_imageView->hostLayout().gridColumnsValue();
        m_masonryCountSpin->setValue(g > 0 ? g : 3);
    } else {
        m_masonryCountSpin->setValue(m_imageView->hostLayout().masonryColumnsValue());
    }
}

void MainWindow::updateScrollBarPolicyForMode()
{
    if (!m_imageView) {
        return;
    }
    Qt::ScrollBarPolicy h = Qt::ScrollBarAlwaysOff;
    Qt::ScrollBarPolicy v = Qt::ScrollBarAlwaysOff;
    // Slideshow is always chrome-free — never show scrollbars mid-show even if
    // the user preference is AsNeeded.
    if (isSlideshowSession()) {
        h = Qt::ScrollBarAlwaysOff;
        v = Qt::ScrollBarAlwaysOff;
    } else if (m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked()) {
        // Gallery packs reserve bar space inside applyLayout when needed.
        h = Qt::ScrollBarAsNeeded;
        v = Qt::ScrollBarAsNeeded;
    }
    if (m_imageView->horizontalScrollBarPolicy() != h) {
        m_imageView->setHorizontalScrollBarPolicy(h);
    }
    if (m_imageView->verticalScrollBarPolicy() != v) {
        m_imageView->setVerticalScrollBarPolicy(v);
    }
}

void MainWindow::updateThumbnailBarForMode()
{
    if (!m_thumbnailBar) {
        return;
    }
    // Fullscreen chrome is owned by updateFullscreenUi; do not fight it here.
    if (isFullScreen()) {
        return;
    }
    // CLI force flags override per-mode preferences for the strip.
    if (m_forceNoThumbnails) {
        if (m_thumbnailDock) {
            m_thumbnailDock->setVisible(false);
        } else {
            m_thumbnailBar->setVisible(false);
        }
        if (m_toggleThumbnailBarAct) {
            m_toggleThumbnailBarAct->setChecked(false);
        }
        return;
    }
    if (m_forceThumbnails) {
        const bool show = !m_session.paths().isEmpty();
        if (m_thumbnailDock) {
            m_thumbnailDock->setVisible(show);
        } else {
            m_thumbnailBar->setVisible(show);
        }
        if (m_toggleThumbnailBarAct) {
            m_toggleThumbnailBarAct->setChecked(show);
        }
        m_thumbnailBarVisibleBeforeFullscreen = show;
        return;
    }

    bool show = false;
    if (m_imageView && m_imageView->isGalleryMode()) {
        show = m_thumbnailsPreferredGallery && !m_session.paths().isEmpty();
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        // Workspace default on: show strip whenever the session has images.
        show = m_thumbnailsPreferredWorkspace && !m_session.paths().isEmpty();
    } else {
        // Image mode: auto when multi-file session (legacy applyThumbnailVisibility).
        show = m_session.paths().size() > 1;
    }
    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(show);
    } else {
        m_thumbnailBar->setVisible(show);
    }
    if (m_toggleThumbnailBarAct) {
        m_toggleThumbnailBarAct->setChecked(show);
    }
    m_thumbnailBarVisibleBeforeFullscreen = show;
}

void MainWindow::updateLayoutPanelForMode()
{
    if (!m_layoutDock || !m_toggleLayoutPanelAct) {
        return;
    }
    const bool workspace = m_imageView && m_imageView->isWorkspaceMode();
    // Layout panel is Workspace-only: disable the toggle outside Workspace and
    // never leave the dock visible in Gallery or Image.
    m_toggleLayoutPanelAct->setEnabled(workspace);
    m_toggleLayoutPanelAct->setProperty(
        "biltooDisabledHelp",
        tr("The Layout panel is only available in Workspace mode."));
    if (!workspace) {
        if (m_layoutDock->isVisible()) {
            m_layoutDock->setVisible(false);
        }
        if (m_toggleLayoutPanelAct->isChecked()) {
            QSignalBlocker blocker(m_toggleLayoutPanelAct);
            m_toggleLayoutPanelAct->setChecked(false);
        }
        return;
    }
    if (isFullScreen()) {
        return;
    }
    const bool show = m_layoutPreferredInWorkspace;
    if (m_layoutDock->isVisible() != show) {
        m_layoutDock->setVisible(show);
    }
    if (m_toggleLayoutPanelAct->isChecked() != show) {
        QSignalBlocker blocker(m_toggleLayoutPanelAct);
        m_toggleLayoutPanelAct->setChecked(show);
    }
}

void MainWindow::updateUpToGalleryAction()
{
    if (!m_backToGalleryAct) {
        return;
    }
    // Image mode with a session always has somewhere to go Up:
    // Workspace if that was the source, otherwise implicit default Gallery.
    const bool inImage = m_imageView && m_imageView->isImageMode();
    const bool hasSession = !m_session.paths().isEmpty();
    const bool canReturn = inImage && hasSession;
    m_backToGalleryAct->setVisible(true);
    m_backToGalleryAct->setEnabled(canReturn);
    if (m_workspaceReturnActive) {
        m_backToGalleryAct->setStatusTip(tr("Return to Workspace"));
        m_backToGalleryAct->setToolTip(tr("Return to Workspace"));
    } else {
        m_backToGalleryAct->setStatusTip(tr("Return to Gallery"));
        m_backToGalleryAct->setToolTip(tr("Return to Gallery"));
    }
    if (m_imageView) {
        m_imageView->setGalleryReturnAvailable(canReturn);
    }
}

void MainWindow::updateWorkspaceActionVisibility()
{
    updateUpToGalleryAction();
    const bool workspace = m_imageView && m_imageView->isWorkspaceMode();
    // Gallery layout actions: visible once a session exists. Enabled in Image
    // mode on purpose — activating one enters Gallery with that pack. Grid Crop
    // stays hidden until the separate re-enable task (conflicts with manual crop).
    const bool canGallery = !m_session.paths().isEmpty();
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                         m_layoutGridAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct,
                         m_layoutFlowAct, m_layoutFlowFillAct, m_layoutFacingAct}) {
        if (act) {
            act->setVisible(true);
            act->setEnabled(canGallery);
            act->setProperty(
                "biltooDisabledHelp",
                tr("Gallery layouts require a non-empty session in Gallery mode."));
        }
    }
    if (m_layoutGridCropAct) {
        m_layoutGridCropAct->setVisible(false);
        m_layoutGridCropAct->setEnabled(false);
    }
    if (m_layoutFreeFormAct) {
        m_layoutFreeFormAct->setVisible(false);
        m_layoutFreeFormAct->setEnabled(false);
    }
    // Workspace tools: keep menu entries stable; grey out outside Workspace.
    // (Hiding them made the Workspace menu appear to grow/shrink with mode.)
    for (QAction *act : {m_raiseAct, m_lowerAct,
                         m_opacityUpAct, m_opacityDownAct, m_opacityResetAct,
                         m_resetScaleAct, m_resetRotationAct, m_resetShearAct,
                         m_selectToolAct, m_panToolAct, m_zoomToolAct,
                         m_pageGuideAct, m_fitPageGuideAct,
                         m_workspaceBackgroundAct, m_workspaceBgDefaultAct}) {
        if (act) {
            act->setVisible(true);
            act->setEnabled(workspace);
        }
    }
    // Duplicate: Workspace selection or Gallery selection (new session rows).
    if (m_duplicateAct) {
        m_duplicateAct->setVisible(true);
        const bool canDup = m_imageView
            && ((workspace && m_imageView->hasTransformTargets())
                || (m_imageView->isGalleryMode()
                    && !m_imageView->selectedPaths().isEmpty()));
        m_duplicateAct->setEnabled(canDup);
    }
    // Copy/Cut: Workspace selection only. Paste stays available (enters Workspace).
    const bool canEditTiles = workspace && m_imageView
        && m_imageView->hasTransformTargets();
    if (m_copyWorkspaceAct) {
        m_copyWorkspaceAct->setVisible(true);
        m_copyWorkspaceAct->setEnabled(canEditTiles);
    }
    if (m_cutWorkspaceAct) {
        m_cutWorkspaceAct->setVisible(true);
        m_cutWorkspaceAct->setEnabled(canEditTiles);
    }
    if (m_pasteWorkspaceAct) {
        m_pasteWorkspaceAct->setVisible(true);
        updatePasteActionEnabled();
    }
    if (m_undoAct) {
        m_undoAct->setVisible(true);
        m_redoAct->setVisible(true);
    }
    if (m_workspaceToolBar) {
        m_workspaceToolBar->setVisible(workspace && !isFullScreen());
    }
    // Canvas materials: Gallery/Image → session View; Workspace → project.
    // Same main-toolbar action for all three modes (vertical bar no longer
    // duplicates Background).
    if (m_viewBackgroundAct) {
        const bool bgOk = m_imageView
            && (m_imageView->isGalleryMode() || m_imageView->isImageMode()
                || m_imageView->isWorkspaceMode());
        m_viewBackgroundAct->setVisible(true);
        m_viewBackgroundAct->setEnabled(bgOk);
    }
    updateThumbnailBarForMode();
    updateLayoutPanelForMode();
    updateScrollBarPolicyForMode();
    updateMasonryCountControl();
    updateNavigationActions();
}


void MainWindow::syncWorkspaceBackgroundActions()
{
    if (!m_imageView) {
        return;
    }
    const WorkspaceBackground wb = m_imageView->hostCanvasBg().workspaceRef();
    if (m_workspaceBgDefaultAct) {
        m_workspaceBgDefaultAct->setCheckable(true);
        const bool permanentDefault = wb.isAppDefault();
        const bool previewDefault = m_imageView->hostCanvasBg().isWorkspaceShowDefault();
        // Only meaningful when a custom project background exists.
        m_workspaceBgDefaultAct->setEnabled(!permanentDefault);
        const QSignalBlocker blocker(m_workspaceBgDefaultAct);
        m_workspaceBgDefaultAct->setChecked(permanentDefault || previewDefault);
    }
}





