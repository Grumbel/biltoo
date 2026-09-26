// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "text/textsearchpolicy.h"
#include "shell/dualimageshell.h"
#include "session/sessionopen.h"
#include "shell/keyboardshortcutsdialog.h"
#include "version.h"
#include "thumtoo/version.hpp"
#include "imageitem.h"
#include "display/displayquality.h"
#include <QPainter>
#include "slideshow/slideshowclocks.h"

#include <QDebug>
#include <QThreadPool>
#include <atomic>
#include <thread>
#include <vector>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QPointer>
#include <QSet>
#include <QRect>
#include "util/biltoo_logging.h"
#include <cmath>
#include <algorithm>
#include <QtMath>

namespace {

// Bump when dock object names / structure change, or when a saved
// dockLayoutState blob is known to crash on restore (Qt QDockAreaLayout).
// Mismatched version → ignore blob and use built-in defaults.
constexpr int kDockLayoutStateVersion = 1;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Biltoo"));
    setWindowIcon(QApplication::windowIcon());
    resize(1024, 768);
    setAcceptDrops(true);

    m_imageView = new ImageView(this);
    // Phase 6 Tier 4b: single appearance store on SessionDocument.
    m_imageView->bindSessionSeedBook(&m_session.seedBook());
    // Tier 4 path-order: dual-write view book + document until book is deleted.
    m_imageView->bindSessionDocument(&m_session);
    m_imageView->setFilmstripSelectionProvider([this]() {
        return m_thumbnailBar ? m_thumbnailBar->selectedSessionIds()
                              : QList<SessionImageId>{};
    });
    m_imageView->setAccessibleName(tr("Image view"));
    m_imageView->setAccessibleDescription(
        tr("Shows the current image. In image mode, click the left or right edge "
           "to go to the previous or next image. Use Go menu for keyboard navigation."));
    m_imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageView, &QWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);
    connect(m_imageView, &ImageView::mouseInfoChanged,
            this, &MainWindow::onMouseInfoChanged);
    connect(m_imageView, &ImageView::slideshowTogglePauseRequested, this,
            [this]() {
                if (m_slideshowPaused) {
                    resumeSlideshow();
                } else if (m_slideshowClockRunning) {
                    pauseSlideshow();
                }
            });
    connect(m_imageView, &ImageView::slideshowSeekRequested, this,
            &MainWindow::seekSlideshowFraction);
    connect(m_imageView, &ImageView::navigatePreviousRequested,
            this, &MainWindow::goPrevious);
    connect(m_imageView, &ImageView::navigateNextRequested,
            this, &MainWindow::goNext);
    connect(m_imageView, &ImageView::linkActivated, this,
            [this](int page, const QString &uri) {
                if (page > 0) {
                    navigateDocumentPage(page);
                } else if (!uri.isEmpty()) {
                    openDocumentLinkUri(uri);
                }
            });

    connect(m_imageView, &ImageView::galleryReturnRequested,
            this, &MainWindow::returnFromImageMode);
    connect(m_imageView, &ImageView::cropModeChanged, this, [this](bool on) {
        if (m_cropAct) {
            m_cropAct->setChecked(on);
        }
    });
    connect(m_imageView, &ImageView::attentionModeChanged, this, [this](bool on) {
        if (m_attentionAct) {
            m_attentionAct->setChecked(on);
        }
    });
    connect(m_imageView,
            QOverload<SessionImageId, const QString &, const QImage &>::of(
                &ImageView::sessionAppearanceChanged),
            this,
            [this](SessionImageId id, const QString &path, const QImage &image) {
                if (m_thumbnailBar) {
                    // Non-crop appearance must not replace a sticky crop bake.
                    m_thumbnailBar->setSessionImageOverride(id, path, image, false);
                }
            });
    connect(m_imageView,
            QOverload<SessionImageId, const QString &, const QImage &, bool>::of(
                &ImageView::sessionCropApplied),
            this,
            [this](SessionImageId id, const QString &path, const QImage &image, bool hasCrop) {
                if (m_thumbnailBar) {
                    m_thumbnailBar->setSessionImageOverride(id, path, image, true, hasCrop);
                }
            });
    connect(m_imageView, &ImageView::fullscreenToggleRequested,
            this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageView::galleryItemOpenRequested,
            this, &MainWindow::openGalleryItemInImageMode);
    connect(m_imageView, &ImageView::sessionImageOpenRequested,
            this, &MainWindow::openSessionImageInImageMode);
    connect(m_imageView, &ImageView::sessionSlotOpenRequested,
            this, &MainWindow::openSessionIndexInImageMode);
    connect(m_imageView, &ImageView::sessionRemovePathsRequested,
            this, &MainWindow::removeSessionPaths);
    connect(m_imageView, &ImageView::sessionRemoveIdsRequested,
            this, &MainWindow::removeSessionIds);
    connect(m_imageView, &ImageView::sessionRemoveIndicesRequested,
            this, &MainWindow::removeSessionIndices);
    connect(m_imageView, &ImageView::sessionSlotFocused,
            this, [this](int idx) {
                if (idx < 0) {
                    return;
                }
                if (isGalleryMode()) {
                    QTimer::singleShot(0, this, [this, idx]() {
                        setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                    });
                } else {
                    setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                }
            });
    connect(m_imageView, &ImageView::sessionImageFocused,
            this, [this](SessionImageId id) {
                const int idx = indexOfSessionId(id);
                if (idx < 0) {
                    return;
                }
                // Gallery: paint selection first; session cursor / filmstrip next tick.
                if (isGalleryMode()) {
                    QTimer::singleShot(0, this, [this, idx]() {
                        setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                    });
                } else {
                    setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                }
            });
    connect(m_imageView, &ImageView::galleryItemFocused,
            this, [this](const QString &path) {
                auto apply = [this](int idx) {
                    if (idx < 0) {
                        return;
                    }
                    if (isGalleryMode()) {
                        QTimer::singleShot(0, this, [this, idx]() {
                            setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                        });
                    } else {
                        setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                    }
                };
                // Prefer selected/sole live tile so duplicate paths focus the
                // correct session row (not paths().indexOf first match).
                if (ImageItem *pref = m_imageView->findPreferredItemForPath(path)) {
                    if (pref->sessionId() != kInvalidSessionImageId) {
                        const int idx = indexOfSessionId(pref->sessionId());
                        if (idx >= 0) {
                            apply(idx);
                            return;
                        }
                    }
                    {
                        const int listIdx = m_imageView->sessionListIndex(pref);
                        if (listIdx >= 0) {
                            apply(listIdx);
                            return;
                        }
                    }
                }
                // No live preferred tile: bound session row, else pure path index.
                apply(indexOfPathPreferId(path));
            });
    connect(m_imageView, &ImageView::filesDropped,
            this, &MainWindow::onFilesDropped);
    connect(m_imageView, &ImageView::stickyZoomChanged,
            this, &MainWindow::syncZoomModeChecks);


    m_thumbnailBar = new ThumbnailBar(this);
    m_thumbnailBar->setSearchHitIndex(&m_searchIndex);
    if (m_imageView) {
        m_imageView->hostGallery().setSearchHitIndex(&m_searchIndex);
    }
    m_thumbnailBar->setAccessibleName(tr("Thumbnails"));
    if (m_imageView) {
        m_thumbnailBar->setStripBackground(m_imageView->hostCanvasBg().primaryColor());
        // Filmstrip cell aspect = ItemWorld content ops + native size (not sample
        // pixmap size). SessionImageId is identity; path XDG is unbound fallback
        // inside ThumbnailBar when provider returns empty.
        m_thumbnailBar->setLayoutAspectProvider(
            [this](SessionImageId sid, const QString &path) -> QSize {
                if (!m_imageView || path.isEmpty()) {
                    return {};
                }
                // Same API as Gallery/Workspace/Image placeholders.
                return m_imageView->contentLayoutSize(path, sid);
            });
        // Filmstrip cell *pixels* content ops from ItemWorld (not path-only XDG).
        m_thumbnailBar->setContentAppearanceProvider(
            [this](SessionImageId sid, const QString &path) -> WorkspaceItemState {
                WorkspaceItemState want;
                if (!m_imageView || path.isEmpty()) {
                    return want;
                }
                if (sid != kInvalidSessionImageId
                    && m_imageView->itemWorld().hasDurableAppearance(sid)) {
                    want = m_imageView->sessionAppearanceValue(sid);
                }
                // Bound: ItemWorld only — no path XDG (matches Image underlay).
                // Unbound: full path XDG including crop.
                if (sid == kInvalidSessionImageId
                    && !SessionAppearance::hasContentAppearance(want)
                    && want.colorAdjust.isIdentity()) {
                    ThumtooCache::StoredContentAppearance stored;
                    if (ThumtooCache::loadContentAppearance(path, &stored)
                        && !stored.isIdentity()) {
                        SessionAppearance::applyStoredContentAppearance(&want, stored);
                    }
                }
                return want;
            });
        // Image-mode ←/→: reuse filmstrip Soft (and ImageCache) instead of LQIP
        // when the strip already decoded the path.
        m_imageView->setImageModeSoftProvider(
            [this](const QString &path, SessionImageId sid, bool *displayReady) {
                if (!m_thumbnailBar) {
                    return QImage();
                }
                return m_thumbnailBar->sampleForImageModePending(path, sid, displayReady);
            });
    }
    connect(m_thumbnailBar, &ThumbnailBar::indexActivated,
            this, &MainWindow::onThumbnailActivated);
    connect(m_thumbnailBar, &ThumbnailBar::indexNavigated,
            this, &MainWindow::onThumbnailNavigated);
    connect(m_thumbnailBar, &ThumbnailBar::workspaceSelectionChanged,
            this, &MainWindow::onThumbnailWorkspaceSelectionChanged);
    connect(m_thumbnailBar, &ThumbnailBar::removeIndicesRequested,
            this, &MainWindow::removeSessionIndices);
    connect(m_thumbnailBar, &ThumbnailBar::reorderRowsRequested,
            this, &MainWindow::reorderSessionRows);
    connect(m_thumbnailBar, &ThumbnailBar::loadsChanged,
            this, &MainWindow::updateStatus);
    // Filmstrip put soft into ImageCache; Gallery pass1 only runs on decode-window
    // ticks. Without this, tiles stay blank until scroll / ladderReady / watchdog.
    connect(m_thumbnailBar, &ThumbnailBar::loadsChanged, this, [this]() {
        if (isGalleryMode() && m_imageView) {
            m_imageView->hostGallery().scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSettleMs);
        }
    });
    connect(m_imageView, &ImageView::workspacePathsChanged,
            this, &MainWindow::onWorkspacePathsChanged);
    connect(m_imageView, &ImageView::canvasSelectionChanged, this, [this]() {
        // Let the tile selection paint first; action enablement can wait a tick.
        if (isGalleryMode()) {
            QTimer::singleShot(0, this, [this]() {
                if (!isGalleryMode()) {
                    return;
                }
                updateNavigationActions();
                // Mirror Gallery multi-select onto the filmstrip (same as Workspace).
                if (!m_syncingSelection && m_thumbnailBar && m_imageView) {
                    m_syncingSelection = true;
                    m_thumbnailBar->setSelectedIndices(m_imageView->hostWorkspace().selectedSessionIndices());
                    m_syncingSelection = false;
                }
            });
            return;
        }
        updateNavigationActions();
        if (isWorkspaceMode()) {
            updateWorkspaceActionVisibility();
            updateLayoutPanel();
        }
        if (m_syncingSelection || !isWorkspaceMode() || !m_thumbnailBar || !m_imageView) {
            return;
        }
        m_syncingSelection = true;
        m_thumbnailBar->setSelectedIndices(m_imageView->hostWorkspace().selectedSessionIndices());
        m_syncingSelection = false;
    });

    m_imageView->setMinimumHeight(120);
    m_dualShell = new DualImageShell(m_imageView, this);
    setCentralWidget(m_dualShell);
    connect(m_dualShell, &DualImageShell::activeViewChanged, this, [this](ImageView *view) {
        Q_UNUSED(view);
        // PreferCache / tile ticks already follow setActiveHost inside the shell.
        // Session navigation and filmstrip stay on primary (m_imageView).
    });

    m_thumbnailDock = new QDockWidget(tr("Filmstrip"), this);
    m_thumbnailDock->setObjectName(QStringLiteral("ThumbnailDock"));
    m_thumbnailDock->setWidget(m_thumbnailBar);
    m_thumbnailDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                     | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_thumbnailDock->setFeatures(QDockWidget::DockWidgetClosable
                                 | QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_thumbnailDock);
    connect(m_thumbnailDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_toggleThumbnailBarAct && m_toggleThumbnailBarAct->isChecked() != visible) {
            m_toggleThumbnailBarAct->setChecked(visible);
        }
        if (!isFullScreen()) {
            m_thumbnailBarVisibleBeforeFullscreen = visible;
        }
    });
    connect(m_thumbnailDock, &QDockWidget::dockLocationChanged, this,
            &MainWindow::onThumbnailDockLocationChanged);

    m_metadataPanel = new MetadataPanel(this);
    m_metadataDock = new QDockWidget(tr("Metadata"), this);
    m_metadataDock->setObjectName(QStringLiteral("MetadataDock"));
    m_metadataDock->setWidget(m_metadataPanel);
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_metadataDock->setFeatures(QDockWidget::DockWidgetClosable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
    m_metadataDock->hide();
    // When the user opens the panel, load metadata for the current selection
    // (selection itself skips decode while the dock is hidden).
    connect(m_metadataDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_metadataPanel) {
            m_metadataPath.clear();
            updateMetadataPanel();
        }
    });

    m_adjustmentsPanel = new AdjustmentsPanel(this);
    m_adjustmentsDock = new QDockWidget(tr("Adjustments"), this);
    m_adjustmentsDock->setObjectName(QStringLiteral("AdjustmentsDock"));
    m_adjustmentsDock->setWidget(m_adjustmentsPanel);
    m_adjustmentsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_adjustmentsDock->setFeatures(QDockWidget::DockWidgetClosable
                                   | QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_adjustmentsDock);
    m_adjustmentsDock->hide();
    connect(m_adjustmentsDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateAdjustmentsPanel();
            updateCropPanel();
        }
    });
    connect(m_adjustmentsPanel, &AdjustmentsPanel::applyToSelectionRequested,
            this, [this](const ColorAdjustments &adj) {
                if (!m_imageView || !m_adjustmentsPanel) {
                    return;
                }
                QList<SessionImageId> filmIds;
                if (m_thumbnailBar) {
                    filmIds = m_thumbnailBar->selectedSessionIds();
                }
                const auto targets = BatchTargets::resolve(
                    m_imageView, m_adjustmentsPanel->targetMode(), filmIds,
                    m_adjustmentsPanel->rangeFrom(), m_adjustmentsPanel->rangeTo());
                const int n = m_imageView->hostImage().applyColorAdjustmentsToBatch(adj, targets);
                if (n > 0 && statusBar()) {
                    statusBar()->showMessage(
                        tr("Colour grade applied to %n image(s)", "", n), 4000);
                }
                updateAdjustmentsPanel();
                updateCropPanel();
            });
    connect(m_adjustmentsPanel, &AdjustmentsPanel::adjustmentsChanged,
            this, [this](const ColorAdjustments &adj) {
                if (m_imageView) {
                    m_imageView->hostImage().setTargetColorAdjustments(adj);
                }
                // Histogram / vectorscope: rebuild after slider idle (not every tick).
                if (!m_adjustmentsPreviewTimer) {
                    m_adjustmentsPreviewTimer = new QTimer(this);
                    m_adjustmentsPreviewTimer->setSingleShot(true);
                    m_adjustmentsPreviewTimer->setInterval(200);
                    connect(m_adjustmentsPreviewTimer, &QTimer::timeout, this, [this]() {
                        if (!m_adjustmentsPanel || !m_imageView) {
                            return;
                        }
                        ImageItem *item = m_imageView->targetItem();
                        if (!item && !m_imageView->liveItems().isEmpty()
                            && m_imageView->isImageMode()) {
                            item = m_imageView->liveItems().first();
                        }
                        if (item) {
                            m_adjustmentsPanel->setPreviewImage(item->pixmap().toImage());
                        }
                    });
                }
                m_adjustmentsPreviewTimer->start();
            });

    m_cropPanel = new CropPanel(this);
    m_cropDock = new QDockWidget(tr("Crop"), this);
    m_cropDock->setObjectName(QStringLiteral("CropDock"));
    m_cropDock->setWidget(m_cropPanel);
    m_cropDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_cropDock->setFeatures(QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_cropDock);
    m_cropDock->hide();

    m_ocrPanel = new OcrPanel(this);
    m_ocrDock = new QDockWidget(tr("OCR"), this);
    m_ocrDock->setObjectName(QStringLiteral("OcrDock"));
    m_ocrDock->setWidget(m_ocrPanel);
    m_ocrDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_ocrDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_ocrDock);
    m_ocrDock->hide();
    {
        QSettings settings;
        m_ocrPanel->setLanguage(
            settings.value(QStringLiteral("ocr/lang"), QStringLiteral("eng")).toString());
        m_ocrPanel->setJobs(settings.value(QStringLiteral("ocr/jobs"), 2).toInt());
    }
    connect(m_ocrPanel, &OcrPanel::runRequested, this, &MainWindow::runOcrFromPanel);
    connect(m_ocrPanel, &OcrPanel::cancelRequested, this, &MainWindow::cancelOcrBatch);

    m_textPanel = new TextPanel(this);
    m_textDock = new QDockWidget(tr("Text"), this);
    m_textDock->setObjectName(QStringLiteral("TextDock"));
    m_textDock->setWidget(m_textPanel);
    m_textDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_textDock->setFeatures(QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_textDock);
    m_textDock->hide();
    connect(m_textDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateTextPanel();
            connectTextPanel();
        }
    });

    connect(m_cropDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateCropPanel();
        }
    });
    connect(m_cropPanel, &CropPanel::applyToSelectionRequested,
            this, [this](const CropPanelRecipe &recipe) {
                if (!m_imageView || !m_cropPanel) {
                    return;
                }
                QList<SessionImageId> filmIds;
                if (m_thumbnailBar) {
                    filmIds = m_thumbnailBar->selectedSessionIds();
                }
                const auto targets = BatchTargets::resolve(
                    m_imageView, m_cropPanel->targetMode(), filmIds,
                    m_cropPanel->rangeFrom(), m_cropPanel->rangeTo());
                const int n = m_imageView->hostCrop().applyCropRecipeToBatch(recipe, targets);
                if (n > 0 && statusBar()) {
                    statusBar()->showMessage(
                        tr("Crop applied to %n image(s)", "", n), 4000);
                }
                updateCropPanel();
            });
    connect(m_cropPanel, &CropPanel::applyToCurrentRequested,
            this, [this](const CropPanelRecipe &recipe) {
                if (!m_imageView) {
                    return;
                }
                const auto targets = BatchTargets::resolve(
                    m_imageView, BatchTargets::Mode::Current, {});
                const int n = m_imageView->hostCrop().applyCropRecipeToBatch(recipe, targets);
                if (n > 0 && statusBar()) {
                    statusBar()->showMessage(
                        tr("Crop applied to %n image(s)", "", n), 4000);
                }
                updateCropPanel();
            });
    connect(m_cropPanel, &CropPanel::resetCropOnSelectionRequested, this, [this]() {
        if (!m_imageView || !m_cropPanel) {
            return;
        }
        QList<SessionImageId> filmIds;
        if (m_thumbnailBar) {
            filmIds = m_thumbnailBar->selectedSessionIds();
        }
        const auto targets = BatchTargets::resolve(
            m_imageView, m_cropPanel->targetMode(), filmIds,
            m_cropPanel->rangeFrom(), m_cropPanel->rangeTo());
        const int n = m_imageView->hostCrop().resetCropOnBatch(targets);
        if (n > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("Crop reset on %n image(s)", "", n), 4000);
        }
        updateCropPanel();
    });
    connect(m_cropPanel, &CropPanel::resetCropOnCurrentRequested, this, [this]() {
        if (!m_imageView) {
            return;
        }
        const auto targets = BatchTargets::resolve(
            m_imageView, BatchTargets::Mode::Current, {});
        const int n = m_imageView->hostCrop().resetCropOnBatch(targets);
        if (n > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("Crop reset on %n image(s)", "", n), 4000);
        }
        updateCropPanel();
    });

    connect(m_cropPanel, &CropPanel::suggestFromTextRequested, this, [this]() {
        if (!m_imageView || !m_cropPanel) {
            return;
        }
        const QString path = m_imageView->hostImage().classicPath();
        if (path.isEmpty() || !PagePath::isPageRef(path)) {
            if (statusBar()) {
                statusBar()->showMessage(tr("Suggest margins needs a document page"), 4000);
            }
            m_cropPanel->setStatusText(tr("No document page"));
            return;
        }
        QSize logical = m_imageView->hostSizeBook().known(path);
        if (logical.isEmpty()) {
            logical = ThumtooCache::cachedSize(path);
        }
        if (logical.width() < 8 || logical.height() < 8) {
            if (statusBar()) {
                statusBar()->showMessage(tr("Page size unknown — open the page first"), 4000);
            }
            m_cropPanel->setStatusText(tr("Page size unknown"));
            return;
        }
        // Prefer OCR (has region kinds); fall back to native / Auto policy.
        ThumtooCache::PageTextLayer layer =
            TextLayerResolve::load(path, TextLayerResolve::Prefer::Ocr);
        if (layer.regions.isEmpty()) {
            layer = TextLayerResolve::load(path, TextLayerResolve::Prefer::Auto);
        }
        if (layer.regions.isEmpty() || !layer.pageBounds.isValid()) {
            if (statusBar()) {
                statusBar()->showMessage(
                    tr("No text layer — View → OCR This Page or open a text PDF"), 5000);
            }
            m_cropPanel->setStatusText(tr("No text layer with regions"));
            return;
        }
        const QString docPath = PagePath::documentFilePath(path);
        const bool pageYUp = PagePath::isDjvuFile(docPath);
        QVector<QRectF> bands;
        bands.reserve(layer.regions.size());
        for (const ThumtooCache::TextRegion &r : layer.regions) {
            if (r.kind != ThumtooCache::TextRegion::Kind::Header
                && r.kind != ThumtooCache::TextRegion::Kind::Footer
                && r.kind != ThumtooCache::TextRegion::Kind::PageNumber) {
                continue;
            }
            const QRectF img = ThumtooCache::pageRectToImageRect(
                r.bbox, layer.pageBounds, logical, pageYUp);
            if (img.isValid() && !img.isEmpty()) {
                bands.append(img);
            }
        }
        const CropRecipeUtil::SuggestedMargins sug =
            CropRecipeUtil::suggestMarginsFromBandRegions(logical, bands);
        if (!sug.ok) {
            if (statusBar()) {
                statusBar()->showMessage(
                    tr("No header/footer/page-number bands to crop"), 4000);
            }
            m_cropPanel->setStatusText(tr("No header/footer bands found"));
            return;
        }
        CropPanelRecipe recipe = m_cropPanel->recipe();
        recipe.mode = CropPanelRecipe::Mode::ManualMargins;
        recipe.marginLeft = sug.left;
        recipe.marginTop = sug.top;
        recipe.marginRight = sug.right;
        recipe.marginBottom = sug.bottom;
        m_cropPanel->setRecipe(recipe);
        // setRecipe does not emit; drive the same debounced preview path.
        ImageItem *item = m_imageView->targetItem();
        if (!item && m_imageView->isImageMode()
            && !m_imageView->liveItems().isEmpty()) {
            item = m_imageView->liveItems().first();
        }
        if (item) {
            m_imageView->hostCrop().previewCropRecipeOnItem(item, recipe);
        }
        updateCropPanel();
        if (statusBar()) {
            statusBar()->showMessage(
                tr("Suggested margins T=%1 B=%2 (from text bands)")
                    .arg(sug.top)
                    .arg(sug.bottom),
                5000);
        }
        m_cropPanel->setStatusText(
            tr("Suggested from text: top %1 px, bottom %2 px")
                .arg(sug.top)
                .arg(sug.bottom));
    });

    connect(m_cropPanel, &CropPanel::recipeChanged, this, [this](const CropPanelRecipe &recipe) {
        if (!m_imageView || !m_cropPanel) {
            return;
        }
        // Debounced soft preview on the current page only (no durable write).
        if (!m_cropPreviewTimer) {
            m_cropPreviewTimer = new QTimer(this);
            m_cropPreviewTimer->setSingleShot(true);
            m_cropPreviewTimer->setInterval(180);
            connect(m_cropPreviewTimer, &QTimer::timeout, this, [this]() {
                if (!m_imageView || !m_cropPanel) {
                    return;
                }
                ImageItem *item = m_imageView->targetItem();
                if (!item && m_imageView->isImageMode()
                    && !m_imageView->liveItems().isEmpty()) {
                    item = m_imageView->liveItems().first();
                }
                if (!item) {
                    return;
                }
                const CropPanelRecipe r = m_cropPanel->recipe();
                m_imageView->hostCrop().previewCropRecipeOnItem(item, r);
                updateCropPanel();
            });
        }
        m_cropPreviewTimer->start();
        Q_UNUSED(recipe);
    });

    m_layoutPanel = new LayoutPanel(this);
    m_layoutDock = new QDockWidget(tr("Layout"), this);
    m_layoutDock->setObjectName(QStringLiteral("LayoutDock"));
    m_layoutDock->setWidget(m_layoutPanel);
    m_layoutDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_layoutDock->setFeatures(QDockWidget::DockWidgetClosable
                              | QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_layoutDock);
    m_layoutDock->hide();
    connect(m_layoutPanel, &LayoutPanel::applyRequested,
            this, &MainWindow::applyWorkspaceLayoutFromPanel);

    m_tocPanel = new TocPanel(this);
    m_tocDock = new QDockWidget(tr("Contents"), this);
    m_tocDock->setObjectName(QStringLiteral("TocDock"));
    m_tocDock->setWidget(m_tocPanel);
    m_tocDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_tocDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_tocDock);
    m_tocDock->hide();
    connect(m_tocPanel, &TocPanel::navigateToPage, this, &MainWindow::navigateDocumentPage);
    connect(m_tocPanel, &TocPanel::openExternalUri, this, &MainWindow::openDocumentLinkUri);
    connect(m_tocDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateTocPanel();
        }
    });

    m_helpPanel = new HelpPanel(this);
    m_helpDock = new QDockWidget(tr("Help"), this);
    m_helpDock->setObjectName(QStringLiteral("HelpDock"));
    m_helpDock->setWidget(m_helpPanel);
    m_helpDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_helpDock->setFeatures(QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_helpDock);
    m_helpDock->hide();
    connect(m_helpPanel, &HelpPanel::showAllShortcutsRequested,
            this, &MainWindow::showKeyboardShortcuts);

    createActions();
    createMenus();
    updateFileExportActions();
    createToolBar();
    createStatusBar();

    // Use status tips as hover tooltips on the toolbar and menus
    for (QAction *act : findChildren<QAction *>()) {
        if (act->toolTip().isEmpty() && !act->statusTip().isEmpty()) {
            act->setToolTip(act->statusTip());
        }
    }

    populateActionHelpTexts();
    installActionHelpTracking();

    bindViewerShortcuts();

    // Application-wide shortcuts so they work while the image view has focus
    auto *escShortcut = new QShortcut(Qt::Key_Escape, this);
    escShortcut->setContext(Qt::WindowShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        // Prefer focusWidget ancestry: hasFocus() can be false for clear-button
        // children or right after ShortcutOverride races.
        auto focusIn = [](QWidget *root) -> bool {
            if (!root || !root->isVisible()) {
                return false;
            }
            QWidget *f = QApplication::focusWidget();
            return f && (f == root || root->isAncestorOf(f));
        };
        if (focusIn(m_locationBar) || (m_locationEdit && m_locationEdit->hasFocus())) {
            cancelLocationBar();
            return;
        }
        if (focusIn(m_searchBar) || (m_searchEdit && m_searchEdit->hasFocus())) {
            cancelSearchBar();
            return;
        }
        if (m_imageView && m_imageView->hostCrop().active()) {
            m_imageView->hostCrop().cancelCrop();
            return;
        }
        // Leave slideshow (playing or paused) before leaving fullscreen / Image mode.
        if (isSlideshowSession()) {
            stopSlideshow();
            return;
        }
        if (isFullScreen()) {
            showNormal();
            return;
        }
        // Image mode with a session always has Up (Workspace or implicit Gallery).
        if (m_imageView && m_imageView->isImageMode() && !m_session.paths().isEmpty()) {
            returnFromImageMode();
        }
    });

    // Dedicated F/F11 shortcuts (WindowShortcut) so leave-fullscreen is
    // reliable even when the checkable action and window state briefly disagree.
    // Window-scoped so a second MainWindow does not fight for the same keys.
    for (const int key : {static_cast<int>(Qt::Key_F), static_cast<int>(Qt::Key_F11)}) {
        auto *sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this]() {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
        });
    }

    connect(m_imageView, &ImageView::statusChanged, this, &MainWindow::updateStatus);

    m_slideshowTimer = new QTimer(this);
    m_slideshowTimer->setTimerType(Qt::PreciseTimer);
    // Repeating clock tick. Scheduling is pure from elapsed time — no
    // single-shot arm/resume chains (those raced when interval == transition).
    m_slideshowTimer->setSingleShot(false);
    m_slideshowTimer->setInterval(16);
    connect(m_slideshowTimer, &QTimer::timeout, this, &MainWindow::onSlideshowTick);
    m_cursorHideTimer = new QTimer(this);
    m_cursorHideTimer->setSingleShot(true);
    m_cursorHideTimer->setInterval(1000);
    connect(m_cursorHideTimer, &QTimer::timeout, this, &MainWindow::hideSlideshowCursor);

    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(false);
    } else {
        m_thumbnailBar->setVisible(false);
    }
    updateNavigationActions();
    readSettings();
    updateWorkspaceActionVisibility();
}

MainWindow::~MainWindow() = default;

bool MainWindow::isWorkspaceMode() const
{
    return m_imageView && m_imageView->isWorkspaceMode();
}

bool MainWindow::isGalleryMode() const
{
    return m_imageView && m_imageView->isGalleryMode();
}

bool MainWindow::isImageMode() const
{
    return m_imageView && m_imageView->isImageMode();
}


void MainWindow::onThumbnailWorkspaceSelectionChanged()
{
    // Image mode: Ctrl/Shift on the filmstrip only updates strip selection —
    // refresh Open Selection / bulk actions; do not touch the canvas.
    if (!isWorkspaceMode() && !isGalleryMode()) {
        updateNavigationActions();
        return;
    }
    const QList<int> sel = m_thumbnailBar->selectedIndices();
    if (!sel.isEmpty()) {
        const int idx = sel.last();
        if (idx != m_currentIndex && idx >= 0 && idx < m_session.paths().size()) {
            m_currentIndex = idx;
            // Keep ImageView session cursor aligned (filmstrip multi-select does not
            // call setCurrentIndex). Image mode underlay is inactive here, but a
            // later openSession must not inherit a lagging hostSessionId.
            if (m_imageView) {
                m_imageView->setCurrentSessionId(sessionIdAt(idx));
                m_imageView->hostSlideshow().setSessionPosition(
                    idx, m_session.paths().size(), false);
            }
            // Invalidate so updateStatus → updateMetadataPanel refreshes when visible.
            if (m_metadataPanel) {
                m_metadataPath.clear();
            }
        }
    }
    // Shared selection: filmstrip drives canvas selection by session slot.
    if (!m_syncingSelection && m_imageView) {
        m_syncingSelection = true;
        m_imageView->hostWorkspace().selectBySessionIndices(sel);
        m_syncingSelection = false;
        if (isGalleryMode() && !sel.isEmpty() && sel.last() >= 0
            && sel.last() < m_session.paths().size()) {
            const SessionImageId sid = sessionIdAt(sel.last());
            if (sid != kInvalidSessionImageId) {
                m_imageView->hostGallery().revealSessionId(sid);
            } else {
                m_imageView->hostGallery().revealPath(m_session.paths().at(sel.last()));
            }
        }
    }
    updateStatus();
}


void MainWindow::onWorkspacePathsChanged()
{
    if (!isWorkspaceMode()) {
        return;
    }
    markWorkspaceDirty();
    syncThumbnailCanvasMembership();
    updateStatus();
}
void MainWindow::syncThumbnailCanvasMembership()
{
    if (!m_thumbnailBar || !m_imageView) {
        return;
    }
    // Ensure every on-canvas item is tied to a session row (badges + shared selection).
    m_imageView->hostWorkspace().rebindSession(m_session.paths(), m_session.ids());

    // After rebind demotes duplicate SessionImageIds, allocate a fresh session
    // image for each still-unbound live tile so two Workspace tiles never share
    // one filmstrip row / one appearance slot.
    bool grew = false;
    for (ImageItem *item : m_imageView->liveItems()) {
        if (!item) {
            continue;
        }
        if (item->sessionId() != kInvalidSessionImageId
            && m_session.indexOfId(item->sessionId()) >= 0) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        // Paste / drop still decoding: a PendingSessionBind owns the next id.
        // Allocating here created phantom filmstrip rows that could not drag.
        if (m_imageView->hostBindBook().hasBindForPath(path)) {
            continue;
        }
        const SessionImageId id = allocSessionId();
        m_session.append(path, id);
        m_imageView->setItemSessionId(item, id);
        item->setSessionIndex(m_session.size() - 1);
        // Preserve current pixels as the new session image's appearance.
        m_imageView->hostImage().commitItemSessionEdit(item);
        grew = true;
    }
    if (grew) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        m_imageView->hostWorkspace().rebindSession(m_session.paths(), m_session.ids());
    }

    // Badge by stable id → session row, not path (duplicate-safe).
    QSet<int> onCanvas;
    for (ImageItem *item : m_imageView->liveItems()) {
        if (!item) {
            continue;
        }
        const int idx = m_imageView->sessionListIndex(item);
        if (idx >= 0) {
            onCanvas.insert(idx);
        }
    }
    m_thumbnailBar->setOnCanvasIndices(onCanvas);
}

void MainWindow::zoomIn()
{
    m_imageView->hostImage().zoomIn();
    syncZoomModeChecks();
}

void MainWindow::zoomOut()
{
    m_imageView->hostImage().zoomOut();
    syncZoomModeChecks();
}

void MainWindow::zoomReset()
{
    // Sticky 1:1 is Image-mode only. Gallery/Workspace: one-shot view reset.
    if (m_imageView->isImageMode()) {
        if (m_imageView->hostFraming().isStickyZoomEnabled()
            && m_imageView->hostFraming().currentStickyZoomKind() == StickyZoomKind::Actual) {
            m_imageView->hostImage().releaseStickyZoom();
            syncZoomModeChecks();
            return;
        }
        m_imageView->hostImage().zoomReset();
        m_imageView->hostFraming().setStickyZoomKind(StickyZoomKind::Actual);
        m_imageView->hostImage().setStickyZoomEnabled(true);
    } else {
        m_imageView->hostImage().releaseStickyZoom();
        m_imageView->hostImage().zoomReset();
    }
    syncZoomModeChecks();
}

void MainWindow::zoomFit()
{
    if (m_imageView->isImageMode()) {
        if (m_imageView->hostFraming().isStickyZoomEnabled()
            && m_imageView->hostFraming().currentStickyZoomKind() == StickyZoomKind::Fit) {
            m_imageView->hostImage().releaseStickyZoom();
            syncZoomModeChecks();
            return;
        }
        m_imageView->hostImage().zoomFit();
        m_imageView->hostFraming().setStickyZoomKind(StickyZoomKind::Fit);
        m_imageView->hostImage().setStickyZoomEnabled(true);
    } else {
        m_imageView->hostImage().releaseStickyZoom();
        m_imageView->hostImage().zoomFit();
    }
    syncZoomModeChecks();
}

void MainWindow::zoomFill()
{
    if (m_imageView->isImageMode()) {
        if (m_imageView->hostFraming().isStickyZoomEnabled()
            && m_imageView->hostFraming().currentStickyZoomKind() == StickyZoomKind::Fill) {
            m_imageView->hostImage().releaseStickyZoom();
            syncZoomModeChecks();
            return;
        }
        m_imageView->hostImage().zoomFill();
        m_imageView->hostFraming().setStickyZoomKind(StickyZoomKind::Fill);
        m_imageView->hostImage().setStickyZoomEnabled(true);
    } else {
        m_imageView->hostImage().releaseStickyZoom();
        m_imageView->hostImage().zoomFill();
    }
    syncZoomModeChecks();
}

void MainWindow::syncZoomModeChecks()
{
    if (!m_imageView) {
        return;
    }
    const bool sticky = m_imageView->hostFraming().isStickyZoomEnabled();
    const auto kind = m_imageView->hostFraming().currentStickyZoomKind();
    auto setCheck = [](QAction *act, bool on) {
        if (!act) {
            return;
        }
        const QSignalBlocker blocker(act);
        act->setChecked(on);
    };
    setCheck(m_zoomFitAct, sticky && kind == StickyZoomKind::Fit);
    setCheck(m_zoomFillAct, sticky && kind == StickyZoomKind::Fill);
    setCheck(m_zoom1to1Act, sticky && kind == StickyZoomKind::Actual);
}

void MainWindow::toggleFullscreen()
{
    // Always drive from the real window state so leave-fullscreen cannot stick
    // when a checkable action's checked flag desyncs from the WM.
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::rotateLeft()
{
    m_imageView->hostImage().rotateLeft();
}

void MainWindow::rotateRight()
{
    m_imageView->hostImage().rotateRight();
}

void MainWindow::flipHorizontal()
{
    m_imageView->hostImage().flipHorizontal();
}

void MainWindow::flipVertical()
{
    m_imageView->hostImage().flipVertical();
}

void MainWindow::resetContentAppearance()
{
    if (!m_imageView || !m_imageView->hostImage().targetHasContentAppearance()) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Reset content appearance?"));
    box.setText(tr("Discard flip, rotation, crop, and colour grade for the selected image(s)?"));
    box.setInformativeText(
        tr("The original files are never modified. This clears the local "
           "content appearance saved for those files and restores the on-disk "
           "pixels in this session."));
    QPushButton *resetBtn = box.addButton(tr("Reset"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancelBtn);
    box.exec();
    if (box.clickedButton() != resetBtn) {
        return;
    }
    const int n = m_imageView->hostImage().resetContentAppearanceForTargets();
    if (n > 0) {
        updateStatus();
        updateMetadataPanel();
        if (statusBar()) {
            statusBar()->showMessage(
                tr("Content appearance reset for %n image(s)", "", n), 4000);
        }
    }
}

void MainWindow::toggleCropMode()
{
    if (!m_imageView) {
        return;
    }
    if (m_imageView->hostAttention().active()) {
        m_imageView->hostAttention().setAttentionMode(false);
        if (m_attentionAct) {
            m_attentionAct->setChecked(false);
        }
    }
    const bool want = m_cropAct && m_cropAct->isChecked();
    // Gallery: crop on the packed grid is unusable — open the subject in Image
    // mode, then enter crop once pixels are ready.
    if (want && m_imageView->isGalleryMode()) {
        ImageItem *item = m_imageView->targetItem();
        if (!item || !m_imageView->hostWorkspace().hasSingleCropTarget()) {
            if (m_cropAct) {
                m_cropAct->setChecked(false);
            }
            return;
        }
        const int idx = m_imageView->sessionListIndex(item);
        if (idx < 0) {
            if (m_cropAct) {
                m_cropAct->setChecked(false);
            }
            return;
        }
        m_pendingGalleryCrop = true;
        openSessionIndexInImageMode(idx);
        // LoadReplace is async; one-shot when Image mode has decoded pixels.
        auto *conn = new QMetaObject::Connection;
        *conn = QObject::connect(
            m_imageView, &ImageView::statusChanged, this,
            [this, conn]() {
                if (!m_pendingGalleryCrop || !m_imageView
                    || !m_imageView->isImageMode()) {
                    return;
                }
                ImageItem *primary = m_imageView->targetItem();
                if (!primary && !m_imageView->liveItems().isEmpty()) {
                    primary = m_imageView->liveItems().first();
                }
                // Soft-only is enough to enter crop (prepareCrop loads host).
                // Waiting for hasDecodedPixels left Gallery→Image crop stuck on
                // soft tiles, or entered only after a crop bake arrived.
                if (!primary || !primary->hasDisplayPixels()) {
                    return;
                }
                m_pendingGalleryCrop = false;
                QObject::disconnect(*conn);
                delete conn;
                m_imageView->hostCrop().setCropMode(true);
                if (m_cropAct) {
                    m_cropAct->setChecked(m_imageView->hostCrop().active());
                }
            });
        if (m_cropAct) {
            // Stay unchecked until crop actually opens.
            m_cropAct->setChecked(false);
        }
        return;
    }
    m_pendingGalleryCrop = false;
    m_imageView->hostCrop().setCropMode(want);
    if (m_cropAct) {
        m_cropAct->setChecked(m_imageView->hostCrop().active());
    }
}



void MainWindow::exportDocumentText()
{
    const QStringList pages = documentPagePathsForSearch();
    if (pages.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No document pages to export text from"), 5000);
        }
        return;
    }
    QString suggested = QStringLiteral("document.txt");
    if (m_imageView) {
        const QString path = m_imageView->hostImage().classicPath();
        if (!path.isEmpty()) {
            const QString doc = PagePath::documentFilePath(path);
            if (!doc.isEmpty()) {
                suggested = QFileInfo(doc).completeBaseName() + QStringLiteral(".txt");
            }
        }
    }
    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Export Text"), suggested, tr("Text files (*.txt);;All files (*)"));
    if (outPath.isEmpty()) {
        return;
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Exporting text from %n page(s)…", "", pages.size()), 0);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList blocks;
    blocks.reserve(pages.size());
    int pagesWithText = 0;
    int totalRegions = 0;
    for (const QString &pagePath : pages) {
        ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(pagePath);
        if (layer.regions.isEmpty()) {
            layer = ThumtooCache::ensurePageTextLayer(pagePath);
        }
        QStringList lines;
        for (const ThumtooCache::TextRegion &r : layer.regions) {
            if (r.role != ThumtooCache::TextRegion::Role::Text || r.text.isEmpty()) {
                continue;
            }
            lines.append(r.text);
            ++totalRegions;
        }
        if (!lines.isEmpty()) {
            ++pagesWithText;
            const int pageNo = PagePath::pageNumber(pagePath);
            blocks.append(QStringLiteral("----- page %1 -----").arg(pageNo));
            blocks.append(lines.join(QLatin1Char('\n')));
            blocks.append(QString());
        }
    }
    QApplication::restoreOverrideCursor();
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Text"),
                             tr("Could not write %1").arg(outPath));
        return;
    }
    // UTF-8 with BOM — helps Windows Notepad; Linux editors ignore it.
    const QByteArray utf8 = blocks.join(QLatin1Char('\n')).toUtf8();
    static const char bom[] = "\xEF\xBB\xBF";
    f.write(bom, 3);
    f.write(utf8);
    f.close();
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FIND")) {
        qWarning().noquote()
            << QStringLiteral("[find] exportText path=%1 pages=%2 withText=%3 regions=%4 bytes=%5")
                   .arg(outPath)
                   .arg(pages.size())
                   .arg(pagesWithText)
                   .arg(totalRegions)
                   .arg(utf8.size());
    }
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Exported text from %1/%2 pages (%3 regions) → %4")
                .arg(pagesWithText)
                .arg(pages.size())
                .arg(totalRegions)
                .arg(QFileInfo(outPath).fileName()),
            8000);
    }
}


void MainWindow::openSearchBar()
{
    if (!m_searchBar || !m_searchEdit) {
        return;
    }
    if (m_imageView && m_searchEdit->text() != m_imageView->hostTextLayer().searchQueryRef()) {
        QSignalBlocker block(m_searchEdit);
        m_searchEdit->setText(m_imageView->hostTextLayer().searchQueryRef());
    }
    if (m_searchFuzzyCheck && m_imageView) {
        QSignalBlocker block(m_searchFuzzyCheck);
        m_searchFuzzyCheck->setChecked(m_imageView->hostTextLayer().isSearchFuzzy());
    }
    if (m_searchSourceCombo && m_imageView) {
        QSignalBlocker block(m_searchSourceCombo);
        const int idx = m_searchSourceCombo->findData(int(m_imageView->hostText().layerPrefer()));
        if (idx >= 0) {
            m_searchSourceCombo->setCurrentIndex(idx);
        }
        // Ensure controller matches settings (first open after startup).
        m_imageView->hostText().setLayerPrefer(
            static_cast<TextLayerResolve::Prefer>(m_searchSourceCombo->currentData().toInt()));
    }
    updateSearchMatchLabel();
    m_searchBar->setVisible(true);
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

void MainWindow::cancelSearchBar()
{
    if (!m_searchBar || !m_searchEdit) {
        return;
    }
    m_searchEdit->clearFocus();
    if (!m_searchBarPinned) {
        m_searchBar->setVisible(false);
    }
}


void MainWindow::setSearchBarPinned(bool pinned)
{
    m_searchBarPinned = pinned;
    if (m_showSearchBarAct && m_showSearchBarAct->isChecked() != pinned) {
        QSignalBlocker block(m_showSearchBarAct);
        m_showSearchBarAct->setChecked(pinned);
    }
    if (!m_searchBar) {
        return;
    }
    if (pinned) {
        m_searchBar->setVisible(true);
        if (m_searchEdit && m_imageView) {
            QSignalBlocker block(m_searchEdit);
            m_searchEdit->setText(m_imageView->hostTextLayer().searchQueryRef());
            updateSearchMatchLabel();
        }
    } else if (m_searchEdit && !m_searchEdit->hasFocus()) {
        m_searchBar->setVisible(false);
    }
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    if (!m_imageView) {
        return;
    }
    const bool fuzzy = !m_searchFuzzyCheck || m_searchFuzzyCheck->isChecked();
    m_imageView->hostText().setSearchFuzzy(fuzzy);
    const QString path = m_imageView->hostImage().classicPath();
    m_docSearchPageMatchCount = m_imageView->hostText().setSearchQuery(text);
    // Tag current session row for page-local hits (filmstrip / gallery chrome).
    {
        SessionImageId sid = kInvalidSessionImageId;
        if (m_currentIndex >= 0 && m_currentIndex < m_session.size()) {
            sid = m_session.idAt(m_currentIndex);
        }
        if (text.trimmed().isEmpty()) {
            m_searchIndex.clear();
        } else {
            if (m_searchIndex.query() != text.trimmed()) {
                m_searchIndex.beginQuery(text.trimmed());
            }
            m_searchIndex.addOrUpdate(
                sid, path, quint16(qMin(65535, m_docSearchPageMatchCount)));
        }
        if (m_thumbnailBar) {
            m_thumbnailBar->viewport()->update();
        }
        if (m_imageView->viewport()) {
            m_imageView->viewport()->update();
        }
    }

    m_docSearchHitPages.clear();
    m_docSearchHitIndex = -1;
    m_docSearchQuery = text.trimmed();
    updateSearchMatchLabel();
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FIND")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[find] query=%1 fuzzy=%2 path=%3 pageRef=%4 hasLayer=%5 regions=%6 matches=%7 mode=%8")
                   .arg(text.trimmed())
                   .arg(fuzzy)
                   .arg(path)
                   .arg(PagePath::isPageRef(path))
                   .arg(m_imageView->hostText().hasLayer())
                   .arg(m_imageView->hostText().regionCount())
                   .arg(m_docSearchPageMatchCount)
                   .arg(m_imageView->isImageMode()
                            ? QStringLiteral("image")
                            : (m_imageView->isGalleryMode() ? QStringLiteral("gallery")
                                                            : QStringLiteral("workspace")));
    }
    if (!text.trimmed().isEmpty() && statusBar()) {
        if (!PagePath::isPageRef(path)) {
            statusBar()->showMessage(
                tr("Find works on PDF / DjVu / EPUB pages"), 4000);
        } else if (!m_imageView->hostText().hasLayer()) {
            statusBar()->showMessage(
                tr("No extractable text on this page (scanned image?) — try File → Export Text"),
                5000);
        } else if (m_docSearchPageMatchCount == 0) {
            statusBar()->showMessage(
                tr("No matches on this page (%n text region(s))", "",
                   m_imageView->hostText().regionCount()),
                3000);
        }
    }
    scheduleDocumentSearch(text);
}

void MainWindow::updateSearchMatchLabel()
{
    if (!m_searchMatchLabel) {
        return;
    }
    if (!m_searchEdit || m_searchEdit->text().trimmed().isEmpty()) {
        m_searchMatchLabel->clear();
        if (m_searchPrevBtn) {
            m_searchPrevBtn->setEnabled(false);
        }
        if (m_searchNextBtn) {
            m_searchNextBtn->setEnabled(false);
        }
        return;
    }
    const int pageHits = m_docSearchPageMatchCount;
    const int pagesWith = m_docSearchHitPages.size();
    QString text;
    if (m_docSearchRunning) {
        text = tr("Searching…");
        if (pageHits > 0) {
            text += tr(" · %n on page", "", pageHits);
        }
    } else if (pagesWith > 0) {
        const int cur = m_docSearchHitIndex >= 0 ? m_docSearchHitIndex + 1 : 0;
        text = tr("%1/%2 pages").arg(cur).arg(pagesWith);
        if (pageHits > 0) {
            text += tr(" · %n on page", "", pageHits);
        }
    } else if (pageHits > 0) {
        text = tr("%n on page", "", pageHits);
    } else if (m_imageView && !m_imageView->hostText().hasLayer()
               && PagePath::isPageRef(m_imageView->hostImage().classicPath())) {
        text = tr("No text");
    } else if (m_imageView && !PagePath::isPageRef(m_imageView->hostImage().classicPath())) {
        text = tr("Not a document page");
    } else {
        text = tr("No matches");
    }
    m_searchMatchLabel->setText(text);
    const bool nav = pagesWith > 0 || pageHits > 0;
    if (m_searchPrevBtn) {
        m_searchPrevBtn->setEnabled(nav);
    }
    if (m_searchNextBtn) {
        m_searchNextBtn->setEnabled(nav);
    }
}

QStringList MainWindow::documentPagePathsForSearch() const
{
    QStringList out;
    if (!m_imageView) {
        return out;
    }
    QString path = m_imageView->hostImage().classicPath();
    if (path.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return out;
    }
    const QString doc = PagePath::documentFilePath(path);
    if (doc.isEmpty()) {
        return out;
    }
    // Prefer already-expanded session rows for this document (stable paths).
    for (const QString &p : m_session.paths()) {
        if (PagePath::isPageRef(p) && PagePath::documentFilePath(p) == doc) {
            out.append(p);
        }
    }
    if (out.isEmpty() && PagePath::isPageRef(path)) {
        out.append(path);
    }
    if (!out.isEmpty()) {
        return out;
    }
    // Fall back to expand helpers.
    const QString layout = PagePath::epubLayoutParamsOf(path);
    if (!layout.isEmpty() || path.contains(QLatin1String("//epub:"))) {
        return ThumtooCache::expandEpubToPageRefs(doc);
    }
    if (PagePath::isEpubFile(doc)) {
        return ThumtooCache::expandEpubToPageRefs(doc);
    }
    // PDF / DjVu page refs share makeRef form; try both expanders.
    QStringList pdf = ThumtooCache::expandPdfToPageRefs(doc);
    if (!pdf.isEmpty()) {
        return pdf;
    }
    return ThumtooCache::expandDjvuToPageRefs(doc);
}

void MainWindow::scheduleDocumentSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        m_docSearchGeneration.fetch_add(1);
        m_docSearchRunning = false;
        m_docSearchHitPages.clear();
        m_docSearchHitIndex = -1;
        m_docSearchQuery.clear();
        m_searchIndex.clear();
        if (m_thumbnailBar) {
            m_thumbnailBar->viewport()->update();
        }
        if (m_imageView && m_imageView->viewport()) {
            m_imageView->viewport()->update();
        }
        updateSearchMatchLabel();
        return;
    }
    if (!m_docSearchDebounce) {
        m_docSearchDebounce = new QTimer(this);
        m_docSearchDebounce->setSingleShot(true);
        connect(m_docSearchDebounce, &QTimer::timeout, this, [this]() {
            startDocumentSearch(m_docSearchQuery);
        });
    }
    m_docSearchQuery = trimmed;
    m_docSearchDebounce->start(280);
}

void MainWindow::startDocumentSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QStringList pages = documentPagePathsForSearch();
    if (pages.isEmpty()) {
        // Single-page / non-document: page-local search only.
        m_docSearchRunning = false;
        m_docSearchHitPages.clear();
        updateSearchMatchLabel();
        return;
    }
    const quint64 gen = m_docSearchGeneration.fetch_add(1) + 1;
    m_docSearchRunning = true;
    updateSearchMatchLabel();

    const bool fuzzy = !m_searchFuzzyCheck || m_searchFuzzyCheck->isChecked();
    TextLayerResolve::Prefer prefer = TextLayerResolve::Prefer::Auto;
    if (m_searchSourceCombo) {
        prefer = static_cast<TextLayerResolve::Prefer>(
            m_searchSourceCombo->currentData().toInt());
    } else if (m_imageView) {
        prefer = m_imageView->hostText().layerPrefer();
    }
    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, pages, trimmed, fuzzy, gen, prefer]() {
        QVector<QPair<QString, int>> pathHits;
        pathHits.reserve(64);
        QVector<int> hitPages;
        hitPages.reserve(64);
        for (const QString &pagePath : pages) {
            if (!guard) {
                return;
            }
            const ThumtooCache::PageTextLayer layer =
                TextLayerResolve::load(pagePath, prefer);
            QVector<QString> texts;
            QVector<QRectF> bboxes;
            QVector<int> blockIds;
            texts.reserve(layer.regions.size());
            bboxes.reserve(layer.regions.size());
            blockIds.reserve(layer.regions.size());
            for (const ThumtooCache::TextRegion &r : layer.regions) {
                texts.append(r.text);
                bboxes.append(r.bbox);
                blockIds.append(r.blockId);
            }
            const int matches = TextSearchPolicy::findHits(
                texts, bboxes, trimmed, fuzzy, blockIds).size();
            if (matches > 0) {
                pathHits.append(qMakePair(pagePath, matches));
                const int page = PagePath::pageNumber(pagePath);
                if (page > 0) {
                    hitPages.append(page);
                }
            }
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, gen, trimmed, hitPages, pathHits]() {
                MainWindow *host = guard.data();
                if (!host || gen != host->m_docSearchGeneration.load()) {
                    return;
                }
                host->onDocumentSearchFinished(gen, trimmed, hitPages, pathHits);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::onDocumentSearchFinished(quint64 generation, const QString &query,
                                          const QVector<int> &hitPages,
                                          const QVector<QPair<QString, int>> &pathMatchCounts)
{
    if (generation != m_docSearchGeneration.load()) {
        return;
    }
    m_docSearchRunning = false;
    m_docSearchQuery = query;
    m_docSearchHitPages = hitPages;

    // Map document paths → SessionImageId tags for filmstrip / gallery chrome.
    {
        const quint64 g = m_searchIndex.beginQuery(query);
        QVector<SessionSearchIndex::Hit> hits;
        hits.reserve(pathMatchCounts.size());
        for (const auto &pair : pathMatchCounts) {
            SessionSearchIndex::Hit h;
            h.path = pair.first;
            h.matchCount = quint16(qMin(65535, pair.second));
            h.id = m_session.firstIdForPath(pair.first);
            if (h.id == kInvalidSessionImageId) {
                // Prefer id-aligned index for duplicate-safe membership.
                const int idx = m_session.indexOfPathPreferId(pair.first);
                if (idx >= 0) {
                    h.id = m_session.idAt(idx);
                }
            }
            hits.append(h);
        }
        m_searchIndex.commit(g, query, hits);
        if (m_thumbnailBar) {
            m_thumbnailBar->viewport()->update();
        }
        if (m_imageView && m_imageView->viewport()) {
            m_imageView->viewport()->update();
        }
    }
    // Point at the hit for the current page when possible.
    m_docSearchHitIndex = -1;
    if (m_imageView && !hitPages.isEmpty()) {
        const int cur = PagePath::pageNumber(m_imageView->hostImage().classicPath());
        for (int i = 0; i < hitPages.size(); ++i) {
            if (hitPages.at(i) == cur) {
                m_docSearchHitIndex = i;
                break;
            }
        }
        if (m_docSearchHitIndex < 0) {
            m_docSearchHitIndex = 0;
        }
    }
    if (m_imageView) {
        m_docSearchPageMatchCount = m_imageView->hostTextLayer().matchCount();
    }
    updateSearchMatchLabel();
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_FIND")) {
        qWarning().noquote()
            << QStringLiteral("[find] docScan done query=%1 pagesWithHits=%2 pageMatches=%3")
                   .arg(query)
                   .arg(hitPages.size())
                   .arg(m_docSearchPageMatchCount);
    }
    if (statusBar()) {
        if (hitPages.isEmpty()) {
            statusBar()->showMessage(tr("No matches in document"), 3000);
        } else {
            statusBar()->showMessage(
                tr("%n page(s) with matches", "", hitPages.size()), 3000);
        }
    }
}

void MainWindow::goToSearchHit(int index)
{
    if (index < 0 || index >= m_docSearchHitPages.size()) {
        return;
    }
    m_docSearchHitIndex = index;
    const int page = m_docSearchHitPages.at(index);
    navigateDocumentPage(page);
    // Re-apply query so highlights load for the new page.
    if (m_imageView && !m_docSearchQuery.isEmpty()) {
        m_docSearchPageMatchCount = m_imageView->hostText().setSearchQuery(m_docSearchQuery);
    }
    updateSearchMatchLabel();
}

void MainWindow::findNextMatch()
{
    if (m_docSearchHitPages.isEmpty()) {
        // Only page-local matches — nothing to navigate.
        if (m_docSearchPageMatchCount > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("%n match(es) on this page", "", m_docSearchPageMatchCount), 2000);
        }
        return;
    }
    int next = m_docSearchHitIndex + 1;
    if (next >= m_docSearchHitPages.size()) {
        next = 0;
    }
    goToSearchHit(next);
}

void MainWindow::findPreviousMatch()
{
    if (m_docSearchHitPages.isEmpty()) {
        return;
    }
    int prev = m_docSearchHitIndex - 1;
    if (prev < 0) {
        prev = m_docSearchHitPages.size() - 1;
    }
    goToSearchHit(prev);
}



void MainWindow::toggleSmoothScaling()
{
    const bool on = m_smoothScalingAct && m_smoothScalingAct->isChecked();
    DisplayQuality::setSmoothScaling(on);
    if (!m_imageView) {
        return;
    }
    m_imageView->setRenderHint(QPainter::SmoothPixmapTransform, on);
    if (QGraphicsScene *sc = m_imageView->scene()) {
        const auto mode = on ? Qt::SmoothTransformation : Qt::FastTransformation;
        for (QGraphicsItem *gi : sc->items()) {
            if (auto *item = dynamic_cast<ImageItem *>(gi)) {
                item->setTransformationMode(mode);
            }
        }
        sc->update();
    }
    if (m_imageView->viewport()) {
        m_imageView->viewport()->update();
    }
}

void MainWindow::toggleHud()
{
    const bool on = m_toggleHudAct->isChecked();
    m_imageView->hostHud().setVisible(on, [v = m_imageView, on = bool(on)]() {
            v->hostSlideshow().syncProgressTimerWithHud(on);
            if (v->viewport()) v->viewport()->update();
        });
}

void MainWindow::toggleThumbnailLabels()
{
    if (!m_thumbnailBar) {
        return;
    }
    m_thumbnailBar->setLabelsVisible(!m_hideThumbLabelsAct->isChecked());
}


void MainWindow::toggleAttentionMode()
{
    if (!m_imageView || !m_attentionAct) {
        return;
    }
    const bool want = m_attentionAct->isChecked();
    if (want && m_imageView->hostCrop().active()) {
        m_imageView->hostCrop().cancelCrop();
        if (m_cropAct) {
            m_cropAct->setChecked(false);
        }
    }
    if (want && !m_imageView->isImageMode()) {
        // Attention edit is Image-mode only for now.
        if (m_session.paths().isEmpty()) {
            m_attentionAct->setChecked(false);
            return;
        }
        int idx = m_currentIndex;
        if (idx < 0 || idx >= m_session.paths().size()) {
            idx = 0;
        }
        openSessionIndexInImageMode(idx);
    }
    m_imageView->hostAttention().setAttentionMode(want);
    m_attentionAct->setChecked(m_imageView->hostAttention().active());
}

void MainWindow::toggleThumbnailCrop()
{
    if (!m_thumbnailBar || !m_cropThumbnailsAct) {
        return;
    }
    m_thumbnailBar->setCropToSquare(m_cropThumbnailsAct->isChecked());
}


void MainWindow::raiseSelected()
{
    m_imageView->hostWorkspace().raiseSelected();
}

void MainWindow::lowerSelected()
{
    m_imageView->hostWorkspace().lowerSelected();
}

void MainWindow::opacityUp()
{
    m_imageView->hostWorkspace().opacityUp();
}

void MainWindow::opacityDown()
{
    m_imageView->hostWorkspace().opacityDown();
}

void MainWindow::opacityReset()
{
    m_imageView->hostWorkspace().opacityReset();
}

void MainWindow::resetItemScale()
{
    m_imageView->hostWorkspace().resetItemScale();
}

void MainWindow::resetItemRotation()
{
    m_imageView->hostWorkspace().resetItemRotation();
}

void MainWindow::resetItemShear()
{
    if (m_imageView) {
        m_imageView->hostWorkspace().resetItemShear();
    }
}

QList<int> MainWindow::sessionSelectionIndices() const
{
    // Union filmstrip + canvas selection (session order). A single current row
    // on the strip must not hide a larger Gallery/Workspace multi-select.
    QSet<int> set;
    if (m_thumbnailBar) {
        for (int idx : m_thumbnailBar->selectedIndices()) {
            if (idx >= 0 && idx < m_session.size()) {
                set.insert(idx);
            }
        }
    }
    if (m_imageView && (isGalleryMode() || isWorkspaceMode())) {
        for (int idx : m_imageView->hostWorkspace().selectedSessionIndices()) {
            if (idx >= 0 && idx < m_session.size()) {
                set.insert(idx);
            }
        }
    }
    if (set.isEmpty() && m_currentIndex >= 0
        && m_currentIndex < m_session.paths().size()) {
        set.insert(m_currentIndex);
    }
    QList<int> indices = set.values();
    std::sort(indices.begin(), indices.end());
    return indices;
}

QStringList MainWindow::pathsFromUiSelection() const
{
    QStringList paths;
    for (int idx : sessionSelectionIndices()) {
        if (idx >= 0 && idx < m_session.paths().size()) {
            paths.append(m_session.paths().at(idx));
        }
    }
    return paths;
}

QList<SessionEntrySnapshot> MainWindow::sessionSelectionSnapshots() const
{
    QList<SessionEntrySnapshot> out;
    for (int idx : sessionSelectionIndices()) {
        if (idx < 0 || idx >= m_session.paths().size()) {
            continue;
        }
        SessionEntrySnapshot snap;
        snap.index = idx;
        snap.path = m_session.pathAt(idx);
        snap.id = m_session.idAt(idx);
        if (m_imageView && snap.id != kInvalidSessionImageId) {
            // Transfer content appearance only (crop / bake / colour / attention).
            // hasDurableAppearance also includes Workspace placement — pose-only
            // must not set hasAppearance and skip freezeItemAppearance (that was
            // the Open Selection "lost crop" path when sparse lag or draft applied).
            const bool durableContent =
                m_imageView->itemWorld().hasContentEditComponents(snap.id)
                || m_imageView->itemWorld().hasAttention(snap.id);
            if (durableContent) {
                snap.appearance = m_imageView->sessionAppearanceValue(snap.id);
                snap.hasAppearance =
                    SessionAppearance::hasContentAppearance(snap.appearance)
                    || snap.appearance.hasCrop
                    || snap.appearance.hasAttention
                    || !snap.appearance.attentionPoints.isEmpty();
            }
            if (!snap.hasAppearance) {
                ImageItem *item = m_imageView->findItemBySessionId(snap.id);
                if (!item && isImageMode()) {
                    item = m_imageView->primaryItem();
                    if (item && item->sessionId() != snap.id) {
                        item = nullptr;
                    }
                }
                if (item) {
                    snap.appearance = m_imageView->freezeItemAppearance(item);
                    snap.hasAppearance =
                        SessionAppearance::hasContentAppearance(snap.appearance)
                        || snap.appearance.hasCrop
                        || snap.appearance.hasAttention
                        || !snap.appearance.attentionPoints.isEmpty();
                }
            }
            if (snap.hasAppearance) {
                snap.appearance.sessionId = snap.id;
                if (snap.appearance.path.isEmpty()) {
                    snap.appearance.path = snap.path;
                }
                // Drop Workspace placement — new window opens in Gallery/Image.
                snap.appearance.pos = QPointF();
                snap.appearance.scale = 1.0;
                snap.appearance.scaleY = 1.0;
                snap.appearance.shear = 0.0;
                snap.appearance.rotation = 0.0;
                snap.appearance.opacity = 1.0;
                snap.appearance.z = 0.0;
            }
        }
        out.append(snap);
    }
    return out;
}

void MainWindow::loadSessionSnapshots(const QList<SessionEntrySnapshot> &entries, int startAt)
{
    if (entries.isEmpty()) {
        return;
    }
    stopSlideshow();
    SessionOpen::beginReplace(m_imageView, m_thumbnailBar);
    ++m_expandGeneration;
    setExpandProgressBusy(false);

    // Drop empty paths; keep appearance parallel via filtered list.
    QList<SessionEntrySnapshot> cleaned;
    cleaned.reserve(entries.size());
    for (const SessionEntrySnapshot &e : entries) {
        if (e.path.isEmpty()) {
            continue;
        }
        cleaned.append(e);
    }
    if (cleaned.isEmpty()) {
        return;
    }

    QStringList paths;
    paths.reserve(cleaned.size());
    for (const SessionEntrySnapshot &e : cleaned) {
        paths.append(e.path);
    }
    // setPaths allocates fresh SessionImageIds (do not reuse source ids).
    m_session.setPaths(paths);
    if (m_imageView) {
        m_imageView->itemWorld().clearAppearance();
    }
    m_session.validateUniqueIds("loadSessionSnapshots");

    // Install content appearance *before* finishApplyExpandedLoad so Gallery
    // pack / contentLayoutSize see crop aspect on first layout.
    const auto installTransferredAppearance = [this, cleaned]() {
        if (!m_imageView) {
            return;
        }
        const int n = qMin(cleaned.size(), m_session.size());
        for (int i = 0; i < n; ++i) {
            if (!cleaned.at(i).hasAppearance) {
                continue;
            }
            WorkspaceItemState st = cleaned.at(i).appearance;
            st.sessionId = m_session.idAt(i);
            st.path = m_session.pathAt(i);
            st.sessionIndex = i;
            // Workspace pose already cleared in sessionSelectionSnapshots.
            // Path-XDG seed skips when hasContentAppearance is already set
            // (applyStoredContentAppearanceSeed); no private seed-book API needed.
            m_imageView->itemWorld().setAppearance(st.sessionId, st);
        }
    };
    installTransferredAppearance();

    // Preserve selection order — do not run sortFileListSync.
    finishApplyExpandedLoad(startAt);

    // Re-apply after open barriers: prepareExpandedSession may seed path-XDG
    // orient asynchronously for large sessions; content ops (crop) already win
    // inside applyStoredContentAppearanceSeed, but re-install keeps attention
    // and grade authoritative for the transferred snapshots.
    installTransferredAppearance();

    if (cleaned.size() > 1) {
        setWindowTitle(tr("Biltoo — %n image(s)", "", cleaned.size()));
    }

    // Filmstrip may install on a later event-loop turn (warm multi-open) or
    // after size resolve (cold). Select all transferred rows once the strip exists.
    const auto selectTransferred = [this]() {
        if (!m_thumbnailBar || m_session.isEmpty()) {
            return;
        }
        if (isGalleryMode() || isWorkspaceMode()) {
            m_thumbnailBar->setMultiSelectEnabled(true);
        }
        QList<int> all;
        all.reserve(m_session.size());
        for (int i = 0; i < m_session.size(); ++i) {
            all.append(i);
        }
        m_thumbnailBar->setSelectedIndices(all);
        if (m_imageView && (isGalleryMode() || isWorkspaceMode())) {
            m_imageView->hostWorkspace().selectBySessionIndices(all);
        }
    };
    if (m_imageView && m_imageView->hostGallerySizeResolve().active()) {
        connect(m_imageView, &ImageView::gallerySizeResolveFinished, this,
                selectTransferred,
                static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
    } else {
        QTimer::singleShot(0, this, selectTransferred);
    }
}

void MainWindow::openSelectionInNewWindow()
{
    const QList<SessionEntrySnapshot> entries = sessionSelectionSnapshots();
    if (entries.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Nothing selected to open in a new window."), 3000);
        }
        return;
    }
    const int n = entries.size();
    int withAppearance = 0;
    for (const SessionEntrySnapshot &e : entries) {
        if (e.hasAppearance) {
            ++withAppearance;
        }
    }
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (isVisible()) {
        window->move(frameGeometry().topLeft() + QPoint(32, 32));
    }
    window->show();
    window->loadSessionSnapshots(entries);
    if (statusBar()) {
        if (withAppearance > 0) {
            statusBar()->showMessage(
                tr("Opened %n image(s) in a new window (%1 with appearance).",
                   "", n).arg(withAppearance),
                4000);
        } else {
            statusBar()->showMessage(
                tr("Opened %n image(s) in a new window.", "", n), 4000);
        }
    }
}

void MainWindow::newWindow()
{
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    // Offset so the new frame is not fully stacked under this one.
    if (isVisible()) {
        window->move(frameGeometry().topLeft() + QPoint(32, 32));
    }
    window->show();
}

void MainWindow::toggleWorkspaceMode()
{
    const bool on = m_workspaceModeAct->isChecked();
    if (on) {
        stopSlideshow();
        m_galleryReturnActive = false;
        m_workspaceReturnActive = false;
        if (m_backToGalleryAct) {
            m_backToGalleryAct->setEnabled(false);
        }
        m_thumbnailBar->setMultiSelectEnabled(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        // Workspace opens with nothing selected (filmstrip + canvas). Residual
        // Image/Gallery selection used to stay selected so a one-thumb drag
        // shipped every selected path via selectedItems().
        if (m_imageView->canvasScene()) {
            m_imageView->canvasScene()->clearSelection();
        }
        m_thumbnailBar->selectNoneThumbs();
        // Workspace starts/stays empty unless the user (or a project) places tiles.
        // Do not seed from the session list — arrangement is permanent across modes.
        syncThumbnailCanvasMembership();
        // Thumbnail visibility for Workspace is applied in updateWorkspaceActionVisibility
        // via updateThumbnailBarForMode (default preferred on).
        // Uncheck gallery layout actions
        for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                             m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                             m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct,
                             m_layoutFlowAct, m_layoutFlowFillAct, m_layoutFacingAct}) {
            if (act) {
                act->setChecked(false);
            }
        }
    } else {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_imageView->setViewMode(ImageView::ViewMode::Image);
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostDisplayPipeline().loadImage(m_session.paths().at(m_currentIndex));
            m_thumbnailBar->setCurrentIndex(m_currentIndex);
        }
    }
    updateWorkspaceActionVisibility();
}

void MainWindow::setSelectTool()
{
    if (m_imageView && m_imageView->hostCrop().active()) {
        // Leaving crop via another tool commits the draft (same as toolbar off).
        m_imageView->hostCrop().setCropMode(false);
        if (m_cropAct) {
            m_cropAct->setChecked(false);
        }
    }
    m_imageView->setTool(ImageView::Tool::Select);
    m_selectToolAct->setChecked(true);
}

void MainWindow::setPanTool()
{
    if (m_imageView && m_imageView->hostCrop().active()) {
        m_imageView->hostCrop().setCropMode(false);
        if (m_cropAct) {
            m_cropAct->setChecked(false);
        }
    }
    m_imageView->setTool(ImageView::Tool::Pan);
    m_panToolAct->setChecked(true);
}

void MainWindow::setZoomTool()
{
    if (m_imageView && m_imageView->hostCrop().active()) {
        m_imageView->hostCrop().setCropMode(false);
        if (m_cropAct) {
            m_cropAct->setChecked(false);
        }
    }
    m_imageView->setTool(ImageView::Tool::Zoom);
    if (m_zoomToolAct) {
        m_zoomToolAct->setChecked(true);
    }
}


void MainWindow::showSlideshowSettings()
{
    SlideshowSettingsDialog dlg(this);
    dlg.setIntervalMs(m_slideshowIntervalMs);
    dlg.setStartFullscreen(m_slideshowFullscreen);
    dlg.setLoop(m_slideshowLoop);
    if (m_imageView) {
        dlg.setTransitionIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentTransition()));
        dlg.setTransitionDurationMs(m_imageView->hostSlideshow().settings().transitionDuration());
        dlg.setMotionIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentMotion()));
        dlg.setPanZoomFactor(m_imageView->hostSlideshow().settings().currentPanZoomFactor());
        dlg.setZoomIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentZoom()));
        dlg.setLetterboxFillIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentLetterboxFill()));
        dlg.setPadColor(m_imageView->hostSlideshow().padColorForPaint());
        // Solid mode stores its own colour; surface current effective pad for UI.
        if (m_imageView->hostSlideshow().settings().currentLetterboxFill()
            == SlideshowLetterboxFill::Solid) {
            // pad from setter path uses dedicated colour via slideshowPadColor
        }
    }
    // Live apply — no OK; Close dismisses. settingsChanged fires on each edit.
    auto applyFromDialog = [this, &dlg]() {
        setSlideshowIntervalMs(dlg.intervalMs());
        m_slideshowFullscreen = dlg.startFullscreen();
        m_slideshowLoop = dlg.loop();
        if (!m_imageView) {
            return;
        }
        m_imageView->hostSlideshow().setSlideshowTransition(
            static_cast<SlideshowTransition>(dlg.transitionIndex()));
        // Cap already enforced by the dialog max; clamp again for safety.
        // Duration is the full transition (out + in), so cap at the interval.
        const int intervalCap = m_slideshowIntervalMs;
        m_imageView->hostSlideshow().setSlideshowTransitionDurationMs(
            SlideshowClocks::clampTransitionMs(dlg.transitionDurationMs(), intervalCap));
        m_imageView->hostSlideshow().setSlideshowMotion(
            static_cast<SlideshowMotion>(dlg.motionIndex()));
        m_imageView->hostSlideshow().setPanZoomFactor(dlg.panZoomFactor());
        m_imageView->hostSlideshow().setSlideshowZoom(
            static_cast<SlideshowZoom>(SlideshowClocks::clampZoomIndex(dlg.zoomIndex())));
        m_imageView->hostSlideshow().setSlideshowPadColor(dlg.padColor());
        m_imageView->hostSlideshow().setSlideshowLetterboxFill(
            static_cast<SlideshowLetterboxFill>(
                SlideshowClocks::clampLetterboxFillIndex(dlg.letterboxFillIndex())));
        // Interval changes remap phase inside setSlideshowIntervalMs. Other live
        // settings must not re-arm the clock (that restarted the dwell at 0 and
        // felt like a long pause on the current image).
        if (m_slideshowClockRunning && !m_slideshowPaused) {
            m_imageView->hostSlideshow().cancelSlideshowTransition();
            m_slideshowPendingToIndex = -1;
            m_slideshowPreloadToIdx = -1;
            m_slideshowTransitionCycle = -1;
            m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
            m_imageView->hostSlideshow().reapplySlideshowFraming();
            updateSlideshowFromClock();
        }
        writeSettings();
    };
    connect(&dlg, &SlideshowSettingsDialog::settingsChanged, this, applyFromDialog);
    dlg.exec();
}

void MainWindow::remapSlideshowPhase(int oldIntervalMs, int newIntervalMs)
{
    // Position is unitless [slide + phase]. Interval only affects how fast
    // wall time advances m_slideshowPosition — no absolute-ms remap.
    Q_UNUSED(oldIntervalMs);
    Q_UNUSED(newIntervalMs);
    m_slideshowTransitionCycle = -1;
    if (!m_slideshowPaused && m_slideshowClockRunning) {
        // Restart rate sample so the next tick does not apply a large dt under
        // the new interval after a long settings dialog pause.
        m_slideshowClock.start();
    }
}

void MainWindow::armSlideshowAdvanceTimer()
{
    // Restart the pure clock at the current session index (start / resume /
    // ←/→ navigation). Interval *edits* use remapSlideshowPhase instead so the
    // dwell is not reset to zero.
    if (m_slideshowPaused || !m_slideshowTimer) {
        return;
    }
    if (m_session.paths().isEmpty() || isWorkspaceMode()) {
        return;
    }

    m_slideshowBaseIndex = SlideshowClocks::clampPathIndex(
        m_currentIndex, m_session.paths().size());
    m_slideshowPausedAccumMs = 0;
    m_slideshowPosition = 0.0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    m_slideshowClock.start();
    m_slideshowClockRunning = true;

    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        m_imageView->hostSlideshow().reapplySlideshowFraming();
        if (m_session.paths().size() > 1) {
            int next = (m_currentIndex + 1) % m_session.paths().size();
            if (next < 0) {
                next = 0;
            }
            m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at(next));
        }
    }

    if (!m_slideshowTimer->isActive()) {
        m_slideshowTimer->start();
    }
    updateSlideshowFromClock();
}

void MainWindow::updateSlideshowFromClock()
{
    // ------------------------------------------------------------------
    // Unitless position model:
    //   m_slideshowPosition = cycles since arm + phase in [0,1)
    //   wall Δt advances position by Δt / intervalMs
    // Interval / speed changes only alter the rate — position is unchanged.
    // ------------------------------------------------------------------
    if (m_slideshowPaused || !m_slideshowClockRunning || !m_slideshowTimer) {
        return;
    }
    const int n = m_session.paths().size();
    if (n < 1 || isWorkspaceMode()) {
        return;
    }

    const int intervalMs = SlideshowClocks::clampIntervalMs(m_slideshowIntervalMs);
    int transitionMs = m_imageView ? m_imageView->hostSlideshow().settings().transitionDuration() : 0;
    transitionMs = SlideshowClocks::clampTransitionMs(transitionMs, intervalMs);
    const int pureMs = intervalMs - transitionMs;
    const qreal pureFrac = SlideshowClocks::pureFrac(pureMs, intervalMs);

    // Integrate wall time into unitless position (rate = 1/interval).
    qint64 wallDelta = 0;
    if (m_slideshowClock.isValid()) {
        wallDelta = m_slideshowClock.restart();
    } else {
        m_slideshowClock.start();
    }
    if (wallDelta < 0) {
        wallDelta = 0;
    }
    // Suspend / debug stall: do not skip many slides in one tick.
    if (wallDelta > qint64(intervalMs) * 3) {
        wallDelta = intervalMs;
    }
    m_slideshowPosition += qreal(wallDelta) / qreal(intervalMs);

    const qreal cycleF = qFloor(m_slideshowPosition);
    const qreal phaseT = m_slideshowPosition - cycleF; // [0,1)
    const qint64 cycle = qint64(cycleF);
    const qint64 absFrom = qint64(m_slideshowBaseIndex) + cycle;

    // Non-loop: stop after the last image's pure dwell (no wrap transition).
    if (!m_slideshowLoop && absFrom >= n) {
        if (m_currentIndex != n - 1 && !m_slideshowAdvancing) {
            m_slideshowAdvancing = true;
            setCurrentIndex(n - 1);
            m_slideshowAdvancing = false;
        }
        if (m_imageView) {
            m_imageView->hostSlideshow().setSlideshowPhase(
                m_session.paths().at(n - 1), QString(), -1.0);
            m_imageView->hostSlideshow().cancelSlideshowTransition();
        }
        m_slideshowPendingToIndex = -1;
        m_slideshowTransitionCycle = -1;
        m_slideshowPreloadToIdx = -1;
        stopSlideshow();
        return;
    }

    const int fromIdx = m_slideshowLoop
                            ? int(absFrom % n)
                            : SlideshowClocks::clampPathIndex(int(absFrom), n);
    // Single-image or non-loop last slide: never crossfade into a wrap target.
    const bool allowTransition =
        (n > 1) && (m_slideshowLoop || fromIdx < n - 1);
    const int toIdx = allowTransition ? ((fromIdx + 1) % n) : fromIdx;

    // Session timeline HUD (video-player style) in ms for display only.
    if (m_imageView) {
        qint64 totalMs = 0;
        qint64 elapsedMs = 0;
        if (m_slideshowLoop) {
            const qreal sessionPos =
                std::fmod(qreal(m_slideshowBaseIndex) + m_slideshowPosition, qreal(n));
            const qreal sessionPosPos =
                sessionPos < 0.0 ? sessionPos + qreal(n) : sessionPos;
            totalMs = qint64(n) * qint64(intervalMs);
            elapsedMs =
                totalMs > 0 ? qint64(sessionPosPos * qreal(intervalMs)) % totalMs : 0;
        } else {
            totalMs = qint64(n) * qint64(intervalMs);
            const qreal pos = qMin(qreal(m_slideshowBaseIndex) + m_slideshowPosition,
                                   qreal(n));
            elapsedMs = qMin(totalMs, qint64(pos * qreal(intervalMs)));
        }
        m_imageView->hostSlideshow().setSlideshowTimeline(elapsedMs, totalMs);
        // Per-cycle dwell fraction for the thin progress line.
        m_imageView->hostSlideshow().setSlideshowCycleProgress(phaseT);
    }

    // Look-ahead once per toIdx — not every 16ms clock tick (was a log/CPU storm
    // when PreferCache plateaued below want and ensure stayed a no-op).
    if (m_imageView && n > 1 && toIdx != m_slideshowPreloadToIdx) {
        m_slideshowPreloadToIdx = toIdx;
        m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at(toIdx));
        m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at((toIdx + 1) % n));
        m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at((toIdx + 2) % n));
        m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at((toIdx + 3) % n));
    }

    const QString fromPath = m_session.paths().at(fromIdx);
    const QString toPath = m_session.paths().at(toIdx);

    if (phaseT < pureFrac || transitionMs <= 0 || !allowTransition) {
        if (m_imageView) {
            // Clear any leftover to-side buffer so dwell is not dual-blended.
            m_imageView->hostSlideshow().setSlideshowPhase(fromPath, QString(), -1.0);
        }
        if (m_currentIndex != fromIdx && !m_slideshowAdvancing) {
            m_slideshowAdvancing = true;
            setCurrentIndex(fromIdx);
            m_slideshowAdvancing = false;
        }
        m_slideshowPendingToIndex = -1;
        m_slideshowTransitionCycle = -1;
        // Non-loop last slide: end when the pure dwell finishes.
        if (!m_slideshowLoop && fromIdx >= n - 1
            && phaseT >= pureFrac && transitionMs > 0) {
            stopSlideshow();
        } else if (!m_slideshowLoop && fromIdx >= n - 1
                   && transitionMs <= 0 && phaseT >= 1.0 - 1e-6) {
            stopSlideshow();
        }
        return;
    }

    const qreal t = SlideshowClocks::transitionBlendT(phaseT, pureFrac);
    if (m_imageView) {
        if (m_slideshowTransitionCycle != cycle) {
            qCDebug(lcSlideshow).nospace()
                << "[slideshow] phase-fade cycle=" << cycle
                << " phaseT=" << QString::number(phaseT, 'f', 3)
                << " t=" << QString::number(t, 'f', 3)
                << " from=" << fromIdx
                << " to=" << toIdx
                << " path=" << QFileInfo(toPath).fileName();
            // New transition cycle: reset fade target so armSlideshowToPhase
            // runs cleanly (avoids stale buffers when wrapping last→first).
            m_slideshowTransitionCycle = cycle;
            m_slideshowPendingToIndex = toIdx;
        }
        m_imageView->hostSlideshow().setSlideshowPhase(fromPath, toPath, t);
        const bool transitionDone = (t >= 1.0 - 1e-6);
        if (transitionDone && m_currentIndex != toIdx && !m_slideshowAdvancing) {
            m_slideshowAdvancing = true;
            setCurrentIndex(toIdx);
            m_slideshowAdvancing = false;
            m_slideshowPendingToIndex = -1;
            m_slideshowPreloadToIdx = -1;
            // After commit, force pure phase on the new current path so the
            // next tick does not keep a finished fade pair on screen.
            if (t >= 1.0 - 1e-9 && phaseT >= 1.0 - 1e-6) {
                m_imageView->hostSlideshow().setSlideshowPhase(toPath, QString(), -1.0);
            }
        }
    }
}

void MainWindow::onSlideshowTick()
{
    updateSlideshowFromClock();
}

void MainWindow::toggleToolBar()
{
    const bool visible = m_toggleToolBarAct->isChecked();
    m_toolBar->setVisible(visible);
    if (!isFullScreen()) {
        m_toolBarVisibleBeforeFullscreen = visible;
    }
}

void MainWindow::toggleThumbnailBar()
{
    const bool visible = m_toggleThumbnailBarAct->isChecked();
    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(visible);
    } else if (m_thumbnailBar) {
        m_thumbnailBar->setVisible(visible);
    }
    // Remember preference for the mode the user is currently in so Gallery and
    // Workspace stay independent. Image mode still uses the force flags so
    // applyThumbnailVisibility keeps working after load/sort.
    if (m_imageView && m_imageView->isGalleryMode()) {
        m_thumbnailsPreferredGallery = visible;
        m_forceThumbnails = false;
        m_forceNoThumbnails = false;
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        m_thumbnailsPreferredWorkspace = visible;
        m_forceThumbnails = false;
        m_forceNoThumbnails = false;
    } else {
        m_forceNoThumbnails = !visible;
        m_forceThumbnails = visible;
    }
    if (!isFullScreen()) {
        m_thumbnailBarVisibleBeforeFullscreen = visible;
    }
}

void MainWindow::setThumbnailBarPosition(ThumbnailEdge edge)
{
    if (!m_thumbnailBar || !m_thumbnailDock) {
        m_thumbnailEdge = edge;
        return;
    }
    m_thumbnailEdge = edge;
    const bool horizontalBar =
        (edge == ThumbnailEdge::Bottom || edge == ThumbnailEdge::Top);
    const Qt::Orientation barOrientation =
        horizontalBar ? Qt::Horizontal : Qt::Vertical;

    m_dockLocationGuard = true;
    m_thumbnailBar->setBarOrientation(barOrientation);

    Qt::DockWidgetArea area = Qt::BottomDockWidgetArea;
    switch (edge) {
    case ThumbnailEdge::Top:
        area = Qt::TopDockWidgetArea;
        break;
    case ThumbnailEdge::Left:
        area = Qt::LeftDockWidgetArea;
        break;
    case ThumbnailEdge::Right:
        area = Qt::RightDockWidgetArea;
        break;
    case ThumbnailEdge::Bottom:
    default:
        area = Qt::BottomDockWidgetArea;
        break;
    }
    addDockWidget(area, m_thumbnailDock);

    const int thumbSize = m_thumbnailBar->thumbSize();
    const int barExtent = ThumbnailBar::extentForThumbSize(thumbSize);
    // Let the user resize the dock; seed a sensible default extent.
    if (horizontalBar) {
        m_thumbnailBar->setMinimumHeight(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMinThumbSize));
        m_thumbnailBar->setMaximumHeight(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMaxThumbSize));
        m_thumbnailBar->setMinimumWidth(0);
        m_thumbnailBar->setMaximumWidth(QWIDGETSIZE_MAX);
        resizeDocks({m_thumbnailDock}, {barExtent}, Qt::Vertical);
    } else {
        m_thumbnailBar->setMinimumWidth(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMinThumbSize));
        m_thumbnailBar->setMaximumWidth(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMaxThumbSize));
        m_thumbnailBar->setMinimumHeight(0);
        m_thumbnailBar->setMaximumHeight(QWIDGETSIZE_MAX);
        resizeDocks({m_thumbnailDock}, {barExtent}, Qt::Horizontal);
    }
    m_thumbnailBar->setThumbSize(thumbSize);
    m_dockLocationGuard = false;
    updateThumbnailEdgeActions();
}

void MainWindow::onThumbnailDockLocationChanged(Qt::DockWidgetArea area)
{
    if (m_dockLocationGuard || !m_thumbnailBar) {
        return;
    }
    ThumbnailEdge edge = m_thumbnailEdge;
    switch (area) {
    case Qt::LeftDockWidgetArea:
        edge = ThumbnailEdge::Left;
        break;
    case Qt::RightDockWidgetArea:
        edge = ThumbnailEdge::Right;
        break;
    case Qt::TopDockWidgetArea:
        edge = ThumbnailEdge::Top;
        break;
    case Qt::BottomDockWidgetArea:
        edge = ThumbnailEdge::Bottom;
        break;
    default:
        // Floating: keep last edge / orientation.
        return;
    }
    if (edge == m_thumbnailEdge) {
        return;
    }
    m_thumbnailEdge = edge;
    const bool horizontalBar =
        (edge == ThumbnailEdge::Bottom || edge == ThumbnailEdge::Top);
    m_thumbnailBar->setBarOrientation(horizontalBar ? Qt::Horizontal : Qt::Vertical);
    updateThumbnailEdgeActions();
}

void MainWindow::updateThumbnailEdgeActions()
{
}


void MainWindow::toggleScrollBars()
{
    // Honour mode (slideshow forces off).
    updateScrollBarPolicyForMode();
}

void MainWindow::showKeyboardShortcuts()
{
    // Collect actions that expose shortcuts, with menu-derived categories.
    QList<QAction *> actions;
    QList<QString> categories;
    QSet<QAction *> seen;

    auto addAct = [&](QAction *act, const QString &category) {
        if (!act || act->isSeparator() || seen.contains(act)) {
            return;
        }
        if (act->shortcuts().isEmpty() && act->shortcut().isEmpty()) {
            return;
        }
        seen.insert(act);
        actions.append(act);
        categories.append(category);
    };

    std::function<void(QMenu *, const QString &)> walkMenu;
    walkMenu = [&](QMenu *menu, const QString &category) {
        if (!menu) {
            return;
        }
        for (QAction *a : menu->actions()) {
            if (!a) {
                continue;
            }
            if (a->menu()) {
                QString sub = a->text();
                sub.remove(QLatin1Char('&'));
                walkMenu(a->menu(), sub.isEmpty() ? category : sub);
            } else {
                addAct(a, category);
            }
        }
    };

    if (menuBar()) {
        for (QAction *top : menuBar()->actions()) {
            if (!top || !top->menu()) {
                continue;
            }
            QString cat = top->text();
            cat.remove(QLatin1Char('&'));
            walkMenu(top->menu(), cat);
        }
    }

    // Toolbar-only or uncategorized actions with shortcuts.
    for (QAction *act : findChildren<QAction *>()) {
        addAct(act, tr("Other"));
    }

    // F / F11 are dedicated QShortcuts on the window, not on the action.
    // Temporarily expose them on the action so the table can list Fullscreen.
    QList<QKeySequence> fullscreenShortcutsSaved;
    if (m_fullscreenAct) {
        fullscreenShortcutsSaved = m_fullscreenAct->shortcuts();
        if (fullscreenShortcutsSaved.isEmpty()) {
            m_fullscreenAct->setShortcuts({QKeySequence(Qt::Key_F), QKeySequence(Qt::Key_F11)});
        }
        addAct(m_fullscreenAct, tr("View"));
    }

    KeyboardShortcutsDialog dlg(actions, categories, this);
    connect(&dlg, &KeyboardShortcutsDialog::actionHighlighted, this, [this](QAction *act) {
        if (m_helpPanel && act) {
            if (m_helpDock && !m_helpDock->isVisible()) {
                m_helpDock->show();
            }
            m_helpPanel->showAction(act);
        }
    });
    connect(&dlg, &KeyboardShortcutsDialog::actionActivated, this, [this](QAction *act) {
        if (!act) {
            return;
        }
        if (m_helpPanel) {
            m_helpPanel->showAction(act);
        }
        if (act->isEnabled()) {
            act->trigger();
        }
    });
    dlg.exec();
    if (m_fullscreenAct) {
        m_fullscreenAct->setShortcuts(fullscreenShortcutsSaved);
    }
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About Biltoo"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    QString title = tr("<h3>Biltoo %1</h3>").arg(QApplication::applicationVersion());
    title += tr("<p>thumtoo %1</p>")
                 .arg(QString::fromUtf8(thumtoo::version_string().data(),
                                        int(thumtoo::version_string().size())));
    box.setText(title);

    // Compile-time optional libs only (no runtime user-disable yet).
    auto feat = [](bool on) {
        return on ? QObject::tr("✔ enabled") : QObject::tr("✘ missing");
    };
    const QString features = tr(
        "<p><b>Optional features in this build</b></p>"
        "<ul>"
        "<li>libvips (extra codecs, attention Pan&amp;Zoom): %1</li>"
        "<li>libexiv2 (Exif / IPTC / XMP metadata): %2</li>"
        "<li>thumtoo (size index + tiles/LQIP): %3</li>"
        "<li>thumtoo archives (zip / tar / 7z / rar / …): %4</li>"
        "<li>libunarr (solid RAR / CBR extract): %5</li>"
        "<li>MuPDF (PDF / EPUB pages): %6</li>"
        "<li>DjVuLibre (DjVu pages): %7</li>"
        "<li>libcurl (HTTP/S fetch via thumtoo): %8</li>"
        "<li>GIO (default-application Preferences): %9</li>"
        "</ul>"
        "<p>Qt imageformat plugins (e.g. KImageFormats for XCF) are loaded at "
        "runtime when installed.</p>")
        .arg(feat(BILTOO_FEATURE_VIPS),
             feat(BILTOO_FEATURE_EXIV2),
             feat(BILTOO_FEATURE_THUMTOO),
             feat(BILTOO_FEATURE_ARCHIVE),
             feat(BILTOO_FEATURE_THUMTOO_UNARR),
             feat(BILTOO_FEATURE_THUMTOO_MUPDF),
             feat(BILTOO_FEATURE_THUMTOO_DJVU),
             feat(BILTOO_FEATURE_THUMTOO_CURL),
             feat(BILTOO_FEATURE_GIO));

    box.setInformativeText(
        tr("<p>A classic image viewer with Gallery overview and a free-form "
           "Workspace for comparing images.</p>"
           "%1"
           "<p>Copyright © 2026 Ingo Ruhnke &lt;grumbel@gmail.com&gt;<br/>"
           "License: GPL-3.0-or-later</p>")
            .arg(features));
    // GNOME 2 HIG: single affirmative Close on the right is fine for about boxes
    box.setStandardButtons(QMessageBox::Close);
    box.button(QMessageBox::Close)->setText(tr("&Close"));
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
}

void MainWindow::showCachePrepareDialog()
{
    if (!ThumtooCache::isAvailable()) {
        QMessageBox::information(
            this, tr("Prepare tile cache"),
            tr("Durable tile cache requires thumtoo, which is not available in this build."));
        return;
    }
    CachePrepareDialog dlg(m_session.paths(), this);
    dlg.exec();
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    dlg.setSlideshowIntervalMs(m_slideshowIntervalMs);
    dlg.setSlideshowFullscreen(m_slideshowFullscreen);
    dlg.setSlideshowLoop(m_slideshowLoop);
    if (m_imageView) {
        dlg.setSlideshowTransitionIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentTransition()));
        dlg.setSlideshowTransitionDurationMs(m_imageView->hostSlideshow().settings().transitionDuration());
        dlg.setSlideshowMotionIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentMotion()));
        dlg.setPanZoomFactor(m_imageView->hostSlideshow().settings().currentPanZoomFactor());
        dlg.setSlideshowZoomIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentZoom()));
        dlg.setSlideshowLetterboxFillIndex(static_cast<int>(m_imageView->hostSlideshow().settings().currentLetterboxFill()));
        dlg.setSlideshowPadColor(m_imageView->hostSlideshow().padColorForPaint());
    }
    dlg.setSortModeIndex(static_cast<int>(m_sortMode));
    dlg.setStartInWorkspaceMode(m_startInWorkspaceMode);
    dlg.setImageModeLeftDragPan(m_imageView->hostChrome().isImageModeLeftDragPan());
    dlg.setBackgroundColor(m_imageView->hostCanvasBg().primaryColor());
    dlg.setBackgroundColorAlt(m_imageView->hostCanvasBg().altColor());
    dlg.setBackgroundPatternIndex(
        m_imageView->hostCanvasBg().currentPattern() == BackgroundPattern::Checkerboard ? 1 : 0);
    dlg.setCheckerboardWorkspaceOnly(m_imageView->hostCanvasBg().isCheckerWorkspaceOnly());
    dlg.setHudFontPointSize(m_imageView->hostHudPrefs().fontPointSizeValue());
    dlg.setHudTextColor(m_imageView->hostHudPrefs().textColorRef());
    dlg.setHudPanelColor(m_imageView->hostHudPrefs().panelColorRef());
    dlg.setScrollBarsVisible(m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked());
    dlg.setThumbnailLabelsVisible(m_hideThumbLabelsAct && !m_hideThumbLabelsAct->isChecked());
    dlg.setAdjustmentsPanelVisible(m_adjustmentsDock && m_adjustmentsDock->isVisible());
    dlg.setLayoutPanelPreferredInWorkspace(m_layoutPreferredInWorkspace);
    dlg.setThumbnailsPreferredWorkspace(m_thumbnailsPreferredWorkspace);
    dlg.setThumbnailsPreferredGallery(m_thumbnailsPreferredGallery);
    {
        int pos = 0;
        if (m_thumbnailEdge == ThumbnailEdge::Top) pos = 1;
        else if (m_thumbnailEdge == ThumbnailEdge::Left) pos = 2;
        else if (m_thumbnailEdge == ThumbnailEdge::Right) pos = 3;
        dlg.setThumbnailPositionIndex(pos);
    }
    {
        int layoutMode = static_cast<int>(LayoutMode::Masonry);
        if (m_imageView && m_imageView->isGalleryMode()) {
            layoutMode = static_cast<int>(m_imageView->hostLayout().currentMode());
        } else if (m_galleryReturnActive) {
            layoutMode = static_cast<int>(m_galleryReturnLayout);
        } else {
            QSettings settings;
            layoutMode = settings.value(QStringLiteral("lastGalleryLayout"), layoutMode).toInt();
        }
        dlg.setDefaultGalleryLayoutMode(layoutMode);
    }
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    setSlideshowIntervalMs(dlg.slideshowIntervalMs());
    m_slideshowFullscreen = dlg.slideshowFullscreen();
    m_slideshowLoop = dlg.slideshowLoop();
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowTransition(
            static_cast<SlideshowTransition>(dlg.slideshowTransitionIndex()));
        {
            // Full transition (out + in); cap at the interval.
            const int intervalCap = m_slideshowIntervalMs;
            m_imageView->hostSlideshow().setSlideshowTransitionDurationMs(
                SlideshowClocks::clampTransitionMs(dlg.slideshowTransitionDurationMs(), intervalCap));
        }
        m_imageView->hostSlideshow().setSlideshowMotion(
            static_cast<SlideshowMotion>(dlg.slideshowMotionIndex()));
        m_imageView->hostSlideshow().setPanZoomFactor(dlg.panZoomFactor());
        m_imageView->hostSlideshow().setSlideshowZoom(
            static_cast<SlideshowZoom>(
                SlideshowClocks::clampZoomIndex(dlg.slideshowZoomIndex())));
        m_imageView->hostSlideshow().setSlideshowPadColor(dlg.slideshowPadColor());
        m_imageView->hostSlideshow().setSlideshowLetterboxFill(
            static_cast<SlideshowLetterboxFill>(
                SlideshowClocks::clampLetterboxFillIndex(dlg.slideshowLetterboxFillIndex())));
        // If a slideshow is running, re-frame the current slide for zoom/motion.
        if (m_slideshowClockRunning) {
            m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
            m_imageView->hostSlideshow().reapplySlideshowFraming();
        }
    }
    {
        const int si = dlg.sortModeIndex();
        SortMode mode = SortMode::Name;
        if (si >= 0 && si <= 8) {
            mode = static_cast<SortMode>(si);
        }
        setSortMode(mode);
    }
    m_startInWorkspaceMode = dlg.startInWorkspaceMode();
    m_imageView->hostChrome().setImageModeLeftDragPan(dlg.imageModeLeftDragPan());
    m_imageView->hostShell().setBackgroundColor(dlg.backgroundColor());
    m_imageView->hostShell().setBackgroundColorAlt(dlg.backgroundColorAlt());
    if (m_thumbnailBar) {
        m_thumbnailBar->setStripBackground(dlg.backgroundColor());
    }
    m_imageView->hostShell().setBackgroundPattern(
        dlg.backgroundPatternIndex() == 1 ? BackgroundPattern::Checkerboard
                                          : BackgroundPattern::Solid);
    m_imageView->hostShell().setCheckerboardWorkspaceOnly(dlg.checkerboardWorkspaceOnly());
    m_imageView->hostHud().setFontPointSize(dlg.hudFontPointSize(), [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
    m_imageView->hostHud().setTextColor(dlg.hudTextColor(), [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
    m_imageView->hostHud().setPanelColor(dlg.hudPanelColor(), [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });

    if (m_toggleScrollBarsAct) {
        m_toggleScrollBarsAct->setChecked(dlg.scrollBarsVisible());
        toggleScrollBars();
    }
    if (m_hideThumbLabelsAct) {
        m_hideThumbLabelsAct->setChecked(!dlg.thumbnailLabelsVisible());
        if (m_thumbnailBar) {
            m_thumbnailBar->setLabelsVisible(dlg.thumbnailLabelsVisible());
        }
    }
    if (m_adjustmentsDock) {
        const bool showAdj = dlg.adjustmentsPanelVisible();
        m_adjustmentsDock->setVisible(showAdj);
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(showAdj);
        }
    }
    m_layoutPreferredInWorkspace = dlg.layoutPanelPreferredInWorkspace();
    m_thumbnailsPreferredWorkspace = dlg.thumbnailsPreferredWorkspace();
    m_thumbnailsPreferredGallery = dlg.thumbnailsPreferredGallery();
    updateLayoutPanelForMode();
    updateThumbnailBarForMode();
    updateFileExportActions();
    {
        const int pos = dlg.thumbnailPositionIndex();
        ThumbnailEdge edge = ThumbnailEdge::Bottom;
        if (pos == 1) edge = ThumbnailEdge::Top;
        else if (pos == 2) edge = ThumbnailEdge::Left;
        else if (pos == 3) edge = ThumbnailEdge::Right;
        setThumbnailBarPosition(edge);
    }
    {
        const int layoutMode = dlg.defaultGalleryLayoutMode();
        m_galleryReturnLayout = static_cast<LayoutMode>(layoutMode);
        QSettings settings;
        settings.setValue(QStringLiteral("lastGalleryLayout"), layoutMode);
        if (m_imageView && m_imageView->isGalleryMode()) {
            m_imageView->setLayoutMode(static_cast<LayoutMode>(layoutMode));
        }
    }
    writeSettings();
}

void MainWindow::selectAllThumbnails()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    // Gallery / Workspace: select every live canvas tile (Ctrl+A also handled
    // in ImageView). Filmstrip Select All remains the session multi-select when
    // focus is on the strip or we are in Image mode.
    if (m_imageView && (isGalleryMode() || isWorkspaceMode())) {
        m_imageView->hostWorkspace().selectAllCanvasItems();
        return;
    }
    if (!m_thumbnailBar) {
        return;
    }
    m_thumbnailBar->selectAllThumbs();
}

void MainWindow::updateWindowTitle()
{
    if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        const QString name = PagePath::displayName(m_session.paths().at(m_currentIndex));
        // GNOME-style: document name first, then app
        setWindowTitle(tr("%1 — Biltoo").arg(name));
    } else {
        setWindowTitle(tr("Biltoo"));
    }
}


void MainWindow::updateTocPanel()
{
    if (!m_tocPanel || !m_imageView) {
        return;
    }
    const QString path = m_imageView->hostImage().classicPath();
    if (path.isEmpty() || (!PagePath::isPageRef(path) && !PagePath::isEpubLayoutOnly(path))) {
        // Try session path at current index
        QString sessionPath = path;
        if (sessionPath.isEmpty() && m_currentIndex >= 0
            && m_currentIndex < m_session.paths().size()) {
            sessionPath = m_session.paths().at(m_currentIndex);
        }
        if (sessionPath.isEmpty()
            || (!PagePath::isPageRef(sessionPath) && !PagePath::isEpubLayoutOnly(sessionPath)
                && !PagePath::isPdfFile(sessionPath) && !PagePath::isDjvuFile(sessionPath)
                && !PagePath::isEpubFile(sessionPath))) {
            m_tocPanel->clear();
            return;
        }
        const auto outline = ThumtooCache::ensureDocumentOutline(sessionPath);
        m_tocPanel->setOutline(outline);
        return;
    }
    const auto outline = ThumtooCache::ensureDocumentOutline(path);
    m_tocPanel->setOutline(outline);
}

void MainWindow::navigateDocumentPage(int page_1based)
{
    if (page_1based < 1 || !m_imageView) {
        return;
    }
    QString path = m_imageView->hostImage().classicPath();
    if (path.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path.isEmpty()) {
        return;
    }
    // Prefer jumping within the session list.
    const QString doc = PagePath::documentFilePath(path);
    const QString layout = PagePath::epubLayoutParamsOf(path);
    for (int i = 0; i < m_session.paths().size(); ++i) {
        const QString &p = m_session.paths().at(i);
        if (!PagePath::isPageRef(p)) {
            continue;
        }
        if (PagePath::documentFilePath(p) == doc && PagePath::pageNumber(p) == page_1based) {
            // Full session cursor (index + SessionImageId + classicPath) — do not
            // assign m_currentIndex alone (lags setCurrentSessionId / loadImage).
            if (i != m_currentIndex) {
                setCurrentIndex(i);
                updateTocPanel();
            }
            return;
        }
    }
    // Build a page path even if not in session.
    QString target;
    if (!layout.isEmpty() || path.contains(QLatin1String("//epub:"))) {
        target = PagePath::makeEpubRef(doc, page_1based,
                                       layout.isEmpty() ? PagePath::epubLayoutParamsOf(path)
                                                        : layout);
    } else {
        target = PagePath::makeRef(doc, page_1based);
    }
    if (!target.isEmpty()) {
        m_imageView->hostDisplayPipeline().loadImage(target);
        updateStatus();
    }
}

void MainWindow::openDocumentLinkUri(const QString &uri)
{
    if (uri.isEmpty()) {
        return;
    }
    // Internal-looking URIs may still be page jumps.
    if (uri.startsWith(QLatin1Char('#'))) {
        int page = 0;
        if (uri.mid(1).startsWith(QLatin1String("page="))) {
            page = uri.mid(6).toInt();
        } else {
            page = uri.mid(1).toInt();
        }
        if (page > 0) {
            navigateDocumentPage(page);
            return;
        }
    }
    const QUrl url(uri);
    // Spine-relative EPUB paths (…xhtml / …html / …xml) are not web URLs —
    // thumtoo should have resolved them to page numbers. Do not hand them to
    // the desktop browser (opens nothing useful).
    const QString lower = uri.toLower();
    const bool looksInternalPath =
        url.scheme().isEmpty()
        || url.isRelative()
        || lower.endsWith(QLatin1String(".xhtml"))
        || lower.endsWith(QLatin1String(".html"))
        || lower.endsWith(QLatin1String(".htm"))
        || lower.endsWith(QLatin1String(".xml"))
        || lower.contains(QLatin1String(".xhtml#"))
        || lower.contains(QLatin1String(".html#"));
    if (looksInternalPath) {
        if (statusBar()) {
            statusBar()->showMessage(
                tr("Unresolved document link: %1").arg(uri), 5000);
        }
        return;
    }
    if (url.isValid() && !url.scheme().isEmpty()) {
        QDesktopServices::openUrl(url);
    }
}

void MainWindow::updateMetadataPanel()
{
    if (!m_metadataPanel || !m_imageView) {
        return;
    }
    // Hidden dock: do not decode on every Gallery/Workspace selection.
    // VisibilityChanged reconnect refreshes when the user opens the panel.
    if (m_metadataDock && !m_metadataDock->isVisible()) {
        return;
    }
    QString path;
    // Prefer canvas selection (Workspace / Gallery); else session index.
    const QStringList selected = m_imageView->selectedPaths();
    if (!selected.isEmpty()) {
        path = selected.first();
    } else if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path == m_metadataPath && !path.isEmpty()) {
        return;
    }
    m_metadataPath = path;
    if (path.isEmpty()) {
        m_metadataPanel->clear();
        return;
    }
    // Reuse already-decoded Gallery/Workspace tile pixels when present.
    QImage hint;
    if (ImageItem *item = m_imageView->findPreferredItemForPath(path)) {
        if (item->hasDecodedPixels()) {
            hint = item->sourceImage();
        }
    }
    m_metadataPanel->setImagePath(path, hint);
}

void MainWindow::updateAdjustmentsPanel()
{
    if (!m_adjustmentsPanel || !m_imageView) {
        return;
    }
    // Hidden dock: never copy tile pixels on Gallery selection (was a major lag spike).
    if (m_adjustmentsDock && !m_adjustmentsDock->isVisible()) {
        return;
    }
    ImageItem *item = m_imageView->targetItem();
    if (!item && !m_imageView->liveItems().isEmpty() && m_imageView->isImageMode()) {
        item = m_imageView->liveItems().first();
    }
    // SoftPreview stand-ins (interactive grade, ladder) are valid targets —
    // requiring hasDecodedPixels disabled the panel after the first slider tick.
    if (!item || !item->hasDisplayPixels()) {
        m_adjustmentsPanel->clearPreview();
        m_adjustmentsPanel->setEnabledControls(false);
        return;
    }
    m_adjustmentsPanel->setEnabledControls(true);
    m_adjustmentsPanel->setSessionLength(m_session.size());
    const int selN = m_imageView->transformTargets().size()
        + (m_thumbnailBar ? m_thumbnailBar->selectedSessionIds().size() : 0);
    m_adjustmentsPanel->setApplyToSelectionEnabled(
        selN > 1 || m_adjustmentsPanel->targetMode() == BatchTargets::Mode::IndexRange
        || m_adjustmentsPanel->targetMode() == BatchTargets::Mode::EvenIndices
        || m_adjustmentsPanel->targetMode() == BatchTargets::Mode::OddIndices);
    {
        QSignalBlocker b(m_adjustmentsPanel);
        m_adjustmentsPanel->setAdjustments(m_imageView->itemLiveColor(item));
    }
    // Preview is optional chrome — keep it cheap (scaled down).
    QPixmap pm = item->pixmap();
    if (!pm.isNull() && (pm.width() > 512 || pm.height() > 512)) {
        pm = pm.scaled(512, 512, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    m_adjustmentsPanel->setPreviewImage(pm.toImage());
}



void MainWindow::updateCropPanel()
{
    if (!m_cropPanel || !m_imageView) {
        return;
    }
    if (m_cropDock && !m_cropDock->isVisible()) {
        return;
    }
    ImageItem *item = m_imageView->targetItem();
    if (!item && !m_imageView->liveItems().isEmpty() && m_imageView->isImageMode()) {
        item = m_imageView->liveItems().first();
    }
    const bool hasTarget = item != nullptr;
    m_cropPanel->setEnabledControls(hasTarget || m_session.size() > 0);
    m_cropPanel->setSessionLength(m_session.size());
    const int selN = m_imageView->transformTargets().size()
        + (m_thumbnailBar ? m_thumbnailBar->selectedSessionIds().size() : 0);
    m_cropPanel->setApplyToSelectionEnabled(selN > 1
        || m_cropPanel->targetMode() == BatchTargets::Mode::IndexRange
        || m_cropPanel->targetMode() == BatchTargets::Mode::EvenIndices
        || m_cropPanel->targetMode() == BatchTargets::Mode::OddIndices);
    if (!item) {
        m_cropPanel->setPageSize(QSize());
        m_cropPanel->setStatusText(tr("No page selected"));
        return;
    }
    const QString path = item->path();
    QSize logical = ThumtooCache::cachedSize(path);
    if (!SessionAppearance::isUsableNativeSize(logical)) {
        logical = m_imageView->hostSizeBook().known(path);
    }
    if (!SessionAppearance::isUsableNativeSize(logical)) {
        const QSize isz = item->imageSize();
        if (isz.width() > 1 && isz.height() > 1) {
            logical = isz;
        }
    }
    m_cropPanel->setPageSize(logical);

    const CropPanelRecipe recipe = m_cropPanel->recipe();
    QString status;
    if (!SessionAppearance::isUsableNativeSize(logical)) {
        status = tr("Page size unknown — open/probe size or wait for soft sample");
    } else {
        QImage sample;
        if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
            sample = item->sourceImage();
            if (sample.isNull()) {
                sample = item->pixmap().toImage();
            }
            if (sample.isNull()) {
                sample = ImageCache::get(path);
            }
        }
        const QRect crop = CropRecipeUtil::computeCropRect(recipe, logical, sample);
        if (CropRecipeUtil::isUsableCrop(crop, logical)) {
            status = tr("%1×%2 → crop %3,%4 %5×%6  (selection: %7)")
                         .arg(logical.width())
                         .arg(logical.height())
                         .arg(crop.x())
                         .arg(crop.y())
                         .arg(crop.width())
                         .arg(crop.height())
                         .arg(m_imageView->transformTargets().size());
        } else if (recipe.mode == CropPanelRecipe::Mode::Autocrop && sample.isNull()) {
            status = tr("%1×%2 — autocrop needs a soft sample (Apply will wait/load)")
                         .arg(logical.width())
                         .arg(logical.height());
        } else {
            status = tr("%1×%2 — recipe is full-frame (no crop)")
                         .arg(logical.width())
                         .arg(logical.height());
        }
    }
    m_cropPanel->setStatusText(status);
}

void MainWindow::updateLayoutPanel()
{
    if (!m_layoutPanel || !m_imageView) {
        return;
    }
    const bool workspace = m_imageView->isWorkspaceMode();
    m_layoutPanel->setWorkspaceActive(workspace);
    int count = 0;
    if (workspace && m_imageView->canvasScene()) {
        for (QGraphicsItem *gi : m_imageView->canvasScene()->selectedItems()) {
            if (qgraphicsitem_cast<ImageItem *>(gi)) {
                ++count;
            }
        }
    }
    m_layoutPanel->setSelectionCount(count);
}

void MainWindow::applyWorkspaceLayoutFromPanel()
{
    if (!m_layoutPanel || !m_imageView || !m_imageView->isWorkspaceMode()) {
        return;
    }
    if (m_imageView->hostWorkspace().layoutItems(m_layoutPanel->params())) {
        markWorkspaceDirty();
        if (statusBar()) {
            statusBar()->showMessage(tr("Layout applied to selection."), 2500);
        }
    }
}

void MainWindow::ensureWorkStatusPoll(bool workBusy)
{
    if (!workBusy) {
        if (m_workStatusTimer) {
            m_workStatusTimer->stop();
        }
        return;
    }
    if (!m_workStatusTimer) {
        m_workStatusTimer = new QTimer(this);
        m_workStatusTimer->setInterval(200);
        connect(m_workStatusTimer, &QTimer::timeout, this, [this]() {
            if (!statusBar() || !ThumtooCache::isAvailable()) {
                if (m_workStatusTimer) {
                    m_workStatusTimer->stop();
                }
                return;
            }
            if (!ThumtooCache::workActivityBusy()) {
                if (m_workStatusTimer) {
                    m_workStatusTimer->stop();
                }
                // Fall through to normal status (decode pending / clear).
                updateStatus();
                return;
            }
            // Cheap path: only the work line (no metadata/nav refresh).
            refreshWorkActivityStatusBar();
        });
    }
    if (!m_workStatusTimer->isActive()) {
        m_workStatusTimer->start();
    }
}

void MainWindow::refreshWorkActivityStatusBar()
{
    if (!statusBar() || !ThumtooCache::isAvailable()) {
        return;
    }
    const ThumtooCache::WorkActivity act = ThumtooCache::workActivity();
    QStringList parts;
    if (act.archiveReadRunning > 0) {
        QString arch = tr("Archive read ×%1").arg(act.archiveReadRunning);
        if (!act.runningArchiveLabels.isEmpty()) {
            arch += tr(" · %1").arg(act.runningArchiveLabels.last());
        }
        parts << arch;
    }
    if (act.sizeQueued + act.sizeRunning > 0) {
        QString probe = tr("Size probes %1 running · %2 queued")
                            .arg(act.sizeRunning)
                            .arg(act.sizeQueued);
        if (!act.sizeRunningUris.isEmpty()) {
            const QString leaf = PagePath::displayName(act.sizeRunningUris.last());
            if (!leaf.isEmpty()) {
                probe += tr(" · %1").arg(leaf);
            }
        }
        if (m_imageView && m_imageView->hostGallerySizeResolve().active()) {
            const int total = m_imageView->hostGallerySizeResolve().total();
            const int left = m_imageView->hostGallerySizeResolve().pendingCount();
            if (total > 0) {
                probe += tr(" · session %1/%2")
                             .arg(qMax(0, total - left))
                             .arg(total);
            }
        }
        parts << probe;
    }
    if (act.tileQueued + act.tileRunning > 0) {
        QString tiles = tr("Tiles %1 running · %2 queued")
                            .arg(act.tileRunning)
                            .arg(act.tileQueued);
        if (!act.tileRunningLabels.isEmpty()) {
            tiles += tr(" · %1").arg(act.tileRunningLabels.last());
        }
        parts << tiles;
    }
    if (act.softQueued + act.softRunning > 0) {
        QString soft = tr("Soft %1 running · %2 queued")
                           .arg(act.softRunning)
                           .arg(act.softQueued);
        if (!act.softRunningUris.isEmpty()) {
            soft += tr(" · %1").arg(
                PagePath::displayName(act.softRunningUris.last()));
        }
        parts << soft;
    }
    if (!parts.isEmpty()) {
        statusBar()->showMessage(parts.join(QStringLiteral(" · ")), 0);
        m_decodeStatusActive = true;
        if (m_decodeStatusClearTimer) {
            m_decodeStatusClearTimer->stop();
        }
    }
}

void MainWindow::updateStatus()
{
    if (m_inUpdateStatus) {
        return;
    }
    m_inUpdateStatus = true;
    const auto statusGuard = qScopeGuard([this]() { m_inUpdateStatus = false; });
    if (m_tocDock && m_tocDock->isVisible()) {
        updateTocPanel();
    }
    if (m_imageView && !m_imageView->hostTextLayer().linkHoverTipRef().isEmpty()) {
        statusBar()->showMessage(m_imageView->hostTextLayer().linkHoverTipRef());
    }

    updateNavigationActions();
    updateMetadataPanel();
    updateAdjustmentsPanel();
    updateCropPanel();
    if (m_ocrDock && m_ocrDock->isVisible()) {
        updateOcrPanel();
    }
    if (m_textDock && m_textDock->isVisible()) {
        updateTextPanel();
    }
    // Session index on ImageView so status bar and on-image HUD share n/N.
    if (m_imageView) {
        // Silent while the slideshow timer advances; user Next/Prev still pulse.
        m_imageView->hostSlideshow().setSessionPosition(m_currentIndex, m_session.paths().size(),
                                        !m_slideshowAdvancing);
        // Image mode identity follows classicPath when it is set. statusChanged from
        // Image::enter used to call this while m_currentIndex still lagged the open
        // target and reverted setCurrentSessionId — wrong contentBake on install.
        SessionImageId publishId = currentSessionId();
        if (isImageMode()) {
            const QString classic = m_imageView->hostImage().classicPath();
            if (!classic.isEmpty()) {
                const SessionImageId pinned = m_imageView->hostSessionId().currentIdValue();
                if (pinned != kInvalidSessionImageId) {
                    const int pinIdx = indexOfSessionId(pinned);
                    if (pinIdx >= 0 && m_session.paths().at(pinIdx) == classic) {
                        publishId = pinned;
                    } else {
                        const int byPath = indexOfPathPreferId(classic);
                        if (byPath >= 0) {
                            publishId = sessionIdAt(byPath);
                        }
                    }
                } else {
                    const int byPath = indexOfPathPreferId(classic);
                    if (byPath >= 0) {
                        publishId = sessionIdAt(byPath);
                    }
                }
            }
        }
        m_imageView->setCurrentSessionId(publishId);
        const QString err = m_imageView->hostSessionId().lastLoadErrorRef();
        if (!err.isEmpty() && statusBar()) {
            statusBar()->showMessage(
                tr("Could not load “%1”").arg(PagePath::displayName(err)), 5000);
        }
        // Live work activity from thumtoo ActivityLedger (size/archive/tiles/soft).
        bool sizeProbeStatusShown = false;
        if (statusBar() && ThumtooCache::isAvailable()) {
            const bool busy = ThumtooCache::workActivityBusy();
            if (busy) {
                refreshWorkActivityStatusBar();
                sizeProbeStatusShown = true;
            }
            ensureWorkStatusPoll(busy);
        } else {
            ensureWorkStatusPoll(false);
        }
        // Remaining decode work (Gallery blanks + filmstrip unloaded), not
        // concurrent inflight — the latter flickered 1↔0 between jobs.
        int pending = m_imageView->pendingDecodeCount();
        if (m_thumbnailBar) {
            pending += m_thumbnailBar->pendingLoadCount();
        }
        if (!sizeProbeStatusShown && pending > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("Loading %n thumbnail…", "thumb/decode progress", pending), 0);
            m_decodeStatusActive = true;
            if (m_decodeStatusClearTimer) {
                m_decodeStatusClearTimer->stop();
            }
        } else if (m_decodeStatusActive && statusBar()) {
            // Hold the last non-zero message briefly so a gap between claiming
            // the next soft job does not clear the bar.
            if (!m_decodeStatusClearTimer) {
                m_decodeStatusClearTimer = new QTimer(this);
                m_decodeStatusClearTimer->setSingleShot(true);
                m_decodeStatusClearTimer->setInterval(300);
                connect(m_decodeStatusClearTimer, &QTimer::timeout, this, [this]() {
                    if (!m_imageView || !statusBar()) {
                        m_decodeStatusActive = false;
                        return;
                    }
                    int still = m_imageView->pendingDecodeCount();
                    if (m_thumbnailBar) {
                        still += m_thumbnailBar->pendingLoadCount();
                    }
                    if (still > 0) {
                        statusBar()->showMessage(
                            tr("Loading %n thumbnail…", "thumb/decode progress",
                               still),
                            0);
                        m_decodeStatusActive = true;
                        return;
                    }
                    statusBar()->clearMessage();
                    m_decodeStatusActive = false;
                });
            }
            m_decodeStatusClearTimer->start();
        }
    }
    m_statusLabel->setText(m_imageView->statusText());
}

void MainWindow::onMouseInfoChanged(const ImageMouseInfo &info)
{
    if (!info.valid) {
        m_mouseLabel->clear();
        if (m_colorSwatch) {
            m_colorSwatch->setStyleSheet(QStringLiteral(
                "QLabel { border: 1px solid #888; background: transparent; }"));
            m_colorSwatch->setToolTip(tr("Colour under the cursor"));
        }
        return;
    }
    const QColor &c = info.pixelColor;
    if (c.alpha() < 255) {
        m_mouseLabel->setText(
            tr("(%1, %2)  RGBA %3 %4 %5 %6")
                .arg(info.imagePos.x())
                .arg(info.imagePos.y())
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue())
                .arg(c.alpha()));
    } else {
        m_mouseLabel->setText(
            tr("(%1, %2)  RGB %3 %4 %5")
                .arg(info.imagePos.x())
                .arg(info.imagePos.y())
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue()));
    }
    if (m_colorSwatch) {
        m_colorSwatch->setStyleSheet(
            QStringLiteral("QLabel { border: 1px solid #888; background-color: %1; }")
                .arg(c.name(QColor::HexArgb)));
        m_colorSwatch->setToolTip(
            c.alpha() < 255
                ? tr("RGBA %1 %2 %3 %4 (%5)")
                      .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
                      .arg(c.name(QColor::HexArgb))
                : tr("RGB %1 %2 %3 (%4)")
                      .arg(c.red()).arg(c.green()).arg(c.blue())
                      .arg(c.name()));
    }
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    // Mode-filtered context menu (AUDIT L29 polish).
    QMenu menu(this);
    menu.addAction(m_openAct);
    menu.addAction(m_addAct);
    if (m_openSelectionNewWindowAct) {
        menu.addAction(m_openSelectionNewWindowAct);
    }
    menu.addSeparator();
    if (isImageMode()) {
        menu.addAction(m_previousAct);
        menu.addAction(m_nextAct);
        menu.addSeparator();
    }
    menu.addAction(m_zoomFitAct);
    menu.addAction(m_zoom1to1Act);
    menu.addSeparator();
    menu.addAction(m_rotateLeftAct);
    menu.addAction(m_rotateRightAct);
    menu.addAction(m_flipHAct);
    menu.addAction(m_flipVAct);
    if (m_resetContentAppearanceAct) {
        menu.addAction(m_resetContentAppearanceAct);
    }
    if (isWorkspaceMode()) {
        menu.addAction(m_resetScaleAct);
        if (m_resetShearAct) {
            menu.addAction(m_resetShearAct);
        }
        menu.addAction(m_resetRotationAct);
        menu.addAction(m_copyWorkspaceAct);
        menu.addAction(m_cutWorkspaceAct);
        menu.addAction(m_pasteWorkspaceAct);
        menu.addAction(m_duplicateAct);
        menu.addAction(m_raiseAct);
        menu.addAction(m_lowerAct);
    }
    menu.addSeparator();
    if (m_backToGalleryAct && m_backToGalleryAct->isEnabled()) {
        menu.addAction(m_backToGalleryAct);
    }
    // Workspace mode toggle stays on the main toolbar / Workspace menu only.
    menu.addSeparator();
    // Menu bar is hidden in fullscreen; surface slideshow controls here.
    if (isFullScreen() && m_slideshowAct) {
        menu.addAction(m_slideshowAct);
    }
    if (m_slideshowSettingsAct) {
        menu.addAction(m_slideshowSettingsAct);
    }
    menu.addAction(m_fullscreenAct);
    menu.addAction(m_preferencesAct);
    menu.exec(m_imageView->mapToGlobal(pos));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        updateFullscreenUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::updateFullscreenUi()
{
    const bool fs = isFullScreen();
    m_fullscreenAct->setChecked(fs);
    m_fullscreenAct->setText(fs ? tr("Exit &Fullscreen") : tr("F&ullscreen"));

    // Leaving fullscreen ends the slideshow session (playing or paused).
    if (!fs && isSlideshowSession()) {
        stopSlideshow();
    }

    if (fs) {
        m_toolBarVisibleBeforeFullscreen = m_toolBar->isVisible();
        m_thumbnailBarVisibleBeforeFullscreen =
            m_thumbnailDock ? m_thumbnailDock->isVisible() : m_thumbnailBar->isVisible();
        m_metadataVisibleBeforeFullscreen =
            m_metadataDock && m_metadataDock->isVisible();
        m_layoutVisibleBeforeFullscreen =
            m_layoutDock && m_layoutDock->isVisible();
        m_adjustmentsVisibleBeforeFullscreen =
            m_adjustmentsDock && m_adjustmentsDock->isVisible();
        m_helpVisibleBeforeFullscreen =
            m_helpDock && m_helpDock->isVisible();
        m_toolBar->setVisible(false);
        if (m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(false);
        }
        if (m_thumbnailDock) {
            m_thumbnailDock->setVisible(false);
        } else {
            m_thumbnailBar->setVisible(false);
        }
        m_metadataDock->setVisible(false);
        if (m_layoutDock) {
            m_layoutDock->setVisible(false);
        }
        if (m_adjustmentsDock) {
            m_adjustmentsDock->setVisible(false);
        }
        if (m_helpDock) {
            m_helpDock->setVisible(false);
        }
        m_toggleToolBarAct->setChecked(false);
        m_toggleThumbnailBarAct->setChecked(false);
        m_toggleMetadataAct->setChecked(false);
        if (m_toggleLayoutPanelAct) {
            m_toggleLayoutPanelAct->setChecked(false);
        }
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(false);
        }
        if (m_toggleHelpAct) {
            m_toggleHelpAct->setChecked(false);
        }
        menuBar()->setVisible(false);
        statusBar()->setVisible(false);
        // Ensure arrow keys reach ImageView (navigation), not a hidden chrome widget.
        if (m_imageView) {
            m_imageView->setFocus(Qt::OtherFocusReason);
        }
    } else {
        m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
        m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);
        if (m_metadataDock) {
            m_metadataDock->setVisible(m_metadataVisibleBeforeFullscreen);
        }
        if (m_toggleMetadataAct) {
            m_toggleMetadataAct->setChecked(m_metadataVisibleBeforeFullscreen);
        }
        if (m_adjustmentsDock) {
            m_adjustmentsDock->setVisible(m_adjustmentsVisibleBeforeFullscreen);
        }
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(m_adjustmentsVisibleBeforeFullscreen);
        }
        if (m_helpDock) {
            m_helpDock->setVisible(m_helpVisibleBeforeFullscreen);
        }
        if (m_toggleHelpAct) {
            m_toggleHelpAct->setChecked(m_helpVisibleBeforeFullscreen);
        }
        // Thumbnails and Layout panel follow per-mode rules, not a single
        // pre-fullscreen snapshot (Gallery must not regain a Workspace layout dock).
        updateThumbnailBarForMode();
        updateLayoutPanelForMode();
        updateFileExportActions();
        if (isWorkspaceMode() && m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(true);
        }
        menuBar()->setVisible(true);
        statusBar()->setVisible(true);
    }
}

namespace {

/** Human-readable window frame: "x,y,width,height" (screen coordinates). */
QString formatWindowGeometry(const QRect &r)
{
    return QStringLiteral("%1,%2,%3,%4")
        .arg(r.x())
        .arg(r.y())
        .arg(r.width())
        .arg(r.height());
}

bool parseWindowGeometry(const QString &s, QRect *out)
{
    if (!out || s.trimmed().isEmpty()) {
        return false;
    }
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() != 4) {
        return false;
    }
    bool okX = false, okY = false, okW = false, okH = false;
    const int x = parts.at(0).trimmed().toInt(&okX);
    const int y = parts.at(1).trimmed().toInt(&okY);
    const int w = parts.at(2).trimmed().toInt(&okW);
    const int h = parts.at(3).trimmed().toInt(&okH);
    if (!okX || !okY || !okW || !okH || w < 100 || h < 100) {
        return false;
    }
    *out = QRect(x, y, w, h);
    return true;
}

} // namespace

void MainWindow::readSettings()
{
    QSettings settings;

    // Prefer human-readable geometry ("x,y,w,h"). Fall back to legacy QByteArray
    // from QWidget::saveGeometry() for older configs.
    QRect frame;
    bool haveFrame = parseWindowGeometry(
        settings.value(QStringLiteral("windowGeometry")).toString(), &frame);
    if (!haveFrame) {
        const QByteArray legacy = settings.value(QStringLiteral("geometry")).toByteArray();
        if (!legacy.isEmpty()) {
            restoreGeometry(legacy);
            // Fullscreen is not a persistent preference — only --fullscreen / -f.
            if (isFullScreen()) {
                setWindowState(windowState() & ~Qt::WindowFullScreen);
                showNormal();
                const QRect normal = settings.value(QStringLiteral("normalGeometry")).toRect();
                if (normal.isValid()) {
                    setGeometry(normal);
                }
            }
            haveFrame = true;
            frame = geometry();
        }
    } else {
        setGeometry(frame);
    }
    if (haveFrame && settings.value(QStringLiteral("windowMaximized"), false).toBool()) {
        showMaximized();
    }

    // Dock layout (positions + tabbing + sizes). Version-gated — see
    // kDockLayoutStateVersion. Mismatched / missing version → ignore blob.
    {
        const int ver = settings.value(QStringLiteral("dockLayoutVersion"), 0).toInt();
        const QByteArray state =
            settings.value(QStringLiteral("dockLayoutState")).toByteArray();
        // Drop legacy keys from older experiments.
        settings.remove(QStringLiteral("windowState"));
        settings.remove(QStringLiteral("windowStateVersion"));
        settings.remove(QStringLiteral("windowStateQt"));
        if (ver == kDockLayoutStateVersion && !state.isEmpty()) {
            restoreState(state);
        } else if (ver != 0 && ver != kDockLayoutStateVersion) {
            settings.remove(QStringLiteral("dockLayoutState"));
            settings.remove(QStringLiteral("dockLayoutVersion"));
        }
    }

    // Adjustments is opt-in when no matching dock layout was restored.
    // Explicit key still wins after restore so Preferences can force hide.
    if (m_adjustmentsDock) {
        const bool showAdj =
            settings.value(QStringLiteral("adjustmentsPanelVisible"), false).toBool();
        m_adjustmentsDock->setVisible(showAdj);
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(showAdj);
        }
    }
    // Per-mode chrome preferences (defaults: Workspace thumbs on, Gallery off,
    // Layout panel off). Layout visibility is forced through
    // updateLayoutPanelForMode after mode is applied below.
    m_thumbnailsPreferredWorkspace =
        settings.value(QStringLiteral("thumbnailsPreferredWorkspace"), true).toBool();
    m_thumbnailsPreferredGallery =
        settings.value(QStringLiteral("thumbnailsPreferredGallery"), false).toBool();
    m_layoutPreferredInWorkspace =
        settings.value(QStringLiteral("layoutPreferredInWorkspace"), false).toBool();
    m_toolBarVisibleBeforeFullscreen =
        settings.value(QStringLiteral("toolBarVisible"), true).toBool();
    {
        const bool pinned = settings.value(QStringLiteral("locationBarPinned"), false).toBool();
        const bool searchPinned = settings.value(QStringLiteral("searchBarPinned"), false).toBool();
        setLocationBarPinned(pinned);
        setSearchBarPinned(searchPinned);
    }

    // restoreState can put the location bar back on the same row as the main
    // toolbar (from sessions saved before the dedicated-row layout). Force a
    // break so it always sits on its own row under the main toolbar.
    // Re-apply visibility afterwards — insertToolBarBreak / restoreState can
    // leave the bar shown even when it is not pinned.
    if (m_locationBar) {
        insertToolBarBreak(m_locationBar);
        m_locationBar->setVisible(m_locationBarPinned);
    }
    if (m_searchBar) {
        insertToolBarBreak(m_searchBar);
        m_searchBar->setVisible(m_searchBarPinned);
    }

    m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
    m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);

    const QString sort = settings.value(QStringLiteral("sortMode"), QStringLiteral("name")).toString();
    if (sort == QLatin1String("mtime")) {
        m_sortMode = SortMode::MTime;
    } else if (sort == QLatin1String("filesize")) {
        m_sortMode = SortMode::FileSize;
    } else if (sort == QLatin1String("width")) {
        m_sortMode = SortMode::Width;
    } else if (sort == QLatin1String("height")) {
        m_sortMode = SortMode::Height;
    } else if (sort == QLatin1String("pixels")) {
        m_sortMode = SortMode::PixelCount;
    } else if (sort == QLatin1String("path")) {
        m_sortMode = SortMode::Path;
    } else if (sort == QLatin1String("aspect") || sort == QLatin1String("aspectratio")) {
        m_sortMode = SortMode::AspectRatio;
    } else if (sort == QLatin1String("shuffle") || sort == QLatin1String("random")) {
        m_sortMode = SortMode::Shuffle;
    } else {
        m_sortMode = SortMode::Name;
    }
    setSortMode(m_sortMode); // checks the matching action (may re-sort empty list)

    setSlideshowIntervalMs(
        settings.value(QStringLiteral("slideshowIntervalMs"), 3000).toInt());
    if (m_imageView) {
        const int transitionKind = settings.value(QStringLiteral("slideshowTransition"), 1).toInt();
        m_imageView->hostSlideshow().setSlideshowTransition(
            static_cast<SlideshowTransition>(SlideshowClocks::clampTransitionKind(transitionKind)));
        {
            // Full transition (out + in); cap at the interval.
            const int intervalCap = m_slideshowIntervalMs;
            const int transitionMs =
                settings.value(QStringLiteral("slideshowTransitionDurationMs"), 400).toInt();
            m_imageView->hostSlideshow().setSlideshowTransitionDurationMs(
                SlideshowClocks::clampTransitionMs(transitionMs, intervalCap));
        }
        m_imageView->hostSlideshow().setSlideshowMotion(
            static_cast<SlideshowMotion>(
                SlideshowClocks::clampMotionIndex(
                    settings.value(QStringLiteral("slideshowMotion"), 0).toInt())));
        m_imageView->hostSlideshow().setPanZoomFactor(
            settings.value(QStringLiteral("slideshowPanZoomFactor"), 1.12).toDouble());
        m_imageView->hostSlideshow().setSlideshowZoom(
            static_cast<SlideshowZoom>(
                SlideshowClocks::clampZoomIndex(
                    settings.value(QStringLiteral("slideshowZoomMode"), 0).toInt())));
        const QColor pad = QColor(settings.value(QStringLiteral("slideshowPadColor"),
            m_imageView->hostCanvasBg().primaryColor().name(QColor::HexRgb)).toString());
        if (pad.isValid()) {
            m_imageView->hostSlideshow().setSlideshowPadColor(pad);
        }
        m_imageView->hostSlideshow().setSlideshowLetterboxFill(
            static_cast<SlideshowLetterboxFill>(
                SlideshowClocks::clampLetterboxFillIndex(
                    settings.value(QStringLiteral("slideshowLetterboxFill"), 0).toInt())));
    }
    const int masonryCols = settings.value(QStringLiteral("masonryColumns"), 3).toInt();
    const int gridCols = settings.value(QStringLiteral("gridColumns"), 0).toInt();
    const int masonryRows = settings.value(QStringLiteral("masonryRows"), 3).toInt();
    if (m_imageView) {
        m_imageView->hostGallery().setMasonryColumns(masonryCols);
        m_imageView->hostGallery().setGridColumns(gridCols);
        m_imageView->hostGallery().setMasonryRows(masonryRows);
    }
    if (m_masonryCountSpin) {
        const QSignalBlocker blocker(m_masonryCountSpin);
        m_masonryCountSpin->setValue(m_imageView ? m_imageView->hostLayout().masonryColumnsValue()
                                                 : masonryCols);
    }
    m_slideshowFullscreen =
        settings.value(QStringLiteral("slideshowFullscreen"), true).toBool();
    m_slideshowLoop =
        settings.value(QStringLiteral("slideshowLoop"), true).toBool();
    if (m_imageView) {
        const bool sticky =
            settings.value(QStringLiteral("stickyZoomEnabled"), false).toBool();
        const int kind =
            settings.value(QStringLiteral("stickyZoomKind"), 0).toInt();
        m_imageView->hostFraming().setStickyZoomKind(
            static_cast<StickyZoomKind>(SlideshowClocks::clampZoomIndex(kind)));
        m_imageView->hostImage().setStickyZoomEnabled(sticky);
        syncZoomModeChecks();
    }

    // Workspace mode is off by default. Only enable at startup when the user
    // opted in via Preferences ("Start in workspace mode").
    m_startInWorkspaceMode =
        settings.value(QStringLiteral("startInWorkspaceMode"), false).toBool();
    {
        const int layoutInt = settings.value(QStringLiteral("lastGalleryLayout"), -1).toInt();
        if (layoutInt >= int(LayoutMode::SideBySide)
            && layoutInt <= int(LayoutMode::Facing)) {
            m_galleryReturnLayout = static_cast<LayoutMode>(layoutInt);
        }
    }
    // Migrate legacy key if present and new key never set
    if (!settings.contains(QStringLiteral("startInWorkspaceMode"))
        && settings.contains(QStringLiteral("workspaceMode"))) {
        // Do not migrate "true" from a previous session toggle — always prefer off
        settings.remove(QStringLiteral("workspaceMode"));
        m_startInWorkspaceMode = false;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(m_startInWorkspaceMode);
    }
    if (m_imageView) {
        m_imageView->setViewMode(m_startInWorkspaceMode
                                     ? ImageView::ViewMode::Workspace
                                     : ImageView::ViewMode::Image);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(m_startInWorkspaceMode);
        const int thumbSize = settings.value(QStringLiteral("thumbnailSize"),
                                             ThumbnailBar::kDefaultThumbSize).toInt();
        m_thumbnailBar->setThumbSize(thumbSize);
        const bool cropThumbs = settings.value(QStringLiteral("thumbnailCropToSquare"), false).toBool();
        m_thumbnailBar->setCropToSquare(cropThumbs);
        if (m_cropThumbnailsAct) {
            m_cropThumbnailsAct->setChecked(cropThumbs);
        }
        const QString pos = settings.value(QStringLiteral("thumbnailBarPosition"),
                                           QStringLiteral("bottom")).toString();
        ThumbnailEdge edge = ThumbnailEdge::Bottom;
        if (pos == QLatin1String("left")) {
            edge = ThumbnailEdge::Left;
        } else if (pos == QLatin1String("right")) {
            edge = ThumbnailEdge::Right;
        } else if (pos == QLatin1String("top")) {
            edge = ThumbnailEdge::Top;
        }
        setThumbnailBarPosition(edge);
    }
    // Filmstrip lives in ThumbnailDock; geometry is part of windowState.
    updateWorkspaceActionVisibility();

    const bool showBars = settings.value(QStringLiteral("scrollBarsVisible"), false).toBool();
    if (m_toggleScrollBarsAct) {
        m_toggleScrollBarsAct->setChecked(showBars);
        toggleScrollBars();
    }

    if (m_imageView) {
        const bool leftPan =
            settings.value(QStringLiteral("imageModeLeftDragPan"), true).toBool();
        m_imageView->hostChrome().setImageModeLeftDragPan(leftPan);
        const bool hud = settings.value(QStringLiteral("hudVisible"), false).toBool();
        m_imageView->hostHud().setVisible(hud, [v = m_imageView, on = bool(hud)]() {
            v->hostSlideshow().syncProgressTimerWithHud(on);
            if (v->viewport()) v->viewport()->update();
        });
        if (m_toggleHudAct) {
            m_toggleHudAct->setChecked(hud);
        }
        {
            const bool smooth =
                settings.value(QStringLiteral("view/smoothScaling"), true).toBool();
            DisplayQuality::setSmoothScaling(smooth);
            if (m_smoothScalingAct) {
                m_smoothScalingAct->setChecked(smooth);
            }
            if (m_imageView) {
                m_imageView->setRenderHint(QPainter::SmoothPixmapTransform, smooth);
            }
        }
        {
            const bool editMarks =
                settings.value(QStringLiteral("contentEditMarksVisible"), true).toBool();
            if (m_toggleContentEditMarksAct) {
                m_toggleContentEditMarksAct->setChecked(editMarks);
            }
            if (m_imageView) {
                m_imageView->hostShell().setContentEditMarksVisible(editMarks);
            }
        }
        m_imageView->hostHud().setFontPointSize(
            settings.value(QStringLiteral("hudFontPointSize"), 11).toInt(),
            [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
        {
            const QColor tc(settings.value(QStringLiteral("hudTextColor"),
                                           QStringLiteral("#ffffff")).toString());
            if (tc.isValid()) {
                m_imageView->hostHud().setTextColor(tc, [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
            }
            // HexArgb is #AARRGGBB (Qt). Legacy #RRGGBBAA with alpha trailing is
            // rejected by the length-8 parser as fully transparent — migrate.
            const QString pcStr = settings.value(QStringLiteral("hudPanelColor"),
                                                 QStringLiteral("#a0000000")).toString();
            QColor pc(pcStr);
            if (!pc.isValid() || pc.alpha() == 0) {
                // Recover common mis-saved form #000000XX (RRGGBB + alpha byte)
                if (pcStr.size() == 9 && pcStr.startsWith(QLatin1Char('#'))) {
                    const QString rgb = pcStr.mid(1, 6);
                    const QString aa = pcStr.mid(7, 2);
                    pc = QColor(QStringLiteral("#") + aa + rgb);
                }
            }
            if (pc.isValid() && pc.alpha() > 0) {
                m_imageView->hostHud().setPanelColor(pc, [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
            } else if (pc.isValid() && pc.alpha() == 0) {
                // Never leave a fully transparent panel as the loaded preference.
                m_imageView->hostHud().setPanelColor(QColor(0, 0, 0, 160), [v = m_imageView]() { if (v->viewport()) v->viewport()->update(); });
            }
        }
        const QColor bg = QColor(settings.value(QStringLiteral("backgroundColor"),
                                                QStringLiteral("#2a2a2a")).toString());
        if (bg.isValid()) {
            m_imageView->hostShell().setBackgroundColor(bg);
            if (m_thumbnailBar) {
                m_thumbnailBar->setStripBackground(bg);
            }
        }
        const QColor bgAlt = QColor(settings.value(QStringLiteral("backgroundColorAlt"),
                                                   QStringLiteral("#303030")).toString());
        if (bgAlt.isValid()) {
            m_imageView->hostShell().setBackgroundColorAlt(bgAlt);
        }
        const QString pat = settings.value(QStringLiteral("backgroundPattern"),
                                           QStringLiteral("checkerboard")).toString();
        m_imageView->hostShell().setBackgroundPattern(
            pat == QLatin1String("solid") ? BackgroundPattern::Solid
                                          : BackgroundPattern::Checkerboard);
        m_imageView->hostShell().setCheckerboardWorkspaceOnly(
            settings.value(QStringLiteral("checkerboardWorkspaceOnly"), true).toBool());
    }
    if (m_thumbnailBar) {
        const bool labels =
            settings.value(QStringLiteral("thumbnailLabelsVisible"), true).toBool();
        m_thumbnailBar->setLabelsVisible(labels);
        if (m_hideThumbLabelsAct) {
            m_hideThumbLabelsAct->setChecked(!labels);
        }
    }

    // Session history (full path lists per open)
    m_sessionHistory.clear();
    const int histCount = settings.beginReadArray(QStringLiteral("sessionHistory"));
    for (int i = 0; i < histCount && i < kMaxSessionHistory; ++i) {
        settings.setArrayIndex(i);
        const QStringList paths = settings.value(QStringLiteral("paths")).toStringList();
        if (!paths.isEmpty()) {
            m_sessionHistory.append(paths);
        }
    }
    settings.endArray();
    rebuildHistoryMenu();

    m_recentProjects.clear();
    const QStringList recent = settings.value(QStringLiteral("recentProjects")).toStringList();
    for (const QString &p : recent) {
        if (!p.isEmpty() && m_recentProjects.size() < kMaxRecentProjects) {
            m_recentProjects.append(p);
        }
    }
    rebuildRecentProjectsMenu();
}

void MainWindow::writeSettings()
{
    QSettings settings;
    // Human-readable frame geometry (not QWidget::saveGeometry QByteArray).
    // Fullscreen is never persisted — store the normal frame instead.
    const QRect frame = isFullScreen() ? normalGeometry() : geometry();
    const bool maximized = !isFullScreen() && isMaximized();
    settings.setValue(QStringLiteral("windowGeometry"), formatWindowGeometry(frame));
    settings.setValue(QStringLiteral("windowMaximized"), maximized);
    // Drop legacy binary keys so conf files stay readable.
    settings.remove(QStringLiteral("geometry"));
    settings.remove(QStringLiteral("normalGeometry"));

    settings.beginWriteArray(QStringLiteral("sessionHistory"), m_sessionHistory.size());
    for (int i = 0; i < m_sessionHistory.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("paths"), m_sessionHistory.at(i));
    }
    settings.endArray();
    settings.setValue(QStringLiteral("recentProjects"), m_recentProjects);
    settings.setValue(QStringLiteral("dockLayoutVersion"), kDockLayoutStateVersion);
    settings.setValue(QStringLiteral("dockLayoutState"), saveState());
    settings.remove(QStringLiteral("windowState"));
    settings.remove(QStringLiteral("windowStateVersion"));
    settings.remove(QStringLiteral("windowStateQt"));
    if (m_imageView) {
        settings.setValue(QStringLiteral("stickyZoomEnabled"),
                          m_imageView->hostFraming().isStickyZoomEnabled());
        settings.setValue(QStringLiteral("view/smoothScaling"),
                          DisplayQuality::smoothScaling());
        settings.setValue(QStringLiteral("stickyZoomKind"),
                          static_cast<int>(m_imageView->hostFraming().currentStickyZoomKind()));
    }
    if (m_adjustmentsDock) {
        settings.setValue(QStringLiteral("adjustmentsPanelVisible"),
                          m_adjustmentsDock->isVisible());
    }
    settings.setValue(QStringLiteral("toolBarVisible"),
                      isFullScreen() ? m_toolBarVisibleBeforeFullscreen
                                     : m_toolBar->isVisible());
    settings.setValue(QStringLiteral("locationBarPinned"), m_locationBarPinned);
    settings.setValue(QStringLiteral("searchBarPinned"), m_searchBarPinned);
    QString sortKey = QStringLiteral("name");
    switch (m_sortMode) {
    case SortMode::MTime: sortKey = QStringLiteral("mtime"); break;
    case SortMode::FileSize: sortKey = QStringLiteral("filesize"); break;
    case SortMode::Width: sortKey = QStringLiteral("width"); break;
    case SortMode::Height: sortKey = QStringLiteral("height"); break;
    case SortMode::PixelCount: sortKey = QStringLiteral("pixels"); break;
    case SortMode::Path: sortKey = QStringLiteral("path"); break;
    case SortMode::AspectRatio: sortKey = QStringLiteral("aspect"); break;
    case SortMode::Shuffle: sortKey = QStringLiteral("shuffle"); break;
    case SortMode::Name:
    default: sortKey = QStringLiteral("name"); break;
    }
    settings.setValue(QStringLiteral("sortMode"), sortKey);
    settings.setValue(QStringLiteral("slideshowIntervalMs"), m_slideshowIntervalMs);
    if (m_imageView) {
        settings.setValue(QStringLiteral("slideshowTransition"),
                          static_cast<int>(m_imageView->hostSlideshow().settings().currentTransition()));
        settings.setValue(QStringLiteral("slideshowTransitionDurationMs"),
                          m_imageView->hostSlideshow().settings().transitionDuration());
        settings.setValue(QStringLiteral("slideshowMotion"),
                          static_cast<int>(m_imageView->hostSlideshow().settings().currentMotion()));
        settings.setValue(QStringLiteral("slideshowPanZoomFactor"),
                          m_imageView->hostSlideshow().settings().currentPanZoomFactor());
        settings.setValue(QStringLiteral("slideshowZoomMode"),
                          static_cast<int>(m_imageView->hostSlideshow().settings().currentZoom()));
        settings.setValue(QStringLiteral("slideshowLetterboxFill"),
                          static_cast<int>(m_imageView->hostSlideshow().settings().currentLetterboxFill()));
        settings.setValue(QStringLiteral("slideshowPadColor"),
                          m_imageView->hostSlideshow().padColorForPaint().name(QColor::HexRgb));
    }
    settings.setValue(QStringLiteral("slideshowFullscreen"), m_slideshowFullscreen);
    settings.setValue(QStringLiteral("slideshowLoop"), m_slideshowLoop);
    if (m_imageView) {
        settings.setValue(QStringLiteral("masonryColumns"),
                          m_imageView->hostLayout().masonryColumnsValue());
        settings.setValue(QStringLiteral("masonryRows"),
                          m_imageView->hostLayout().masonryRowsValue());
        settings.setValue(QStringLiteral("backgroundColor"),
                          m_imageView->hostCanvasBg().primaryColor().name(QColor::HexRgb));
        settings.setValue(QStringLiteral("backgroundColorAlt"),
                          m_imageView->hostCanvasBg().altColor().name(QColor::HexRgb));
        settings.setValue(QStringLiteral("backgroundPattern"),
                          m_imageView->hostCanvasBg().currentPattern()
                                  == BackgroundPattern::Solid
                              ? QStringLiteral("solid")
                              : QStringLiteral("checkerboard"));
        settings.setValue(QStringLiteral("checkerboardWorkspaceOnly"),
                          m_imageView->hostCanvasBg().isCheckerWorkspaceOnly());
    }
    // Persist startup preference only — not the live session toggle
    settings.setValue(QStringLiteral("startInWorkspaceMode"), m_startInWorkspaceMode);
    settings.setValue(QStringLiteral("thumbnailsPreferredWorkspace"),
                      m_thumbnailsPreferredWorkspace);
    settings.setValue(QStringLiteral("thumbnailsPreferredGallery"),
                      m_thumbnailsPreferredGallery);
    settings.setValue(QStringLiteral("layoutPreferredInWorkspace"),
                      m_layoutPreferredInWorkspace);
    if (m_imageView) {
        settings.setValue(QStringLiteral("gridColumns"), m_imageView->hostLayout().gridColumnsValue());
        settings.setValue(QStringLiteral("masonryColumns"), m_imageView->hostLayout().masonryColumnsValue());
        settings.setValue(QStringLiteral("masonryRows"), m_imageView->hostLayout().masonryRowsValue());
    }
    if (m_imageView && m_imageView->isGalleryMode()) {
        settings.setValue(QStringLiteral("lastGalleryLayout"),
                          static_cast<int>(m_imageView->hostLayout().currentMode()));
    } else if (m_galleryReturnActive) {
        settings.setValue(QStringLiteral("lastGalleryLayout"),
                          static_cast<int>(m_galleryReturnLayout));
    }
    settings.remove(QStringLiteral("workspaceMode"));
    settings.setValue(QStringLiteral("scrollBarsVisible"),
                      m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked());
    if (m_thumbnailBar) {
        settings.setValue(QStringLiteral("thumbnailSize"), m_thumbnailBar->thumbSize());
        settings.setValue(QStringLiteral("thumbnailBarPosition"),
                          m_thumbnailEdge == ThumbnailEdge::Left
                              ? QStringLiteral("left")
                              : m_thumbnailEdge == ThumbnailEdge::Right
                                    ? QStringLiteral("right")
                                    : m_thumbnailEdge == ThumbnailEdge::Top
                                          ? QStringLiteral("top")
                                          : QStringLiteral("bottom"));
    }
    settings.remove(QStringLiteral("centralSplitter"));
    if (m_imageView) {
        settings.setValue(QStringLiteral("imageModeLeftDragPan"),
                          m_imageView->hostChrome().isImageModeLeftDragPan());
        settings.setValue(QStringLiteral("hudVisible"), m_imageView->hostHudPrefs().isVisible());
        settings.setValue(QStringLiteral("contentEditMarksVisible"),
                          ImageItem::contentEditMarksVisible());
        settings.setValue(QStringLiteral("hudFontPointSize"), m_imageView->hostHudPrefs().fontPointSizeValue());
        settings.setValue(QStringLiteral("hudTextColor"), m_imageView->hostHudPrefs().textColorRef().name(QColor::HexArgb));
        settings.setValue(QStringLiteral("hudPanelColor"), m_imageView->hostHudPrefs().panelColorRef().name(QColor::HexArgb));
    }
    if (m_thumbnailBar) {
        settings.setValue(QStringLiteral("thumbnailLabelsVisible"),
                          m_thumbnailBar->labelsVisible());
        settings.setValue(QStringLiteral("thumbnailCropToSquare"),
                          m_thumbnailBar->cropToSquare());
    }
}

void MainWindow::resetDockLayout()
{
    QSettings settings;
    settings.remove(QStringLiteral("dockLayoutState"));
    settings.remove(QStringLiteral("dockLayoutVersion"));
    settings.remove(QStringLiteral("windowState"));
    settings.remove(QStringLiteral("windowStateVersion"));
    settings.remove(QStringLiteral("windowStateQt"));

    // Built-in defaults: filmstrip bottom (mode prefs still apply later),
    // metadata right, help/toc left-ish as constructed, tool panels hidden.
    if (m_thumbnailDock) {
        removeDockWidget(m_thumbnailDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_thumbnailDock);
    }
    if (m_metadataDock) {
        removeDockWidget(m_metadataDock);
        addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
        m_metadataDock->show();
        if (m_toggleMetadataAct) {
            m_toggleMetadataAct->setChecked(true);
        }
    }
    if (m_helpDock) {
        removeDockWidget(m_helpDock);
        addDockWidget(Qt::RightDockWidgetArea, m_helpDock);
        m_helpDock->hide();
        if (m_toggleHelpAct) {
            m_toggleHelpAct->setChecked(false);
        }
    }
    if (m_tocDock) {
        removeDockWidget(m_tocDock);
        addDockWidget(Qt::LeftDockWidgetArea, m_tocDock);
        m_tocDock->hide();
        if (m_toggleTocAct) {
            m_toggleTocAct->setChecked(false);
        }
    }
    if (m_adjustmentsDock) {
        removeDockWidget(m_adjustmentsDock);
        addDockWidget(Qt::RightDockWidgetArea, m_adjustmentsDock);
        m_adjustmentsDock->hide();
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(false);
        }
        settings.setValue(QStringLiteral("adjustmentsPanelVisible"), false);
    }
    if (m_cropDock) {
        removeDockWidget(m_cropDock);
        addDockWidget(Qt::RightDockWidgetArea, m_cropDock);
        m_cropDock->hide();
        if (m_toggleCropAct) {
            m_toggleCropAct->setChecked(false);
        }
    }
    if (m_ocrDock) {
        removeDockWidget(m_ocrDock);
        addDockWidget(Qt::RightDockWidgetArea, m_ocrDock);
        m_ocrDock->hide();
        if (m_toggleOcrAct) {
            m_toggleOcrAct->setChecked(false);
        }
    }
    if (m_textDock) {
        removeDockWidget(m_textDock);
        addDockWidget(Qt::RightDockWidgetArea, m_textDock);
        m_textDock->hide();
        if (m_toggleTextAct) {
            m_toggleTextAct->setChecked(false);
        }
    }
    if (m_layoutDock) {
        removeDockWidget(m_layoutDock);
        addDockWidget(Qt::LeftDockWidgetArea, m_layoutDock);
        m_layoutDock->hide();
        m_layoutPreferredInWorkspace = false;
        settings.setValue(QStringLiteral("layoutPreferredInWorkspace"), false);
        if (m_toggleLayoutPanelAct) {
            m_toggleLayoutPanelAct->setChecked(false);
        }
    }
    updateLayoutPanelForMode();
    updateWorkspaceActionVisibility();
    statusBar()->showMessage(tr("Panel layout reset to defaults"), 3000);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_locationEdit && m_locationEdit->hasFocus()) {
            cancelLocationBar();
            event->accept();
            return;
        }
        if (m_searchEdit && m_searchEdit->hasFocus()) {
            cancelSearchBar();
            event->accept();
            return;
        }
        if (m_imageView && m_imageView->hostCrop().active()) {
            m_imageView->hostCrop().cancelCrop();
            event->accept();
            return;
        }
        if (isFullScreen()) {
            showNormal();
            event->accept();
            return;
        }
        if (m_imageView && m_imageView->isImageMode() && !m_session.paths().isEmpty()) {
            returnFromImageMode();
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmQuitOrClose()) {
        event->ignore();
        return;
    }
    writeSettings();
    QMainWindow::closeEvent(event);
}

bool MainWindow::workspaceHasUnsavedWork() const
{
    if (!m_workspaceDirty) {
        return false;
    }
    return m_imageView && m_imageView->hostWorkspace().hasContent();
}

void MainWindow::markWorkspaceDirty()
{
    m_workspaceDirty = true;
}

bool MainWindow::confirmQuitOrClose()
{
    if (!workspaceHasUnsavedWork()) {
        return true;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Save Workspace before closing?"));
    box.setText(tr("The Workspace has images that have not been saved to a project."));
    box.setInformativeText(
        tr("If you close without saving, your arrangement will be lost."));
    QPushButton *discardBtn = box.addButton(tr("Close without Saving"),
                                            QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
    QPushButton *saveBtn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    box.setDefaultButton(saveBtn);
    box.exec();
    if (box.clickedButton() == cancelBtn) {
        return false;
    }
    if (box.clickedButton() == saveBtn) {
        saveProject();
        // User cancelled Save As or save failed — keep the window open.
        if (workspaceHasUnsavedWork()) {
            return false;
        }
        return true;
    }
    Q_UNUSED(discardBtn);
    return true;
}

QStringList MainWindow::extractLocalImagePaths(const QMimeData *mime) const
{
    QStringList result;
    if (!mime || !mime->hasUrls()) {
        return result;
    }
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            result.append(url.toLocalFile());
        }
    }
    return result;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()) {
        return;
    }
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-paths"))
        || !extractLocalImagePaths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::handleWorkspaceDrop(const QStringList &paths, bool fromInternalSelection,
                                     const QPointF &scenePos, bool hasScenePos,
                                     const QList<qint64> &sessionIds)
{
    // External drops must not expand on the GUI thread (USB/NFS stats).
    // Append via background expand; placement for novel files follows session
    // membership after applyExpandedAppend (drop-point placement is best-effort
    // for paths already in-session via the loop below).
    if (!fromInternalSelection && pathsNeedBackgroundExpand(paths)) {
        expandPathsInBackground(paths, /*append=*/true);
        return;
    }
    const QStringList expanded = fromInternalSelection ? paths : expandPaths(paths);
    if (expanded.isEmpty()) {
        return;
    }
    QStringList novel;
    for (const QString &p : expanded) {
        if (!m_session.paths().contains(p)) {
            novel.append(p);
        }
    }
    if (!novel.isEmpty()) {
        appendFiles(novel);
    }
    int i = 0;
    for (const QString &img : expanded) {
        SessionImageId sid = kInvalidSessionImageId;
        int slot = -1;
        // Prefer identity from the filmstrip drag payload (duplicate-safe).
        if (i < sessionIds.size() && sessionIds.at(i) != 0
            && sessionIds.at(i) != static_cast<qint64>(kInvalidSessionImageId)) {
            sid = static_cast<SessionImageId>(sessionIds.at(i));
            slot = m_session.indexOfId(sid);
        }
        const SessionImageId sourceSid = sid;
        // Prefer session-id membership over path counts (duplicates share a path).
        const bool alreadyOnCanvas = (sourceSid != kInvalidSessionImageId)
            ? (m_imageView->findItemBySessionId(sourceSid) != nullptr)
            : (m_imageView->hostWorkspace().pathOccurrenceCount(img) > 0);
        // Session image already on the canvas: allocate a new session image
        // (drop-duplicate) and copy content appearance so a cropped filmstrip
        // drag does not place a full-frame / wrong-looking twin.
        if (alreadyOnCanvas && (slot < 0 || m_imageView->findItemBySessionId(sid))) {
            m_session.append(img);
            const SessionImageId newSid = m_session.ids().isEmpty()
                ? kInvalidSessionImageId
                : m_session.ids().last();
            if (sourceSid != kInvalidSessionImageId && newSid != kInvalidSessionImageId) {
                m_imageView->hostImage().copySessionAppearance(sourceSid, newSid);
            }
            if (m_thumbnailBar) {
                m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
                m_thumbnailBar->setMultiSelectEnabled(true);
            }
            slot = m_session.size() - 1;
            sid = newSid;
        } else if (slot < 0) {
            slot = m_session.lastIndexOfPath(img);
            sid = sessionIdAt(slot);
        }
        if (hasScenePos) {
            const QPointF pos = scenePos + QPointF(28.0 * i, 22.0 * i);
            if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
                dbg && dbg[0] != '\0' && dbg[0] != '0') {
                fprintf(stderr,
                        "biltoo/drop: placeOrMove path=%s sid=%lld slot=%d "
                        "pos=(%.1f,%.1f) alreadyOnCanvas=%d\n",
                        qPrintable(img), static_cast<long long>(sid), slot,
                        pos.x(), pos.y(), alreadyOnCanvas ? 1 : 0);
            }
            // placeOrMoveImageAt owns identity via PendingSessionBind / move-by-id.
            // Do NOT fan-out one SessionImageId onto every selected tile — that
            // currently selected tile and created duplicate SessionImageIds.
            m_imageView->hostWorkspace().placeOrMoveImageAt(img, pos, sid, slot);
        } else {
            if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
                dbg && dbg[0] != '\0' && dbg[0] != '0') {
                fprintf(stderr,
                        "biltoo/drop: NO scene pos — addImageForSession path=%s "
                        "sid=%lld (appearance may restore old pose)\n",
                        qPrintable(img), static_cast<long long>(sid));
            }
            m_imageView->hostWorkspace().addImageForSession(img, sid, slot);
        }
        ++i;
    }
    // Defer membership sync — rebind during the drop stack asserted under
    // Qt 6.11 ("destructor may have already run" / wrong type).
    QTimer::singleShot(0, this, [this]() {
        if (!isWorkspaceMode()) {
            return;
        }
        syncThumbnailCanvasMembership();
        if (m_session.paths().size() > 1 && m_thumbnailBar
            && !(m_thumbnailDock ? m_thumbnailDock->isVisible()
                                : m_thumbnailBar->isVisible())) {
            m_toggleThumbnailBarAct->setChecked(true);
            if (m_thumbnailDock) {
                m_thumbnailDock->setVisible(true);
            } else {
                m_thumbnailBar->setVisible(true);
            }
        }
        updateStatus();
    });
    return;
}

void MainWindow::handleGalleryDrop(const QStringList &paths, bool fromInternalSelection,
                                   const QList<qint64> &sessionIds,
                                   const QPointF &scenePos, bool hasScenePos)
{
    // Filmstrip → Gallery: reorder the session (do not append duplicates).
    if (fromInternalSelection) {
        QList<int> rows;
        QSet<int> seen;
        if (!sessionIds.isEmpty()) {
            for (qint64 raw : sessionIds) {
                const SessionImageId sid = static_cast<SessionImageId>(raw);
                if (sid == kInvalidSessionImageId) {
                    continue;
                }
                const int idx = indexOfSessionId(sid);
                if (idx >= 0 && !seen.contains(idx)) {
                    seen.insert(idx);
                    rows.append(idx);
                }
            }
        }
        if (rows.isEmpty()) {
            for (const QString &p : paths) {
                const int idx = m_session.indexOfPathPreferId(p);
                if (idx >= 0 && !seen.contains(idx)) {
                    seen.insert(idx);
                    rows.append(idx);
                }
            }
        }
        if (rows.isEmpty()) {
            return;
        }
        std::sort(rows.begin(), rows.end());
        int insertBefore = m_session.size();
        if (hasScenePos && m_imageView) {
            insertBefore = galleryReorderInsertBefore(scenePos);
        }
        reorderSessionRows(rows, insertBefore);
        return;
    }

    // Archives/PDF need the same background expand + centre HUD as File→Open.
    if (pathsNeedBackgroundExpand(paths)) {
        expandPathsInBackground(paths, /*append=*/true);
        return;
    }
    const QStringList expanded = expandPaths(paths);
    if (expanded.isEmpty()) {
        return;
    }
    for (const QString &p : expanded) {
        if (p.isEmpty()) {
            continue;
        }
        m_session.append(p);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    }
    applyThumbnailVisibility();
    // Already in Gallery — populate only. enterGallery would re-pack and race
    // the size-first gate (provisional / square first cell).
    populateGalleryCanvas();
    updateStatus();
}

int MainWindow::galleryReorderInsertBefore(const QPointF &scenePos) const
{
    if (!m_imageView || !m_imageView->canvasScene()) {
        return m_session.size();
    }
    ImageItem *best = nullptr;
    qreal bestDist2 = 1e300;
    for (ImageItem *it : m_imageView->liveItems()) {
        if (!it) {
            continue;
        }
        const QRectF br = it->sceneBoundingRect();
        if (br.contains(scenePos)) {
            const int idx = m_imageView->sessionListIndex(it);
            if (idx < 0) {
                continue;
            }
            // Left/top half → insert before; right/bottom half → after.
            const bool after = (scenePos.x() > br.center().x())
                || (qAbs(scenePos.x() - br.center().x()) < 1.0
                    && scenePos.y() > br.center().y());
            return after ? idx + 1 : idx;
        }
        const QPointF c = br.center();
        const qreal dx = c.x() - scenePos.x();
        const qreal dy = c.y() - scenePos.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best = it;
        }
    }
    if (!best) {
        return m_session.size();
    }
    const int idx = m_imageView->sessionListIndex(best);
    if (idx < 0) {
        return m_session.size();
    }
    const QRectF br = best->sceneBoundingRect();
    const bool after = scenePos.x() > br.center().x()
        || (qAbs(scenePos.x() - br.center().x()) < 1.0
            && scenePos.y() > br.center().y());
    return after ? idx + 1 : idx;
}

void MainWindow::handleImageModeDrop(const QStringList &paths, bool fromInternalSelection,
                                     const QList<qint64> &sessionIds)
{
    // Image mode: always append to the session (Open still replaces).
    // Drops from the thumbnail bar are already in the session — just navigate
    // to the first path instead of wiping the session down to one file.
    if (!fromInternalSelection && pathsNeedBackgroundExpand(paths)) {
        expandPathsInBackground(paths, /*append=*/true);
        return;
    }
    const QStringList expanded = fromInternalSelection ? paths : expandPaths(paths);
    if (expanded.isEmpty()) {
        return;
    }
    QStringList novel;
    for (const QString &p : expanded) {
        if (!m_session.paths().contains(p)) {
            novel.append(p);
        }
    }
    if (!novel.isEmpty()) {
        appendFiles(novel);
    }

    // Prefer SessionImageId so duplicate paths keep the intended row.
    // Filmstrip internal drops carry parallel sessionIds; novel external drops
    // land at lastIndexOfPath after append.
    int idx = -1;
    if (!sessionIds.isEmpty()) {
        const SessionImageId sid = static_cast<SessionImageId>(sessionIds.first());
        if (sid != kInvalidSessionImageId) {
            idx = indexOfSessionId(sid);
        }
    }
    if (idx < 0) {
        const QString focus = expanded.first();
        if (novel.contains(focus)) {
            idx = m_session.lastIndexOfPath(focus);
        } else {
            const SessionImageId sid = m_session.firstIdForPath(focus);
            if (sid != kInvalidSessionImageId) {
                idx = indexOfSessionId(sid);
            }
            if (idx < 0) {
                idx = indexOfPathPreferId(focus);
            }
        }
    }
    if (idx >= 0) {
        setCurrentIndex(idx);
    }
}

void MainWindow::handleDroppedUrls(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                   const QPointF &scenePos, bool hasScenePos,
                                   const QList<qint64> &sessionIds,
                                   const QStringList &internalPaths)
{
    // Prefer explicit internal paths from the filmstrip (exact selection,
    // including //archive: members). URL-only drops still go through
    // extractLocalImagePaths + expandPaths (directories / archive files).
    QStringList paths = internalPaths;
    paths.removeAll(QString());
    bool fromInternalSelection = !paths.isEmpty();
    if (!fromInternalSelection) {
        QMimeData mime;
        mime.setUrls(urls);
        paths = extractLocalImagePaths(&mime);
    }
    if (paths.isEmpty()) {
        return;
    }

    // Workspace mode: place each drop as a canvas instance at the drop point.
    // Paths already on the canvas are *duplicated* (not moved); a new session
    // slot is appended so the filmstrip can address the copy independently.
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/drop: handle mode=W%d G%d I%d hasPos=%d scene=(%.1f,%.1f) "
                "paths=%lld sessionIds=%lld internal=%lld\n",
                isWorkspaceMode() ? 1 : 0, isGalleryMode() ? 1 : 0,
                (m_imageView && m_imageView->isImageMode()) ? 1 : 0,
                hasScenePos ? 1 : 0, scenePos.x(), scenePos.y(),
                static_cast<long long>(paths.size()),
                static_cast<long long>(sessionIds.size()),
                static_cast<long long>(internalPaths.size()));
    }
    if (isWorkspaceMode()) {
        handleWorkspaceDrop(paths, fromInternalSelection, scenePos, hasScenePos, sessionIds);
        return;
    }
    // Empty session + external drop: same path as CLI / File→Open (centre HUD,
    // size-first Gallery for multi-image archives). Image-mode append alone
    // loaded the first leaf with a square provisional size and skipped the gate.
    if (m_session.paths().isEmpty() && !fromInternalSelection) {
        loadFiles(paths);
        return;
    }
    if (isGalleryMode()) {
        handleGalleryDrop(paths, fromInternalSelection, sessionIds, scenePos, hasScenePos);
        return;
    }
    handleImageModeDrop(paths, fromInternalSelection, sessionIds);
    Q_UNUSED(modifiers);
}





void MainWindow::onFilesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                const QPointF &scenePos, bool hasScenePos,
                                const QList<qint64> &sessionIds,
                                const QStringList &internalPaths)
{
    handleDroppedUrls(urls, modifiers, scenePos, hasScenePos, sessionIds, internalPaths);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()) {
        return;
    }
    const QByteArray pathBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-paths"));
    const bool hasInternal = !pathBytes.isEmpty();
    if (!event->mimeData()->hasUrls() && !hasInternal) {
        return;
    }
    QList<qint64> sessionIds;
    const QByteArray idBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-session-ids"));
    if (!idBytes.isEmpty()) {
        for (const QByteArray &tok : idBytes.split(',')) {
            bool ok = false;
            const qint64 v = tok.trimmed().toLongLong(&ok);
            sessionIds.append(ok ? v : 0);
        }
    }
    QStringList internalPaths;
    if (hasInternal) {
        internalPaths = QString::fromUtf8(pathBytes).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    // Prefer mapping through the canvas when the cursor is over ImageView —
    // filmstrip→Workspace drops often land on MainWindow if the OpenGL
    // viewport does not deliver Drop to ImageView::dropEvent.
    QPointF scenePos;
    bool hasScenePos = false;
    if (m_imageView && m_imageView->viewport()) {
        const QPoint viewPos = m_imageView->viewport()->mapFromGlobal(QCursor::pos());
        if (m_imageView->viewport()->rect().contains(viewPos)) {
            scenePos = m_imageView->mapToScene(viewPos);
            hasScenePos = true;
        }
    }
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr, "biltoo/drop: MainWindow::dropEvent hasPos=%d scene=(%.1f,%.1f)\n",
                hasScenePos ? 1 : 0, scenePos.x(), scenePos.y());
    }
    handleDroppedUrls(event->mimeData()->urls(), event->modifiers(), scenePos,
                      hasScenePos, sessionIds, internalPaths);
    event->acceptProposedAction();
}

void MainWindow::setDualCompareEnabled(bool on)
{
    if (!m_dualShell || !m_imageView) {
        return;
    }
    if (on) {
        // Dual is Image-mode compare only (not Gallery pack Facing).
        if (!m_imageView->isImageMode()) {
            m_imageView->setViewMode(ImageView::ViewMode::Image);
        }
    }
    m_dualShell->setDualEnabled(on, &m_session, &m_session.seedBook());
    if (m_dualCompareAct) {
        m_dualCompareAct->setChecked(m_dualShell->isDualEnabled());
    }
    if (!on || !m_dualShell->isDualEnabled()) {
        return;
    }
    // Secondary key/edge nav uses the same goPrevious/goNext slots; those
    // route to navigateSecondary when the secondary pane is active.
    if (ImageView *sec = m_dualShell->secondary()) {
        connect(sec, &ImageView::navigatePreviousRequested, this, &MainWindow::goPrevious,
                Qt::UniqueConnection);
        connect(sec, &ImageView::navigateNextRequested, this, &MainWindow::goNext,
                Qt::UniqueConnection);
    }
    if (m_session.isEmpty()) {
        return;
    }
    // Stage 2c.3: seed secondary with the next session row (wrap; same if n==1).
    // Defer until the splitter has a non-zero geometry so fit/framing sees a
    // real viewport (open-on-enable used to race a 0-size secondary).
    const int n = m_session.size();
    int seed = m_currentIndex;
    if (seed < 0 || seed >= n) {
        seed = 0;
    }
    const int other = (n > 1) ? (seed + 1) % n : seed;
    const QString path = m_session.paths().at(other);
    const SessionImageId sid = sessionIdAt(other);
    // Wait for splitter layout so secondary has a non-zero viewport.
    QTimer::singleShot(0, this, [this, path, sid]() {
        if (!m_dualShell || !m_dualShell->isDualEnabled()) {
            return;
        }
        m_dualShell->openOnSecondary(path, sid);
        // Second pass after paint/layout (OpenGL→software viewport + soft seed).
        QTimer::singleShot(100, this, [this, path, sid]() {
            if (!m_dualShell || !m_dualShell->isDualEnabled()) {
                return;
            }
            if (ImageView *sec = m_dualShell->secondary()) {
                if (sec->itemCount() == 0
                    || !sec->primaryItem()
                    || !sec->primaryItem()->hasDisplayPixels()) {
                    m_dualShell->openOnSecondary(path, sid);
                }
            }
        });
    });
}



void MainWindow::connectTextPanel()
{
    if (!m_textPanel || !m_imageView) {
        return;
    }
    // Idempotent: disconnect previous then reconnect.
    disconnect(m_textPanel, nullptr, this, nullptr);
    TextLayerController &text = m_imageView->hostText();
    disconnect(&text, nullptr, m_textPanel, nullptr);
    disconnect(&text, nullptr, this, nullptr);

    connect(m_textPanel, &TextPanel::selectionRegionsChanged, this,
            [this](const QVector<int> &ids) {
                if (!m_imageView) {
                    return;
                }
                m_imageView->hostText().setSelectedRegions(ids);
            });
    connect(m_textPanel, &TextPanel::hoverRegionChanged, this,
            [this](int regionIndex) {
                if (!m_imageView) {
                    return;
                }
                m_imageView->hostText().setHoverRegion(regionIndex);
            });
    connect(m_textPanel, &TextPanel::showGlyphsToggled, this, [this](bool on) {
        if (!m_imageView) {
            return;
        }
        m_imageView->hostText().setShowGlyphs(on);
        if (on && m_showTextRegionsAct) {
            // Glyphs are most useful with outlines visible; do not force outlines.
        }
    });
    connect(m_textPanel, &TextPanel::showOutlinesToggled, this, [this](bool on) {
        if (m_showTextRegionsAct) {
            m_showTextRegionsAct->setChecked(on);
        } else if (m_imageView) {
            m_imageView->hostText().setShowRegions(on);
        }
    });
    connect(m_textPanel, &TextPanel::refreshRequested, this, [this]() {
        if (!m_imageView) {
            return;
        }
        m_imageView->hostText().refresh();
        updateTextPanel();
    });

    connect(&text, &TextLayerController::layerChanged, this, [this]() {
        updateTextPanel();
    });
    connect(&text, &TextLayerController::selectionChanged, this, [this]() {
        if (!m_textPanel || !m_imageView) {
            return;
        }
        m_textPanel->setSelectedRegions(
            m_imageView->hostText().session().selectedRegionsRef());
    });
    connect(&text, &TextLayerController::hoverChanged, this,
            [this](int regionIndex) {
                if (m_textPanel) {
                    m_textPanel->setHoverRegion(regionIndex);
                }
            });
}

void MainWindow::updateTextPanel()
{
    if (!m_textPanel) {
        return;
    }
    if (!m_imageView) {
        m_textPanel->clearLayer();
        m_textPanel->setLayerInfo(tr("No image view"));
        return;
    }
    TextLayerController &text = m_imageView->hostText();
    const auto &sess = text.session();
    const QString path = m_imageView->hostImage().classicPath();
    // Load once if panel needs data for a new path. Never call refresh while
    // handling layerChanged — that would recurse: refresh → emit → updateTextPanel.
    if (!path.isEmpty() && sess.layerPathRef() != path) {
        QSignalBlocker blockSignals(&text);
        text.refresh();
    }
    m_textPanel->setShowGlyphsChecked(sess.showsGlyphs());
    m_textPanel->setShowOutlinesChecked(sess.showsRegions());
    const auto &layer = sess.layerRef();
    m_textPanel->setLayer(layer);
    const int n = layer.regions.size();
    m_textPanel->setLayerInfo(
        tr("%1 — %n region(s)", "", n)
            .arg(path.isEmpty() ? tr("(no path)") : QFileInfo(path).fileName()));
    m_textPanel->setSelectedRegions(sess.selectedRegionsRef());
}

void MainWindow::cancelOcrBatch()
{
    m_ocrGeneration.fetch_add(1);
    m_ocrRunning = false;
    if (m_ocrCancelAct) {
        m_ocrCancelAct->setEnabled(false);
    }
    if (m_ocrPageAct) {
        m_ocrPageAct->setEnabled(true);
    }
    if (m_ocrForcePageAct) {
        m_ocrForcePageAct->setEnabled(true);
    }
    if (m_ocrDocumentAct) {
        m_ocrDocumentAct->setEnabled(true);
    }
    if (m_ocrPanel) {
        m_ocrPanel->setBusy(false);
        m_ocrPanel->clearProgress();
        m_ocrPanel->setSummary(tr("Cancelled"));
        m_ocrPanel->appendLog(tr("Cancelled by user"));
    }
    if (m_imageView) {
        m_imageView->hostShell().clearCentreProgress();
    }
    statusBar()->showMessage(tr("OCR cancelled"), 3000);
}

void MainWindow::ocrCurrentPage()
{
    if (m_ocrPanel) {
        if (m_ocrDock && !m_ocrDock->isVisible()) {
            m_ocrDock->show();
            m_ocrDock->raise();
        }
        startOcrCurrentPage(m_ocrPanel->force());
        return;
    }
    startOcrCurrentPage(/*force=*/false);
}

void MainWindow::ocrCurrentPageForced()
{
    if (m_ocrPanel) {
        m_ocrPanel->setForce(true);
        if (m_ocrDock && !m_ocrDock->isVisible()) {
            m_ocrDock->show();
            m_ocrDock->raise();
        }
    }
    startOcrCurrentPage(/*force=*/true);
}

void MainWindow::runOcrFromPanel()
{
    if (!m_ocrPanel) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("ocr/lang"), m_ocrPanel->language());
    settings.setValue(QStringLiteral("ocr/jobs"), m_ocrPanel->jobs());
    if (m_ocrPanel->scope() == OcrPanel::Scope::Document) {
        ocrDocument();
    } else {
        startOcrCurrentPage(m_ocrPanel->force());
    }
}

void MainWindow::updateOcrPanel()
{
    if (!m_ocrPanel) {
        return;
    }
    if (!m_imageView) {
        m_ocrPanel->setLayerInfo(tr("No image view"));
        return;
    }
    const QString path = m_imageView->hostImage().classicPath();
    if (path.isEmpty()) {
        m_ocrPanel->setLayerInfo(tr("No image selected"));
        return;
    }
    const int page = PagePath::pageNumber(path);
    const auto native = ThumtooCache::cachedPageTextLayer(path);
    const auto ocr = ThumtooCache::cachedOcrPageTextLayer(path);
    const QString engine = ThumtooCache::ocrAvailable()
        ? tr("Tesseract: available")
        : tr("Tesseract: not available in this build");
    const QString where = page > 0
        ? tr("Page %1").arg(page)
        : QFileInfo(path).fileName();
    m_ocrPanel->setLayerInfo(
        tr("%1\nNative text: %2 region(s)\nOCR text: %3 region(s)\n%4")
            .arg(where)
            .arg(native.regions.size())
            .arg(ocr.regions.size())
            .arg(engine));
}

void MainWindow::startOcrCurrentPage(bool force)
{
    if (!m_imageView) {
        return;
    }
    if (m_ocrRunning) {
        statusBar()->showMessage(tr("OCR already running — cancel first"), 3000);
        if (m_ocrPanel) {
            m_ocrPanel->appendLog(tr("Ignored: a job is already running"));
        }
        return;
    }
    const QString path = m_imageView->hostImage().classicPath();
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("No image selected for OCR"), 4000);
        if (m_ocrPanel) {
            m_ocrPanel->appendLog(tr("Error: no current image path"));
            m_ocrPanel->setSummary(tr("No image selected"));
        }
        return;
    }
    QSettings settings;
    QString lang = m_ocrPanel ? m_ocrPanel->language()
                              : settings.value(QStringLiteral("ocr/lang"), QStringLiteral("eng")).toString();
    settings.setValue(QStringLiteral("ocr/lang"), lang);

    const int gen = m_ocrGeneration.fetch_add(1) + 1;
    m_ocrRunning = true;
    if (m_ocrCancelAct) {
        m_ocrCancelAct->setEnabled(true);
    }
    if (m_ocrPageAct) {
        m_ocrPageAct->setEnabled(false);
    }
    if (m_ocrForcePageAct) {
        m_ocrForcePageAct->setEnabled(false);
    }
    if (m_ocrDocumentAct) {
        m_ocrDocumentAct->setEnabled(false);
    }
    if (m_ocrPanel) {
        m_ocrPanel->setBusy(true);
        m_ocrPanel->setProgress(-1, force ? tr("Re-OCR this page…") : tr("OCR this page…"));
        m_ocrPanel->setSummary(tr("Working…"));
        m_ocrPanel->appendLog(
            tr("Start page OCR (force=%1, lang=%2, page=%3): %4")
                .arg(force ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(lang.isEmpty() ? QStringLiteral("eng") : lang)
                .arg(PagePath::pageNumber(path))
                .arg(path));
    }
    statusBar()->showMessage(force ? tr("Re-OCR in progress…") : tr("OCR in progress…"));
    m_imageView->hostShell().setCentreProgress(tr("OCR"), tr("This page…"));

    QPointer<MainWindow> self(this);
    const QString pathCopy = path;
    const QString langCopy = lang;
    QThreadPool::globalInstance()->start([self, pathCopy, langCopy, gen, force]() {
        const ThumtooCache::OcrRunResult result =
            ThumtooCache::runOcrPageTextLayer(pathCopy, force, langCopy);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, pathCopy, result, gen, force]() {
            MainWindow *host = self.data();
            if (!host || gen != host->m_ocrGeneration.load()) {
                return;
            }
            host->m_ocrRunning = false;
            if (host->m_ocrCancelAct) {
                host->m_ocrCancelAct->setEnabled(false);
            }
            if (host->m_ocrPageAct) {
                host->m_ocrPageAct->setEnabled(true);
            }
            if (host->m_ocrForcePageAct) {
                host->m_ocrForcePageAct->setEnabled(true);
            }
            if (host->m_ocrDocumentAct) {
                host->m_ocrDocumentAct->setEnabled(true);
            }
            if (host->m_ocrPanel) {
                host->m_ocrPanel->setBusy(false);
                host->m_ocrPanel->clearProgress();
            }
            if (!host->m_imageView) {
                return;
            }
            host->m_imageView->hostShell().clearCentreProgress();
            const ThumtooCache::PageTextLayer &layer = result.layer;
            const bool ok = result.status == ThumtooCache::OcrRunResult::Status::Ok;
            if (host->m_imageView->hostImage().classicPath() == pathCopy && ok
                && !layer.regions.isEmpty()) {
                host->m_imageView->hostText().installLayer(layer, pathCopy);
                if (host->m_showTextRegionsAct) {
                    host->m_showTextRegionsAct->setChecked(true);
                }
            }
            const QString msg = result.message();
            host->statusBar()->showMessage(msg, ok ? 5000 : 8000);
            if (host->m_ocrPanel) {
                host->m_ocrPanel->setSummary(msg);
                host->m_ocrPanel->appendLog(
                    ok ? host->tr("Page done (force=%1): %2 — %3")
                             .arg(force ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(msg)
                             .arg(pathCopy)
                       : host->tr("Page failed (force=%1): %2 — %3")
                             .arg(force ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(msg)
                             .arg(pathCopy));
            }
            host->updateOcrPanel();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::ocrDocument()
{
    if (!m_imageView) {
        return;
    }
    if (m_ocrRunning) {
        statusBar()->showMessage(tr("OCR already running — cancel first"), 3000);
        if (m_ocrPanel) {
            m_ocrPanel->appendLog(tr("Ignored: a job is already running"));
        }
        return;
    }
    const QStringList pages = documentPagePathsForSearch();
    if (pages.isEmpty()) {
        statusBar()->showMessage(tr("No document pages to OCR"), 4000);
        if (m_ocrPanel) {
            m_ocrPanel->appendLog(tr("Error: no multipage document pages"));
            m_ocrPanel->setSummary(tr("No document pages"));
        }
        return;
    }

    QSettings settings;
    QString lang;
    bool force = false;
    int jobs = 2;
    if (m_ocrPanel) {
        lang = m_ocrPanel->language();
        force = m_ocrPanel->force();
        jobs = m_ocrPanel->jobs();
        if (m_ocrDock && !m_ocrDock->isVisible()) {
            m_ocrDock->show();
            m_ocrDock->raise();
        }
    } else {
        lang = settings.value(QStringLiteral("ocr/lang"), QStringLiteral("eng")).toString();
        bool okDialog = false;
        lang = QInputDialog::getText(
            this, tr("OCR Document"),
            tr("Tesseract language code(s):"),
            QLineEdit::Normal, lang, &okDialog);
        if (!okDialog) {
            return;
        }
        lang = lang.trimmed();
        if (lang.isEmpty()) {
            lang = QStringLiteral("eng");
        }
        force = QMessageBox::question(
                    this, tr("OCR Document"),
                    tr("Force re-OCR of pages that already have an OCR layer?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No)
            == QMessageBox::Yes;
        jobs = qBound(1, settings.value(QStringLiteral("ocr/jobs"), 2).toInt(), 4);
    }
    settings.setValue(QStringLiteral("ocr/lang"), lang);
    settings.setValue(QStringLiteral("ocr/jobs"), jobs);

    const int gen = m_ocrGeneration.fetch_add(1) + 1;
    m_ocrRunning = true;
    if (m_ocrCancelAct) {
        m_ocrCancelAct->setEnabled(true);
    }
    if (m_ocrPageAct) {
        m_ocrPageAct->setEnabled(false);
    }
    if (m_ocrForcePageAct) {
        m_ocrForcePageAct->setEnabled(false);
    }
    if (m_ocrDocumentAct) {
        m_ocrDocumentAct->setEnabled(false);
    }
    if (m_ocrPanel) {
        m_ocrPanel->setBusy(true);
        m_ocrPanel->setProgress(0, tr("Starting document OCR…"));
        m_ocrPanel->setSummary(tr("0 / %1 pages").arg(pages.size()));
        m_ocrPanel->appendLog(
            tr("Start document OCR (%1 pages, jobs=%2, force=%3, lang=%4)")
                .arg(pages.size())
                .arg(jobs)
                .arg(force ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(lang));
    }

    QPointer<MainWindow> self(this);
    const QStringList pagesCopy = pages;
    const QString langCopy = lang;
    QThreadPool::globalInstance()->start([self, pagesCopy, langCopy, gen, force, jobs]() {
        const int total = pagesCopy.size();
        std::atomic<int> nextIndex{0};
        std::atomic<int> doneCount{0};
        std::atomic<int> okCount{0};
        std::atomic<int> failCount{0};

        auto reportProgress = [self, total](int done, int okN, int failN, int pageNo) {
            MainWindow *host = self.data();
            if (!host) {
                return;
            }
            QMetaObject::invokeMethod(host, [self, done, total, okN, failN, pageNo]() {
                MainWindow *h = self.data();
                if (!h) {
                    return;
                }
                const int pct = total > 0 ? (done * 100) / total : 0;
                const QString caption =
                    h->tr("Page %1 — %2/%3 done (%4 ok, %5 failed)")
                        .arg(pageNo)
                        .arg(done)
                        .arg(total)
                        .arg(okN)
                        .arg(failN);
                if (h->m_imageView) {
                    h->m_imageView->hostShell().setCentreProgress(
                        h->tr("OCR document"), caption);
                }
                h->statusBar()->showMessage(
                    h->tr("OCR document: %1 / %2 (%3 ok)")
                        .arg(done)
                        .arg(total)
                        .arg(okN));
                if (h->m_ocrPanel) {
                    h->m_ocrPanel->setProgress(pct, caption);
                    h->m_ocrPanel->setSummary(
                        h->tr("%1 / %2 pages — %3 ok, %4 failed")
                            .arg(done)
                            .arg(total)
                            .arg(okN)
                            .arg(failN));
                }
            }, Qt::QueuedConnection);
        };

        auto worker = [self, &pagesCopy, &langCopy, gen, force, total,
                       &nextIndex, &doneCount, &okCount, &failCount,
                       reportProgress]() {
            while (true) {
                MainWindow *host = self.data();
                if (!host || gen != host->m_ocrGeneration.load()) {
                    return;
                }
                const int i = nextIndex.fetch_add(1);
                if (i >= total) {
                    return;
                }
                const QString &pagePath = pagesCopy.at(i);
                const int pageNo = PagePath::pageNumber(pagePath);
                const ThumtooCache::OcrRunResult result =
                    ThumtooCache::runOcrPageTextLayer(pagePath, force, langCopy);
                const bool pageOk =
                    result.status == ThumtooCache::OcrRunResult::Status::Ok
                    || result.status == ThumtooCache::OcrRunResult::Status::EmptyText;
                if (pageOk) {
                    okCount.fetch_add(1);
                } else {
                    failCount.fetch_add(1);
                    if (failCount.load() <= 5) {
                        const QString detail = result.message();
                        const int pn = pageNo;
                        MainWindow *notify = self.data();
                        if (notify) {
                            QMetaObject::invokeMethod(notify, [self, pn, detail]() {
                                MainWindow *h = self.data();
                                if (h && h->m_ocrPanel) {
                                    h->m_ocrPanel->appendLog(
                                        h->tr("Page %1: %2").arg(pn).arg(detail));
                                }
                            }, Qt::QueuedConnection);
                        }
                    }
                }
                const int done = doneCount.fetch_add(1) + 1;
                reportProgress(done, okCount.load(), failCount.load(), pageNo);
            }
        };

        const int nWorkers = qMin(jobs, total);
        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(nWorkers));
        for (int w = 0; w < nWorkers; ++w) {
            threads.emplace_back(worker);
        }
        for (std::thread &th : threads) {
            th.join();
        }

        MainWindow *host = self.data();
        if (!host) {
            return;
        }
        const int okFinal = okCount.load();
        const int failFinal = failCount.load();
        QMetaObject::invokeMethod(host, [self, gen, okFinal, failFinal, total]() {
            MainWindow *h = self.data();
            if (!h || gen != h->m_ocrGeneration.load()) {
                return;
            }
            h->m_ocrRunning = false;
            if (h->m_ocrCancelAct) {
                h->m_ocrCancelAct->setEnabled(false);
            }
            if (h->m_ocrPageAct) {
                h->m_ocrPageAct->setEnabled(true);
            }
            if (h->m_ocrForcePageAct) {
                h->m_ocrForcePageAct->setEnabled(true);
            }
            if (h->m_ocrDocumentAct) {
                h->m_ocrDocumentAct->setEnabled(true);
            }
            if (h->m_ocrPanel) {
                h->m_ocrPanel->setBusy(false);
                h->m_ocrPanel->clearProgress();
            }
            if (h->m_imageView) {
                h->m_imageView->hostShell().clearCentreProgress();
                h->m_imageView->hostText().refresh();
                if (h->m_showTextRegionsAct) {
                    h->m_showTextRegionsAct->setChecked(true);
                }
            }
            QString msg;
            if (failFinal > 0) {
                msg = h->tr("OCR document finished — %1 ok, %2 failed / %3 pages")
                          .arg(okFinal)
                          .arg(failFinal)
                          .arg(total);
            } else {
                msg = h->tr("OCR document finished — %1 / %2 pages")
                          .arg(okFinal)
                          .arg(total);
            }
            h->statusBar()->showMessage(msg, 8000);
            if (h->m_ocrPanel) {
                h->m_ocrPanel->setSummary(msg);
                h->m_ocrPanel->appendLog(msg);
            }
            h->updateOcrPanel();
        }, Qt::QueuedConnection);
    });
}


