// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QtMath>
#include <atomic>
#include <cmath>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    m_undoStack = new QUndoStack(this);
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<quint64>("quint64");

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hudVisible || m_hudFlashVisible) {
            viewport()->update();
        }
    });

    m_hudFlashTimer = new QTimer(this);
    m_hudFlashTimer->setSingleShot(true);
    m_layoutDebounceTimer = new QTimer(this);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(0);
    connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
        if (isGalleryMode() && !m_items.isEmpty()) {
            applyLayout();
        }
    });
    connect(m_hudFlashTimer, &QTimer::timeout, this, [this]() {
        m_hudFlashVisible = false;
        m_hudAction.clear();
        m_hudDetail.clear();
        viewport()->update();
    });

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setAcceptDrops(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(36, 36, 36)));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAlignment(Qt::AlignCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

ImageView::~ImageView() = default;

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image)
{
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == LoadAdd || role == LoadRestore) {
        m_pendingWorkspacePaths.insert(path);
    }
    const quint64 gen = ++m_loadGeneration;
    QThreadPool::globalInstance()->start([this, path, role, gen]() {
        const QImage image = ImageLoader::load(path);
        QMetaObject::invokeMethod(this, "onImageLoaded", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(QImage, image),
                                  Q_ARG(quint64, gen),
                                  Q_ARG(int, static_cast<int>(role)));
    });
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Replace loads only care about the latest request
    if (role == LoadReplace) {
        if (generation != m_loadGeneration) {
            return; // superseded by a newer navigation / open
        }
        if (path != m_classicPath || isMultiItemMode()) {
            // Stale image-mode navigation or switched to workspace
            if (!(isMultiItemMode() && m_items.isEmpty() && path == m_classicPath)) {
                if (path != m_classicPath) {
                    return;
                }
            }
        }
        if (image.isNull()) {
            m_lastLoadError = path;
            emit statusChanged();
            return;
        }
        if (isImageMode()) {
            clearWorkspace();
            m_lastLoadError.clear();
            ImageItem *item = createItemFromImage(path, image);
            if (!item) {
                m_lastLoadError = path;
                emit statusChanged();
                return;
            }
            // Never inherit Gallery/Workspace placement or scale.
            item->setInteractive(false);
            item->setScaleHandlesEnabled(false);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setPos(0, 0);
            prepareImageModeCanvas();
            fitItem(item, currentFitAspectMode());
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image
        if (m_items.isEmpty()) {
            ImageItem *item = createItemFromImage(path, image);
            if (item) {
                item->setSelected(true);
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
                emit statusChanged();
            }
        }
        return;
    }

    // Workspace add / restore
    if (!m_pendingWorkspacePaths.contains(path)) {
        return;
    }
    m_pendingWorkspacePaths.remove(path);
    if (image.isNull() || findItemByPath(path)) {
        return;
    }

    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    // Flags already match ViewMode via createItemFromImage / applyItemModeFlags.
    // Do not force interactive — that flashes handles in Gallery.

    if (role == LoadRestore) {
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.constEnd()) {
            applyState(item, *it);
        } else {
            // fall back to saved workspace list
            for (const WorkspaceItemState &s : m_savedWorkspace) {
                if (s.path == path) {
                    applyState(item, s);
                    break;
                }
            }
        }
    } else {
        // LoadAdd: drop position, remembered state, or empty-space placement
        if (m_pendingScenePos.contains(path)) {
            const QPointF pos = m_pendingScenePos.take(path);
            item->setPos(pos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
        } else {
            const auto it = m_itemStates.constFind(path);
            if (it != m_itemStates.constEnd()) {
                applyState(item, *it);
            } else {
                WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
                // Prefer non-overlapping placement using the decoded size
                const QSizeF sz(image.width(), image.height());
                s.pos = findEmptyPlacement(sz);
                applyState(item, s);
            }
        }
    }

    if (m_layoutMode != LayoutMode::FreeForm) {
        applyLayout();
    }
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::clearExtras()
{
    if (m_items.size() <= 1) {
        return;
    }
    ImageItem *keep = m_items.first();
    for (int i = m_items.size() - 1; i >= 1; --i) {
        ImageItem *item = m_items.takeAt(i);
        rememberItemState(item);
        m_scene->removeItem(item);
        delete item;
    }
    m_scene->clearSelection();
    if (isMultiItemMode()) {
        keep->setSelected(true);
    }
    m_fitMode = true;
    fitItem(keep, currentFitAspectMode());
    m_undoStack->clear();
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    if (m_tool == Tool::Pan) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    emit toolChanged(m_tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (m_imageModeNavEnabled == on) {
        return;
    }
    m_imageModeNavEnabled = on;
    if (!on) {
        m_hoverEdge = EdgeZone::None;
    }
    viewport()->update();
}

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || color == m_bgColor) {
        return;
    }
    m_bgColor = color;
    viewport()->update();
}

void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!color.isValid() || color == m_bgColorAlt) {
        return;
    }
    m_bgColorAlt = color;
    viewport()->update();
}

void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (m_bgPattern == pattern) {
        return;
    }
    m_bgPattern = pattern;
    viewport()->update();
}

void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (m_bgCheckerWorkspaceOnly == on) {
        return;
    }
    m_bgCheckerWorkspaceOnly = on;
    viewport()->update();
}

bool ImageView::loadImage(const QString &path)
{
    m_classicPath = path;
    m_lastLoadError.clear();

    if (isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_items.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return true;
    }

    // Classic mode: decode off the GUI thread. Keep the previous image until
    // the new one is ready so rapid navigation does not flash an empty view.
    // Clear residual view pan/zoom from Workspace/Gallery immediately.
    prepareImageModeCanvas();
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}

qreal ImageView::viewScale() const
{
    const QTransform t = transform();
    return std::hypot(t.m11(), t.m12());
}

void ImageView::refreshStatus()
{
    emit statusChanged();
    if (m_hudVisible || m_hudFlashVisible) {
        viewport()->update();
    }
}

void ImageView::zoomViewBy(qreal factor)
{
    // Packaged layouts place items in viewport-pixel scene units and keep the
    // view transform identity. Scaling the view breaks that invariant until the
    // next resize reapplies the layout — skip view zoom there.
    if (isGalleryMode()) {
        return;
    }

    m_fitMode = false;
    m_fillMode = false;
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Handle sizes depend on view scale — refresh geometry for selected items
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->updateHandleLayout();
        }
    }
    emit statusChanged();
}

void ImageView::zoomIn()
{
    // View-level zoom in Image mode and free-form Workspace
    zoomViewBy(1.25);
}

void ImageView::zoomOut()
{
    zoomViewBy(1.0 / 1.25);
}

void ImageView::zoomReset()
{
    m_fitMode = false;
    m_fillMode = false;
    if (isMultiItemMode()) {
        resetTransform();
        emit statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        resetTransform();
        centerOn(item);
        emit statusChanged();
    }
}

void ImageView::zoomFit()
{
    m_fitMode = true;
    m_fillMode = false;
    if (isGalleryMode()) {
        if (!m_items.isEmpty()) {
            applyLayout();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatio);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatio);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        emit statusChanged();
    }
}

void ImageView::zoomFill()
{
    m_fitMode = true;
    m_fillMode = true;
    if (isMultiItemMode()) {
        if (isGalleryMode() && !m_items.isEmpty()) {
            applyLayout();
            return;
        }
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatioByExpanding);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatioByExpanding);
        emit statusChanged();
    }
}

void ImageView::flipHorizontal()
{
    if (ImageItem *item = targetItem()) {
        item->toggleHFlip();
        if (m_fitMode) {
            fitItem(item, currentFitAspectMode());
        }
        emit statusChanged();
    }
}

void ImageView::flipVertical()
{
    if (ImageItem *item = targetItem()) {
        item->toggleVFlip();
        if (m_fitMode) {
            fitItem(item, currentFitAspectMode());
        }
        emit statusChanged();
    }
}

void ImageView::setImageModeLeftDragPan(bool on)
{
    m_imageModeLeftDragPan = on;
}

void ImageView::setHudVisible(bool on)
{
    if (m_hudVisible == on) {
        return;
    }
    m_hudVisible = on;
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hudAction = action;
    m_hudDetail = detail;
    m_hudFlashVisible = true;
    if (m_hudFlashTimer) {
        m_hudFlashTimer->start(1800);
    }
    viewport()->update();
}

void ImageView::rotateLeft()
{
    if (ImageItem *item = targetItem()) {
        // Snap to the previous multiple of 90° (clean right-angles)
        const qreal r = item->itemRotation();
        const qreal next = std::ceil(r / 90.0 - 1e-6) * 90.0 - 90.0;
        item->setItemRotation(next);
        if (m_fitMode) {
            fitItem(item, currentFitAspectMode());
        }
        emit statusChanged();
    }
}

void ImageView::rotateRight()
{
    if (ImageItem *item = targetItem()) {
        // Snap to the next multiple of 90° (clean right-angles)
        const qreal r = item->itemRotation();
        const qreal next = std::floor(r / 90.0 + 1e-6) * 90.0 + 90.0;
        item->setItemRotation(next);
        if (m_fitMode) {
            fitItem(item, currentFitAspectMode());
        }
        emit statusChanged();
    }
}

void ImageView::raiseSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setStackZ(item->stackZ() + 1.0);
        emit statusChanged();
    }
}

void ImageView::lowerSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setStackZ(item->stackZ() - 1.0);
        emit statusChanged();
    }
}

void ImageView::opacityUp()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() + 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() - 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(1.0);
        emit statusChanged();
    }
}

void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    if (!item) {
        return;
    }
    if (isImageMode() || m_items.size() == 1) {
        // Identity placement: view does the fit, not residual canvas state.
        item->setItemScale(1.0);
        if (isImageMode()) {
            item->setItemRotation(0.0);
            item->setPos(0, 0);
        }
        resetTransform();
        fitInView(item, mode);
        return;
    }
    fitInView(item, mode);
}

Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_fillMode ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
}

void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(item, 32, 32);
    }
}

QString ImageView::currentPath() const
{
    if (ImageItem *item = targetItem()) {
        return item->path();
    }
    if (ImageItem *item = primaryItem()) {
        return item->path();
    }
    return {};
}

QSize ImageView::imageSize() const
{
    if (ImageItem *item = targetItem()) {
        return item->imageSize();
    }
    if (ImageItem *item = primaryItem()) {
        return item->imageSize();
    }
    return {};
}

int ImageView::itemCount() const
{
    return m_items.size();
}

QStringList ImageView::itemPaths() const
{
    QStringList paths;
    for (ImageItem *item : m_items) {
        paths.append(item->path());
    }
    return paths;
}

QString ImageView::statusText() const
{
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (!item) {
        if (!m_lastLoadError.isEmpty()) {
            return tr("Failed to load: %1").arg(QFileInfo(m_lastLoadError).fileName());
        }
        if (!m_classicPath.isEmpty() && isImageMode()) {
            return tr("Loading %1…").arg(QFileInfo(m_classicPath).fileName());
        }
        return tr("Ready");
    }

    const QString name = QFileInfo(item->path()).fileName();
    if (isMultiItemMode()) {
        const QString modeLabel = isGalleryMode() ? tr("Gallery") : tr("Workspace");
        QString text = tr("%1: %2 images  |  View zoom: %3%  |  %4  |  %5×%6")
                           .arg(modeLabel)
                           .arg(m_items.size())
                           .arg(qRound(viewScale() * 100))
                           .arg(name)
                           .arg(item->imageSize().width())
                           .arg(item->imageSize().height());
        if (item->isSelected()) {
            text += tr("  |  Item: %1%  |  Rot: %2°")
                        .arg(qRound(item->itemScale() * 100))
                        .arg(qRound(item->itemRotation()));
        }
        if (item->itemOpacity() < 0.999) {
            text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
        }
        return text;
    }

    // Image mode: zoom is view-level; item scale stays at 1 unless rotated etc.
    QString text = tr("%1  |  %2×%3  |  Zoom: %4%  |  Rotation: %5°")
                       .arg(name)
                       .arg(item->imageSize().width())
                       .arg(item->imageSize().height())
                       .arg(qRound(viewScale() * 100))
                       .arg(qRound(item->itemRotation()));
    if (item->itemHFlip() || item->itemVFlip()) {
        QStringList flips;
        if (item->itemHFlip()) {
            flips << tr("H");
        }
        if (item->itemVFlip()) {
            flips << tr("V");
        }
        text += tr("  |  Flip: %1").arg(flips.join(QLatin1Char('+')));
    }
    if (item->itemOpacity() < 0.999) {
        text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
    }
    return text;
}

qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    const QPointF c = item->scenePos();
    return qRadiansToDegrees(std::atan2(scenePos.y() - c.y(), scenePos.x() - c.x()));
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Rapid edge clicks arrive as double-clicks (second press is not a Press event).
    // Treat them as navigation, same as a single click on the affordance.
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
        if (zone == EdgeZone::Previous) {
            emit navigatePreviousRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Next) {
            emit navigateNextRequested();
            event->accept();
            return;
        }
        emit fullscreenToggleRequested();
        event->accept();
        return;
    }

    // Gallery uses single-click-to-open (see mouseReleaseEvent).
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_mouseInfo.valid) {
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
    }
    if (m_hoverEdge != EdgeZone::None) {
        m_hoverEdge = EdgeZone::None;
        viewport()->update();
    }
    QGraphicsView::leaveEvent(event);
}

