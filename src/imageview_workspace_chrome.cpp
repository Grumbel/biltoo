// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace viewport chrome and free-form rotate input (ImageView-owned).

#include "imageview.h"
#include "itemcomponents.h"
#include "imageitem.h"

#include <QMouseEvent>
#include "placementlinear.h"
#include "grouptransformgeometry.h"
#include <QToolTip>
#include <QGraphicsItem>

bool ImageView::tryMousePressWorkspaceChrome(QMouseEvent *event)
{
    if (!isWorkspaceMode() || event->button() != Qt::LeftButton
        || m_tool != Tool::Select) {
        return false;
    }
    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // Chrome is painted above all tiles in viewport space; hit-testing must
    // similarly ignore scene z-order of other images under the pointer.
    const QPointF scenePos = mapToScene(event->pos());
    QList<ImageItem *> selected;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (ii->isInteractive() && m_items.contains(ii)) {
                selected.append(ii);
            }
        }
    }
    if (selected.size() > 1) {
        // Multi-select: group frame only (no per-item handles).
        const int gh = groupHandleAt(event->pos(), selected);
        if (gh >= 0 && beginGroupScale(gh, selected)) {
            m_groupXform.setPressScenePos(mapToScene(event->pos()));
            event->accept();
            return true;
        }
    } else if (selected.size() == 1) {
        // Single selection: test that item's handles first, even when the
        // pointer is over another tile's pixmap (handles are drawn on top).
        ImageItem *item = selected.first();
        HandlePressScratch press;
        if (item->beginHandleInteraction(scenePos, event->modifiers(), &press)
            && item->hasActiveHandle()) {
            m_itemInteract.beginHandleDrag(item, captureState(item), press);
            setPageGuideSelected(false);
            event->accept();
            return true;
        }
    }
    // Page guide scale grips when the guide is selected.
    if (m_pageGuide.isInteractive()) {
        const int ph = pageGuideHandleAt(event->pos());
        if (ph >= 0 && beginPageGuideResize(ph)) {
            event->accept();
            return true;
        }
    }
    // No handle hit — fall through to move/select / clear.
    return false;
}

bool ImageView::tryMousePressWorkspaceRotate(QMouseEvent *event)
{
    // Workspace only: Shift + left button free-rotates (unless the press is on
    // a selected item's scale/chrome handle — those use Shift for opposite-edge scale).
    if (!isWorkspaceMode() || event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ShiftModifier)) {
        return false;
    }
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
    if (hit && hit->isSelected() && hit->hasHandleAt(hit->mapFromScene(scenePos))) {
        // Fall through to QGraphicsView → ImageItem handle interaction.
        return false;
    }
    if (!hit) {
        return false;
    }
    m_itemInteract.beginRotate(hit, angleAt(scenePos, hit), hit->itemRotation(),
                               captureState(hit));
    m_scene->clearSelection();
    hit->setSelected(true);
    setCursor(Qt::CrossCursor);
    event->accept();
    return true;
}

bool ImageView::tryMouseMoveWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.isRotating()) {
        return false;
    }
    const QPointF scenePos = mapToScene(event->pos());
    const qreal angle = angleAt(scenePos, m_itemInteract.currentRotateItem());
    // Shift is held to start free-rotate; Ctrl snaps 90°, Shift alone 45°.
    // Stage 2: press-time placement rotation from interact session Placement.
    const qreal rot = PlacementLinear::placementRotationFromDrag(
        m_itemInteract.currentDragStartPlacement().rotation,
        m_itemInteract.currentRotateStartAngle(), angle,
        event->modifiers() & Qt::ControlModifier,
        event->modifiers() & Qt::ShiftModifier);
    m_itemInteract.currentRotateItem()->setItemRotation(rot);
    m_framing.releaseFit();
    emit statusChanged();
    event->accept();
    return true;
}

void ImageView::updateMouseMoveWorkspaceChromeHover(QMouseEvent *event)
{
    // Workspace: drive handle hover from the view so highlight matches the
    // view-owned hit path (rotated / covered items included).
    if (isWorkspaceMode() && m_tool == Tool::Select && !m_itemInteract.isHandleDragging()
        && !m_groupXform.isScaleDrag() && !m_groupXform.isRotateDrag() && !m_chrome.isPanning()) {
        const QPointF scenePos = mapToScene(event->pos());
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    candidates.append(ii);
                }
            }
        }

        // Multi-select: only group handles (individual chrome is hidden).
        if (candidates.size() > 1) {
            for (ImageItem *item : candidates) {
                if (item->hoverHandle() != ImageItem::Handle::None) {
                    item->setHoverHandle(ImageItem::Handle::None);
                }
            }
            const int gh = groupHandleAt(event->pos(), candidates);
            const bool groupHoverChanged = m_groupXform.setHoverHandle(gh);
            if (groupHoverChanged) {
                viewport()->update();
            }
            if (gh >= 0) {
                // 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L; 8–11 rotate
                switch (gh) {
                case 0: case 4: // TL, BR — NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case 2: case 6: // TR, BL — NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case 1: case 5:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case 3: case 7:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case 8: case 9: case 10: case 11:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::ArrowCursor);
                    break;
                }
                if (groupHoverChanged) {
                    const QString tip = GroupTransformGeometry::isRotateHandle(gh)
                        ? tr("Rotate selection")
                        : tr("Scale selection");
                    QToolTip::showText(viewport()->mapToGlobal(event->pos()), tip, viewport());
                }
            } else if (!m_chrome.isPanning()) {
                viewport()->unsetCursor();
                if (groupHoverChanged) {
                    QToolTip::hideText();
                }
            }
        } else {
            if (m_groupXform.hasHoverHandle()) {
                m_groupXform.clearHover();
                viewport()->update();
            }
            ImageItem *hoverOwner = nullptr;
            ImageItem::Handle hoverH = ImageItem::Handle::None;
            std::sort(candidates.begin(), candidates.end(),
                      [](ImageItem *a, ImageItem *b) { return a->stackZ() > b->stackZ(); });
            for (ImageItem *item : candidates) {
                const ImageItem::Handle h = item->handleAt(item->mapFromScene(scenePos));
                if (h != ImageItem::Handle::None) {
                    hoverOwner = item;
                    hoverH = h;
                    break;
                }
            }
            bool hoverChanged = false;
            for (ImageItem *item : candidates) {
                const ImageItem::Handle next =
                    (item == hoverOwner) ? hoverH : ImageItem::Handle::None;
                if (item->hoverHandle() != next) {
                    hoverChanged = true;
                }
                item->setHoverHandle(next);
            }
            if (hoverChanged) {
                viewport()->update();
            }
            if (hoverOwner && hoverH != ImageItem::Handle::None) {
                using H = ImageItem::Handle;
                switch (hoverH) {
                case H::RotateTop: case H::RotateRight:
                case H::RotateBottom: case H::RotateLeft:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                case H::ScaleTopLeft: case H::ScaleBottomRight:
                    // NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case H::ScaleTopRight: case H::ScaleBottomLeft:
                    // NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case H::ScaleTop: case H::ScaleBottom:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::ScaleLeft: case H::ScaleRight:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearTop: case H::ShearBottom:
                    // Horizontal shear: drag along local X (↔ when upright).
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearLeft: case H::ShearRight:
                    // Vertical local shear: drag along local Y (↕ when upright).
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::OpacitySlider:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::PointingHandCursor);
                    break;
                }
                if (hoverChanged) {
                    const QString tip = ImageItem::handleToolTip(hoverH);
                    if (!tip.isEmpty()) {
                        QToolTip::showText(viewport()->mapToGlobal(event->pos()), tip, viewport());
                    } else {
                        QToolTip::hideText();
                    }
                }
            } else if (!m_chrome.isPanning() && !m_itemInteract.isHandleDragging()) {
                viewport()->unsetCursor();
                if (hoverChanged) {
                    QToolTip::hideText();
                }
            }
        }
    }

}

bool ImageView::tryMouseReleaseWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.isRotating() || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_itemInteract.currentRotateItem()) {
        pushItemTransformUndo(m_itemInteract.currentRotateItem(), m_itemInteract.currentDragStartState(),
                              captureState(m_itemInteract.currentRotateItem()), tr("Rotate"));
    }
    m_itemInteract.endRotate();
    restoreToolCursor();
    event->accept();
    return true;
}
