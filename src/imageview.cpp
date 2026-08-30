// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    m_undoStack = new QUndoStack(this);

    setRenderHint(QPainter::SmoothPixmapTransform, true);
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

ImageItem *ImageView::loadItem(const QString &path)
{
    const QImage image = ImageLoader::load(path);
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    item->setInteractive(m_workspaceMode);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.pos = item->pos();
    s.scale = item->itemScale();
    s.rotation = item->itemRotation();
    s.opacity = item->itemOpacity();
    s.z = item->zValue();
    return s;
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setZValue(state.z);
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
    for (const WorkspaceItemState &state : m_savedWorkspace) {
        ImageItem *item = loadItem(state.path);
        if (!item) {
            continue;
        }
        applyState(item, state);
        item->setInteractive(true);
        m_itemStates.insert(state.path, state);
    }
    if (!m_items.isEmpty()) {
        m_scene->clearSelection();
        m_items.last()->setSelected(true);
        m_fitMode = false;
        ensureVisibleItem(m_items.last());
    }
    emit statusChanged();
}

void ImageView::clearWorkspace()
{
    m_scene->clear();
    m_items.clear();
    m_mouseInfo = {};
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
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
            rememberItemState(item);
            m_scene->removeItem(item);
            m_items.removeAt(i);
            delete item;
        }
    }

    // Add missing paths
    int ordinal = m_items.size();
    bool added = false;
    for (const QString &path : paths) {
        if (findItemByPath(path)) {
            continue;
        }
        ImageItem *item = loadItem(path);
        if (!item) {
            continue;
        }
        WorkspaceItemState state = m_itemStates.value(path);
        if (state.path.isEmpty()) {
            state = defaultStateForPath(path, ordinal);
        }
        applyState(item, state);
        item->setInteractive(true);
        ++ordinal;
        added = true;
    }

    if (added && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout();
    } else if (added && !m_items.isEmpty()) {
        m_fitMode = false;
        ensureVisibleItem(m_items.last());
    }

    // Select the most recently added / last path if nothing selected
    if (m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    emit statusChanged();
}

void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    rememberItemState(item);
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
    fitItem(keep);
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
        m_undoStack->clear();
        m_scene->clearSelection();
        // Classic view of the last session path (caller may also loadImage)
        const QString path = !m_classicPath.isEmpty()
                                 ? m_classicPath
                                 : (m_items.isEmpty() ? QString() : m_items.first()->path());
        clearWorkspace();
        if (!path.isEmpty()) {
            ImageItem *item = loadItem(path);
            if (item) {
                item->setInteractive(false);
                m_fitMode = true;
                fitItem(item);
            }
        }
        emit statusChanged();
        return;
    }

    // Entering workspace
    m_workspaceMode = true;
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
    if (m_hoverEdge != EdgeZone::None && !m_workspaceMode && m_imageModeNavEnabled) {
        QPainter painter(viewport());
        drawEdgeAffordances(painter);
    }
}

bool ImageView::loadImage(const QString &path)
{
    m_classicPath = path;

    if (m_workspaceMode) {
        // Session navigation while in workspace does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_items.isEmpty()) {
            ImageItem *item = loadItem(path);
            if (!item) {
                return false;
            }
            item->setInteractive(true);
            item->setSelected(true);
            m_fitMode = true;
            fitItem(item);
            emit statusChanged();
        } else {
            emit statusChanged();
        }
        return true;
    }

    // Classic mode: single centred non-interactive image
    clearWorkspace();
    ImageItem *item = loadItem(path);
    if (!item) {
        return false;
    }
    item->setInteractive(false);
    m_fitMode = true;
    fitItem(item);
    // Reset view transform so the image is truly centred
    resetTransform();
    fitItem(item);
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

    ImageItem *item = loadItem(path);
    if (!item) {
        return false;
    }

    WorkspaceItemState state = m_itemStates.value(path);
    if (state.path.isEmpty()) {
        state = defaultStateForPath(path, m_items.size() - 1);
    }
    applyState(item, state);

    m_scene->clearSelection();
    item->setSelected(true);
    if (m_layoutMode != LayoutMode::FreeForm) {
        applyLayout();
    } else {
        m_fitMode = false;
        ensureVisibleItem(item);
        emit statusChanged();
    }
    return true;
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

void ImageView::zoomIn()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->zoomBy(1.25);
        emit statusChanged();
    }
}

void ImageView::zoomOut()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->zoomBy(1.0 / 1.25);
        emit statusChanged();
    }
}

void ImageView::zoomReset()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = false;
        item->setItemScale(1.0);
        emit statusChanged();
    }
}

void ImageView::zoomFit()
{
    if (ImageItem *item = targetItem()) {
        m_fitMode = true;
        fitItem(item);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        // Fit the whole scene
        m_fitMode = true;
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        emit statusChanged();
    }
}

void ImageView::rotateLeft()
{
    if (ImageItem *item = targetItem()) {
        item->rotateBy(-90.0);
        if (m_fitMode) {
            fitItem(item);
        }
        emit statusChanged();
    }
}

void ImageView::rotateRight()
{
    if (ImageItem *item = targetItem()) {
        item->rotateBy(90.0);
        if (m_fitMode) {
            fitItem(item);
        }
        emit statusChanged();
    }
}

void ImageView::raiseSelected()
{
    if (ImageItem *item = targetItem()) {
        item->setZValue(item->zValue() + 1.0);
        emit statusChanged();
    }
}

void ImageView::lowerSelected()
{
    if (ImageItem *item = targetItem()) {
        item->setZValue(item->zValue() - 1.0);
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

void ImageView::setLayoutMode(LayoutMode mode)
{
    m_layoutMode = mode;
    if (mode != LayoutMode::FreeForm) {
        applyLayout();
    } else {
        emit statusChanged();
    }
}

void ImageView::applyLayout()
{
    if (m_items.isEmpty() || m_layoutMode == LayoutMode::FreeForm) {
        return;
    }

    const qreal gap = 24.0;

    // Normalise rotation for packaged layouts so edges align
    if (m_layoutMode != LayoutMode::Stack) {
        for (ImageItem *item : m_items) {
            item->setItemRotation(0.0);
        }
    }

    if (m_layoutMode == LayoutMode::SideBySide) {
        qreal x = 0.0;
        for (ImageItem *item : m_items) {
            const QRectF br = item->boundingRect();
            const qreal w = br.width() * item->itemScale();
            const qreal h = br.height() * item->itemScale();
            item->setPos(x + w / 2.0, h / 2.0);
            x += w + gap;
        }
    } else if (m_layoutMode == LayoutMode::Vertical) {
        qreal y = 0.0;
        for (ImageItem *item : m_items) {
            const QRectF br = item->boundingRect();
            const qreal w = br.width() * item->itemScale();
            const qreal h = br.height() * item->itemScale();
            item->setPos(w / 2.0, y + h / 2.0);
            y += h + gap;
        }
    } else if (m_layoutMode == LayoutMode::Grid) {
        const int n = m_items.size();
        const int cols = qMax(1, int(std::ceil(std::sqrt(double(n)))));
        qreal colWidth = 0.0;
        qreal rowHeight = 0.0;
        // First pass: max cell size
        for (ImageItem *item : m_items) {
            const QRectF br = item->boundingRect();
            colWidth = qMax(colWidth, br.width() * item->itemScale());
            rowHeight = qMax(rowHeight, br.height() * item->itemScale());
        }
        colWidth += gap;
        rowHeight += gap;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = m_items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QRectF br = item->boundingRect();
            const qreal w = br.width() * item->itemScale();
            const qreal h = br.height() * item->itemScale();
            item->setPos(col * colWidth + w / 2.0, row * rowHeight + h / 2.0);
        }
    } else if (m_layoutMode == LayoutMode::Stack) {
        // Overlap at the same centre for A/B comparison with opacity
        for (ImageItem *item : m_items) {
            item->setPos(0.0, 0.0);
        }
    }

    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32));
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_fitMode = true;
    emit statusChanged();
}

void ImageView::fitItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    fitInView(item, Qt::KeepAspectRatio);
    // Derive approximate scale from the view transform for status display
    const QTransform t = transform();
    const qreal viewScale = std::hypot(t.m11(), t.m12());
    // Item keeps its own scale at 1 when we use view-level fit for a single item;
    // for multi-item we prefer item-level scale. Reset item scale and use view fit
    // only when a single item is present.
    if (m_items.size() == 1) {
        item->setItemScale(1.0);
        item->setItemRotation(item->itemRotation());
        fitInView(item, Qt::KeepAspectRatio);
        Q_UNUSED(viewScale);
    }
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
        return tr("Ready");
    }

    const QString name = QFileInfo(item->path()).fileName();
    QString text = tr("%1  |  %2×%3  |  Zoom: %4%  |  Rotation: %5°")
                       .arg(name)
                       .arg(item->imageSize().width())
                       .arg(item->imageSize().height())
                       .arg(qRound(item->itemScale() * 100))
                       .arg(qRound(item->itemRotation()));
    if (item->itemOpacity() < 0.999) {
        text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
    }

    if (m_items.size() > 1) {
        text = tr("Workspace: %1 images  |  %2").arg(m_items.size()).arg(text);
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
    // Zoom the item under the mouse, or the selection
    ImageItem *item = nullptr;
    const QPointF scenePos = mapToScene(event->position().toPoint());
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            item = ii;
            break;
        }
    }
    if (!item) {
        item = targetItem();
    }
    if (!item) {
        return;
    }

    m_fitMode = false;
    if (event->angleDelta().y() > 0) {
        item->zoomBy(1.25);
    } else {
        item->zoomBy(1.0 / 1.25);
    }
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first());
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

    // Image mode, or Pan tool: left-drag pans
    if (event->button() == Qt::LeftButton) {
        const bool wantPan = !m_workspaceMode
                             || m_tool == Tool::Pan
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
        m_rotateItem->setItemRotation(m_rotateItemStart + delta);
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
    if (!m_workspaceMode && event->button() == Qt::LeftButton) {
        // Ignore double-clicks that land on the nav edge zones
        if (edgeZoneAt(event->pos()) == EdgeZone::None) {
            emit fullscreenToggleRequested();
            event->accept();
            return;
        }
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
