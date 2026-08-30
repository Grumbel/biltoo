// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

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
    item->setInteractive(m_workspaceMode);
    item->setScaleHandlesEnabled(m_layoutMode == LayoutMode::FreeForm);
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
        if (path != m_classicPath || m_workspaceMode) {
            // Stale image-mode navigation or switched to workspace
            if (!(m_workspaceMode && m_items.isEmpty() && path == m_classicPath)) {
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
        if (!m_workspaceMode) {
            clearWorkspace();
            m_lastLoadError.clear();
            ImageItem *item = createItemFromImage(path, image);
            if (!item) {
                m_lastLoadError = path;
                emit statusChanged();
                return;
            }
            item->setInteractive(false);
            m_fitMode = true;
            resetTransform();
            if (horizontalScrollBar()) {
                horizontalScrollBar()->setValue(0);
            }
            if (verticalScrollBar()) {
                verticalScrollBar()->setValue(0);
            }
            fitItem(item, currentFitAspectMode());
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image
        if (m_items.isEmpty()) {
            ImageItem *item = createItemFromImage(path, image);
            if (item) {
                item->setInteractive(true);
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
    item->setInteractive(true);

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

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.pos = item->pos();
    s.scale = item->itemScale();
    s.rotation = item->itemRotation();
    s.opacity = item->itemOpacity();
    s.z = item->stackZ();
    s.hFlip = item->itemHFlip();
    s.vFlip = item->itemVFlip();
    return s;
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setStackZ(state.z);
    item->setItemHFlip(state.hFlip);
    item->setItemVFlip(state.vFlip);
}

void ImageView::rememberItemState(ImageItem *item)
{
    if (!item) {
        return;
    }
    m_itemStates.insert(item->path(), captureState(item));
}

void ImageView::snapshotWorkspace()
{
    m_savedWorkspace.clear();
    for (ImageItem *item : m_items) {
        const WorkspaceItemState s = captureState(item);
        m_savedWorkspace.append(s);
        m_itemStates.insert(s.path, s);
    }
}

void ImageView::restoreWorkspace()
{
    clearWorkspace();
    m_pendingWorkspacePaths.clear();
    for (const WorkspaceItemState &state : m_savedWorkspace) {
        m_itemStates.insert(state.path, state);
        scheduleImageLoad(state.path, LoadRestore);
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::clearWorkspace()
{
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
    m_items.clear();
    m_pendingScenePos.clear();
    m_pendingWorkspacePaths.clear();
    // clear() deletes all QGraphicsItems owned by the scene
    m_scene->clear();
    m_mouseInfo = {};
    emit mouseInfoChanged(m_mouseInfo);
}

ImageItem *ImageView::findItemByPath(const QString &path) const
{
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            return item;
        }
    }
    return nullptr;
}

WorkspaceItemState ImageView::defaultStateForPath(const QString &path, int ordinal) const
{
    WorkspaceItemState s;
    s.path = path;
    s.pos = QPointF(40.0 * ordinal, 30.0 * ordinal);
    s.scale = 1.0;
    s.rotation = 0.0;
    s.opacity = 1.0;
    s.z = ordinal;
    return s;
}

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    const QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();
    QSizeF size = itemSize;
    if (size.width() < 1.0 || size.height() < 1.0) {
        size = QSizeF(200.0, 200.0);
    }

    // Cap the collision footprint so huge images still leave room nearby
    const qreal maxEdge = qMax(120.0, qMin(viewRect.width(), viewRect.height()) * 0.45);
    const qreal longest = qMax(size.width(), size.height());
    if (longest > maxEdge) {
        const qreal f = maxEdge / longest;
        size = QSizeF(size.width() * f, size.height() * f);
    }

    const qreal gap = 32.0;
    auto overlaps = [&](const QPointF &centre) {
        const QRectF proposed(centre.x() - size.width() / 2.0 - gap,
                              centre.y() - size.height() / 2.0 - gap,
                              size.width() + 2.0 * gap,
                              size.height() + 2.0 * gap);
        for (ImageItem *item : m_items) {
            if (item->sceneBoundingRect().intersects(proposed)) {
                return true;
            }
        }
        return false;
    };

    QPointF candidate = viewRect.center();
    if (m_items.isEmpty() || !overlaps(candidate)) {
        return candidate;
    }

    // Spiral search around the viewport centre
    const qreal stepX = size.width() + gap;
    const qreal stepY = size.height() + gap;
    for (int ring = 1; ring <= 48; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dy = -ring; dy <= ring; ++dy) {
                if (qMax(qAbs(dx), qAbs(dy)) != ring) {
                    continue;
                }
                candidate = viewRect.center() + QPointF(dx * stepX, dy * stepY);
                if (!overlaps(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Last resort: to the right of everything currently on the canvas
    const QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isValid()) {
        return QPointF(bounds.right() + gap + size.width() / 2.0, bounds.center().y());
    }
    return viewRect.center();
}

void ImageView::setWorkspacePaths(const QStringList &paths)
{
    if (!m_workspaceMode) {
        return;
    }

    // Remember state of items that will be removed
    QSet<QString> wanted(paths.begin(), paths.end());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        ImageItem *item = m_items.at(i);
        if (!wanted.contains(item->path())) {
            if (item == m_dragItem) {
                m_dragItem = nullptr;
            }
            if (item == m_rotateItem) {
                m_rotateItem = nullptr;
                m_rotating = false;
            }
            rememberItemState(item);
            m_scene->removeItem(item);
            m_items.removeAt(i);
            delete item;
        }
    }

    // Load missing paths off the GUI thread
    for (const QString &path : paths) {
        if (findItemByPath(path)) {
            continue;
        }
        scheduleImageLoad(path, LoadAdd);
    }

    // Select something if nothing selected
    if (m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    rememberItemState(item);
    m_pendingWorkspacePaths.remove(path);
    m_items.removeOne(item);
    m_scene->removeItem(item);
    delete item;
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
    if (m_workspaceMode) {
        keep->setSelected(true);
    }
    m_fitMode = true;
    fitItem(keep, currentFitAspectMode());
    m_undoStack->clear();
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::setWorkspaceMode(bool on)
{
    if (on == m_workspaceMode) {
        return;
    }

    if (!on) {
        // Leaving workspace: remember the canvas, then go classic
        snapshotWorkspace();
        m_workspaceMode = false;
        viewport()->update();
        m_undoStack->clear();
        m_scene->clearSelection();
        // Drop workspace pan/zoom so Image mode starts centred again
        resetTransform();
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(0);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(0);
        }
        // Classic view of the last session path (caller may also loadImage)
        const QString path = !m_classicPath.isEmpty()
                                 ? m_classicPath
                                 : (m_items.isEmpty() ? QString() : m_items.first()->path());
        clearWorkspace();
        m_pendingWorkspacePaths.clear();
        if (!path.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return;
    }

    // Entering workspace
    m_workspaceMode = true;
    viewport()->update();
    if (!m_savedWorkspace.isEmpty()) {
        restoreWorkspace();
    } else {
        // Seed with the current classic image if any
        for (ImageItem *item : m_items) {
            item->setInteractive(true);
        }
        if (!m_items.isEmpty()) {
            m_scene->clearSelection();
            m_items.first()->setSelected(true);
        }
    }
    emit statusChanged();
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

int ImageView::edgeZoneWidth() const
{
    return qMax(48, static_cast<int>(width() * 0.12));
}

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (m_workspaceMode || !m_imageModeNavEnabled) {
        return EdgeZone::None;
    }
    const int zone = edgeZoneWidth();
    if (viewPos.x() < zone) {
        return EdgeZone::Previous;
    }
    if (viewPos.x() > width() - zone) {
        return EdgeZone::Next;
    }
    return EdgeZone::None;
}

void ImageView::updateHoverEdge(const QPoint &viewPos)
{
    const EdgeZone zone = edgeZoneAt(viewPos);
    if (zone == m_hoverEdge) {
        return;
    }
    m_hoverEdge = zone;
    if (m_hoverEdge == EdgeZone::Previous || m_hoverEdge == EdgeZone::Next) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_panning && !m_rotating) {
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    viewport()->update();
}

void ImageView::drawEdgeAffordances(QPainter &painter)
{
    if (m_hoverEdge == EdgeZone::None || m_workspaceMode || !m_imageModeNavEnabled) {
        return;
    }

    const QRect vr = viewport()->rect();
    const int zone = edgeZoneWidth();
    const int cy = vr.center().y();

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Soft hit-zone wash
    QLinearGradient grad;
    if (m_hoverEdge == EdgeZone::Previous) {
        grad = QLinearGradient(0, 0, zone, 0);
        grad.setColorAt(0.0, QColor(0, 0, 0, 90));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(QRect(0, 0, zone, vr.height()), grad);
    } else {
        grad = QLinearGradient(vr.width() - zone, 0, vr.width(), 0);
        grad.setColorAt(0.0, QColor(0, 0, 0, 0));
        grad.setColorAt(1.0, QColor(0, 0, 0, 90));
        painter.fillRect(QRect(vr.width() - zone, 0, zone, vr.height()), grad);
    }

    // Circular button with chevron — sit near the window edge
    const int r = 22;
    constexpr int kEdgeMargin = 10; // gap from window edge to button rim
    const int cx = (m_hoverEdge == EdgeZone::Previous)
                       ? (kEdgeMargin + r)
                       : (vr.width() - kEdgeMargin - r);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawEllipse(QPoint(cx, cy), r, r);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawEllipse(QPoint(cx, cy), r - 3, r - 3);

    QPainterPath chevron;
    if (m_hoverEdge == EdgeZone::Previous) {
        chevron.moveTo(cx + 5, cy - 10);
        chevron.lineTo(cx - 6, cy);
        chevron.lineTo(cx + 5, cy + 10);
    } else {
        chevron.moveTo(cx - 5, cy - 10);
        chevron.lineTo(cx + 6, cy);
        chevron.lineTo(cx - 5, cy + 10);
    }
    QPen pen(QColor(40, 40, 40), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.strokePath(chevron, pen);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    QPainter painter(viewport());
    if (m_hoverEdge != EdgeZone::None && !m_workspaceMode && m_imageModeNavEnabled) {
        drawEdgeAffordances(painter);
    }
    if (m_hudVisible || m_hudFlashVisible) {
        QFont f = font();
        f.setPointSizeF(qMax(10.0, f.pointSizeF()));
        painter.setFont(f);
        const QFontMetrics fm(f);
        const int margin = 10;
        const int pad = 8;
        const int maxW = qMax(40, viewport()->width() - 2 * margin);

        QStringList lines;
        if (m_hudFlashVisible && !m_hudAction.isEmpty()) {
            QString actionLine = m_hudAction;
            if (!m_hudDetail.isEmpty()) {
                actionLine += QLatin1Char(' ') + m_hudDetail;
            }
            lines << actionLine;
        }
        if (m_hudVisible) {
            lines << statusText();
        } else if (m_hudFlashVisible && m_hudDetail.isEmpty()) {
            // Flash without pin: still show filename under the action
            ImageItem *item = targetItem();
            if (!item) {
                item = primaryItem();
            }
            if (item) {
                lines << QFileInfo(item->path()).fileName();
            }
        }

        if (!lines.isEmpty()) {
            int textW = 0;
            int textH = 0;
            QStringList elidedLines;
            for (const QString &line : lines) {
                const QString el = fm.elidedText(line, Qt::ElideMiddle, maxW - 2 * pad);
                elidedLines << el;
                textW = qMax(textW, fm.horizontalAdvance(el));
                textH += fm.height();
            }
            if (elidedLines.size() > 1) {
                textH += 2 * (elidedLines.size() - 1); // line gap
            }
            const QRect bg(margin, margin, textW + 2 * pad, textH + 2 * pad);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 170));
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(QColor(240, 240, 240));
            int y = bg.top() + pad;
            for (int i = 0; i < elidedLines.size(); ++i) {
                if (i == 0 && m_hudFlashVisible && !m_hudAction.isEmpty()) {
                    QFont bold = f;
                    bold.setBold(true);
                    painter.setFont(bold);
                } else {
                    painter.setFont(f);
                }
                painter.drawText(QRect(bg.left() + pad, y, textW, fm.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter, elidedLines.at(i));
                y += fm.height() + 2;
            }
        }
    }
}


void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const QPointF scenePos = mapToScene(event->position().toPoint());
    emit filesDropped(event->mimeData()->urls(), event->modifiers(), scenePos);
    event->acceptProposedAction();
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    if (!m_workspaceMode) {
        // Image mode: flat dark fill (matches setBackgroundBrush)
        painter->fillRect(rect, QColor(36, 36, 36));
        return;
    }

    // Subtle checkerboard in scene coordinates so it pans/zooms with the view
    constexpr int kCell = 16;
    const QColor a(42, 42, 42);
    const QColor b(48, 48, 48);

    const int x0 = static_cast<int>(std::floor(rect.left() / kCell)) * kCell;
    const int y0 = static_cast<int>(std::floor(rect.top() / kCell)) * kCell;
    const int x1 = static_cast<int>(std::ceil(rect.right() / kCell)) * kCell;
    const int y1 = static_cast<int>(std::ceil(rect.bottom() / kCell)) * kCell;

    for (int y = y0; y < y1; y += kCell) {
        for (int x = x0; x < x1; x += kCell) {
            const bool dark = ((x / kCell) + (y / kCell)) & 1;
            painter->fillRect(QRect(x, y, kCell, kCell), dark ? a : b);
        }
    }
}

bool ImageView::loadImage(const QString &path)
{
    m_classicPath = path;
    m_lastLoadError.clear();

    if (m_workspaceMode) {
        // Session navigation while in workspace does not destroy the canvas;
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
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}

bool ImageView::addImage(const QString &path)
{
    if (!m_workspaceMode) {
        return false;
    }

    if (ImageItem *existing = findItemByPath(path)) {
        m_scene->clearSelection();
        existing->setSelected(true);
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }

    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty()) {
        return false;
    }
    m_pendingScenePos.insert(path, scenePos);
    return addImage(path);
}


ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            return item;
        }
    }
    if (m_items.size() == 1) {
        return m_items.first();
    }
    return nullptr;
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
    // View-level zoom in both modes (items keep their own scale/rotation)
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
    if (m_workspaceMode) {
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
    if (m_workspaceMode) {
        if (m_layoutMode != LayoutMode::FreeForm && !m_items.isEmpty()) {
            applyLayout();
        } else if (!m_items.isEmpty()) {
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
    if (m_workspaceMode) {
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
    if (ImageItem *item = targetItem()) {
        item->setStackZ(item->stackZ() + 1.0);
        emit statusChanged();
    }
}

void ImageView::lowerSelected()
{
    if (ImageItem *item = targetItem()) {
        item->setStackZ(item->stackZ() - 1.0);
        emit statusChanged();
    }
}

void ImageView::opacityUp()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() + 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() - 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(1.0);
        emit statusChanged();
    }
}

QSizeF ImageView::nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return QSizeF(item->pixmap().size());
}

void ImageView::snapshotFreeFormStates()
{
    m_freeFormStates.clear();
    for (ImageItem *item : m_items) {
        m_freeFormStates.insert(item->path(), captureState(item));
    }
    m_freeFormViewTransform = transform();
    m_hasFreeFormViewTransform = true;
}

void ImageView::restoreFreeFormStates()
{
    for (ImageItem *item : m_items) {
        const auto it = m_freeFormStates.constFind(item->path());
        if (it != m_freeFormStates.constEnd()) {
            applyState(item, *it);
            m_itemStates.insert(item->path(), *it);
        }
    }
    if (m_hasFreeFormViewTransform) {
        setTransform(m_freeFormViewTransform);
    }
}

void ImageView::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == LayoutMode::FreeForm && mode != LayoutMode::FreeForm) {
        // Leaving free-form: remember positions and view pan/zoom
        snapshotFreeFormStates();
    }

    m_layoutMode = mode;

    // Resize handles only make sense in free-form placement
    const bool scaleHandles = (mode == LayoutMode::FreeForm);
    for (ImageItem *item : m_items) {
        item->setScaleHandlesEnabled(scaleHandles);
    }

    if (mode == LayoutMode::FreeForm) {
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-64, -64, 64, 64));
        }
        m_fitMode = false;
        emit statusChanged();
        return;
    }

    applyLayout();
}

void ImageView::setMasonryColumnWidth(int pixels)
{
    const int clamped = qBound(80, pixels, 800);
    if (clamped == m_masonryColumnWidth) {
        return;
    }
    m_masonryColumnWidth = clamped;
    if (m_layoutMode == LayoutMode::Masonry) {
        applyLayout();
    }
}

void ImageView::applyLayout()
{

    if (m_items.isEmpty() || m_layoutMode == LayoutMode::FreeForm) {
        return;
    }

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    centerOn(0, 0);

    const qreal margin = 16.0;
    const qreal gap = 12.0;
    const qreal availW = qMax(32.0, static_cast<qreal>(viewport()->width()) - 2.0 * margin);
    const qreal availH = qMax(32.0, static_cast<qreal>(viewport()->height()) - 2.0 * margin);
    const int n = m_items.size();

    // Zero rotation so edges align (stack keeps rotation for A/B compare)
    if (m_layoutMode != LayoutMode::Stack) {
        for (ImageItem *item : m_items) {
            item->setItemRotation(0.0);
        }
    }

    if (m_layoutMode == LayoutMode::SideBySide) {
        const qreal cellW = (availW - gap * qMax(0, n - 1)) / qMax(1, n);
        qreal x = margin;
        for (ImageItem *item : m_items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMin(cellW / qMax(1.0, ns.width()),
                                    availH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setPos(x + cellW / 2.0, margin + availH / 2.0);
            x += cellW + gap;
            m_itemStates.insert(item->path(), captureState(item));
        }
    } else if (m_layoutMode == LayoutMode::Vertical) {
        const qreal cellH = (availH - gap * qMax(0, n - 1)) / qMax(1, n);
        qreal y = margin;
        for (ImageItem *item : m_items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMin(availW / qMax(1.0, ns.width()),
                                    cellH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setPos(margin + availW / 2.0, y + cellH / 2.0);
            y += cellH + gap;
            m_itemStates.insert(item->path(), captureState(item));
        }
    } else if (m_layoutMode == LayoutMode::Grid) {
        const int cols = qMax(1, static_cast<int>(std::ceil(std::sqrt(double(n)))));
        const int rows = qMax(1, static_cast<int>(std::ceil(double(n) / double(cols))));
        const qreal cellW = (availW - gap * qMax(0, cols - 1)) / cols;
        const qreal cellH = (availH - gap * qMax(0, rows - 1)) / rows;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = m_items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMin(cellW / qMax(1.0, ns.width()),
                                    cellH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            const qreal cx = margin + col * (cellW + gap) + cellW / 2.0;
            const qreal cy = margin + row * (cellH + gap) + cellH / 2.0;
            item->setPos(cx, cy);
            m_itemStates.insert(item->path(), captureState(item));
        }
    } else if (m_layoutMode == LayoutMode::Masonry) {
        // Fixed column width (configurable); as many columns as fit; scroll for overflow
        const qreal colW = static_cast<qreal>(qMax(40, m_masonryColumnWidth));
        int cols = qMax(1, static_cast<int>(std::floor((availW + gap) / (colW + gap))));
        cols = qMin(cols, n);
        QVector<qreal> colHeights(cols, 0.0);

        // Enable scrolling so tall columns remain reachable
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        for (ImageItem *item : m_items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = colW / qMax(1.0, ns.width());
            item->setItemScale(scale);
            const qreal h = ns.height() * scale;

            int best = 0;
            for (int c = 1; c < cols; ++c) {
                if (colHeights.at(c) < colHeights.at(best)) {
                    best = c;
                }
            }

            const qreal cx = margin + best * (colW + gap) + colW / 2.0;
            const qreal cy = margin + colHeights.at(best) + h / 2.0;
            item->setPos(cx, cy);
            colHeights[best] += h + gap;
            m_itemStates.insert(item->path(), captureState(item));
        }
    } else if (m_layoutMode == LayoutMode::Stack) {
        // Scale each to fit the view; overlap at centre for A/B comparison
        for (ImageItem *item : m_items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMin(availW / qMax(1.0, ns.width()),
                                    availH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setPos(margin + availW / 2.0, margin + availH / 2.0);
            m_itemStates.insert(item->path(), captureState(item));
        }
    }

    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    m_scene->setSceneRect(bounds);
    m_fitMode = true;
    emit statusChanged();
}

void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    if (!item) {
        return;
    }
    fitInView(item, mode);
    if (m_items.size() == 1) {
        item->setItemScale(1.0);
        fitInView(item, mode);
    }
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
        if (!m_classicPath.isEmpty() && !m_workspaceMode) {
            return tr("Loading %1…").arg(QFileInfo(m_classicPath).fileName());
        }
        return tr("Ready");
    }

    const QString name = QFileInfo(item->path()).fileName();
    if (m_workspaceMode) {
        QString text = tr("Workspace: %1 images  |  View zoom: %2%  |  %3  |  %4×%5")
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

void ImageView::updateMouseInfo(const QPoint &viewPos)
{
    ImageMouseInfo info;
    const QPointF scenePos = mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = item;
            break;
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (info.valid != m_mouseInfo.valid
        || info.imagePos != m_mouseInfo.imagePos
        || info.pixelColor != m_mouseInfo.pixelColor
        || info.path != m_mouseInfo.path) {
        m_mouseInfo = info;
        emit mouseInfoChanged(m_mouseInfo);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);

    // Both modes: zoom the view about the cursor
    m_fitMode = false;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->updateHandleLayout();
        }
    }
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_workspaceMode && m_layoutMode != LayoutMode::FreeForm && !m_items.isEmpty()) {
        applyLayout();
        return;
    }
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}

qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    const QPointF c = item->scenePos();
    return qRadiansToDegrees(std::atan2(scenePos.y() - c.y(), scenePos.x() - c.x()));
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    // Image mode: left/right edge clicks navigate the session
    if (!m_workspaceMode && event->button() == Qt::LeftButton
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
    }

    // Image mode (when preferred), or Pan tool / Alt: left-drag pans
    if (event->button() == Qt::LeftButton) {
        const bool wantPan = (!m_workspaceMode && m_imageModeLeftDragPan)
                             || (m_workspaceMode && m_tool == Tool::Pan)
                             || (event->modifiers() & Qt::AltModifier);
        if (wantPan && !(m_workspaceMode && (event->modifiers() & Qt::ShiftModifier))) {
            m_panning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Workspace only: Shift + left button free-rotates
    if (m_workspaceMode && event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ShiftModifier)) {
        ImageItem *hit = nullptr;
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        if (!hit) {
            hit = targetItem();
        }
        if (hit) {
            m_rotating = true;
            m_rotateItem = hit;
            m_rotateStartAngle = angleAt(scenePos, hit);
            m_rotateItemStart = hit->itemRotation();
            m_dragStartState = captureState(hit);
            m_scene->clearSelection();
            hit->setSelected(true);
            setCursor(Qt::CrossCursor);
            event->accept();
            return;
        }
    }

    // Workspace Select tool: let QGraphicsView handle selection / move
    if (m_workspaceMode && event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        // Capture drag start for undo when an item is selected under the cursor
        if (ImageItem *hit = targetItem()) {
            m_dragItem = hit;
            m_dragStartState = captureState(hit);
        }
        emit statusChanged();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    updateMouseInfo(event->pos());

    if (m_rotating && m_rotateItem) {
        const QPointF scenePos = mapToScene(event->pos());
        const qreal angle = angleAt(scenePos, m_rotateItem);
        const qreal delta = angle - m_rotateStartAngle;
        qreal rot = m_rotateItemStart + delta;
        if (event->modifiers() & Qt::ControlModifier) {
            rot = qRound(rot / 90.0) * 90.0;
        } else if (event->modifiers() & Qt::ShiftModifier) {
            // Shift is held to start free-rotate; add Ctrl for 90°, alone keep smooth
            // unless also... User asked Ctrl/Shift snap. Shift starts rotate so
            // during shift-drag, snap to 45 when Shift still held without wanting smooth.
            // Use Ctrl=90 always; for shift-drag path Shift is always down — snap 45.
            rot = qRound(rot / 45.0) * 45.0;
        }
        m_rotateItem->setItemRotation(rot);
        m_fitMode = false;
        emit statusChanged();
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (!m_workspaceMode) {
        updateHoverEdge(event->pos());
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Rapid edge clicks arrive as double-clicks (second press is not a Press event).
    // Treat them as navigation, same as a single click on the affordance.
    if (!m_workspaceMode && event->button() == Qt::LeftButton
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
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_rotating && event->button() == Qt::LeftButton) {
        if (m_rotateItem) {
            const WorkspaceItemState after = captureState(m_rotateItem);
            if (after.rotation != m_dragStartState.rotation
                || after.pos != m_dragStartState.pos) {
                // Lightweight: clear is avoided; push a simple undo via reset path
                // Store as single-step by re-applying start on undo through stack of states
                class TransformCommand : public QUndoCommand {
                public:
                    TransformCommand(ImageView *view, ImageItem *item,
                                     const WorkspaceItemState &before,
                                     const WorkspaceItemState &after)
                        : m_view(view), m_item(item), m_before(before), m_after(after)
                    {
                        setText(QObject::tr("Transform"));
                    }
                    void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                    void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
                private:
                    ImageView *m_view;
                    ImageItem *m_item;
                    WorkspaceItemState m_before, m_after;
                };
                m_undoStack->push(new TransformCommand(this, m_rotateItem,
                                                       m_dragStartState, after));
            }
        }
        m_rotating = false;
        m_rotateItem = nullptr;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_dragItem && event->button() == Qt::LeftButton) {
        const WorkspaceItemState after = captureState(m_dragItem);
        if (after.pos != m_dragStartState.pos
            || after.scale != m_dragStartState.scale
            || after.rotation != m_dragStartState.rotation) {
            class TransformCommand : public QUndoCommand {
            public:
                TransformCommand(ImageView *view, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after)
                    : m_view(view), m_item(item), m_before(before), m_after(after)
                {
                    setText(QObject::tr("Move"));
                }
                void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
            private:
                ImageView *m_view;
                ImageItem *m_item;
                WorkspaceItemState m_before, m_after;
            };
            m_undoStack->push(new TransformCommand(this, m_dragItem,
                                                   m_dragStartState, after));
            emit statusChanged();
        }
        m_dragItem = nullptr;
    }
    QGraphicsView::mouseReleaseEvent(event);
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

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Remove selected items from the workspace (session list is unchanged);
        // remember transform so re-selecting the thumbnail restores position.
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        bool removed = false;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                rememberItemState(item);
                m_items.removeOne(item);
                m_scene->removeItem(item);
                delete item;
                removed = true;
            }
        }
        if (removed) {
            emit statusChanged();
            emit workspacePathsChanged();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
