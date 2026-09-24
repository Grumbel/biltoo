// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace viewport chrome and free-form rotate input (owned by WorkspaceController).

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "view/viewframing.h"
#include "item/itemcomponents.h"
#include "imageitem.h"
#include "item/itemhandlepolicy.h"

#include <QMouseEvent>
#include "item/placementlinear.h"
#include "workspace/grouptransformgeometry.h"
#include <QToolTip>
#include <QGraphicsItem>
#include <QGraphicsScene>

bool WorkspaceController::tryMousePressWorkspaceChrome(QMouseEvent *event)
{
    if (!m_view->isWorkspaceMode() || event->button() != Qt::LeftButton
        || m_view->currentTool() != Tool::Select) {
        return false;
    }
    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // Chrome is painted above all tiles in viewport space; hit-testing must
    // similarly ignore scene z-order of other images under the pointer.
    const QPointF scenePos = m_view->mapToScene(event->pos());
    QList<ImageItem *> selected;
    for (QGraphicsItem *gi : m_view->canvasScene()->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (ii->isInteractive() && m_view->liveItems().contains(ii)) {
                selected.append(ii);
            }
        }
    }
    if (selected.size() > 1) {
        // Multi-select: group frame only (no per-item handles).
        const int gh = groupHandleAt(event->pos(), selected);
        if (gh >= 0 && beginGroupScale(gh, selected)) {
            groupSession().setPressScenePos(m_view->mapToScene(event->pos()));
            event->accept();
            return true;
        }
    } else if (selected.size() == 1) {
        // Single selection: test that item's handles first, even when the
        // pointer is over another tile's pixmap (handles are drawn on top).
        ImageItem *item = selected.first();
        HandlePressScratch press;
        if (item->beginHandleInteraction(scenePos, event->modifiers(), &press)
            && press.hasContinuousHandle()) {
            m_itemInteract.beginHandleDrag(item, (item ? item->placement() : ItemComponents::Placement{}), press);
            setPageGuideSelected(false);
            event->accept();
            return true;
        }
    }
    // Page guide scale grips when the guide is selected.
    if (pageGuideSession().isInteractive()) {
        const int ph = pageGuideHandleAt(event->pos());
        if (ph >= 0 && beginPageGuideResize(ph)) {
            event->accept();
            return true;
        }
    }
    // No handle hit — fall through to move/select / clear.
    return false;
}

bool WorkspaceController::tryMousePressWorkspaceRotate(QMouseEvent *event)
{
    // Workspace only: Shift + left button free-rotates (unless the press is on
    // a selected item's scale/chrome handle — those use Shift for opposite-edge scale).
    if (!m_view->isWorkspaceMode() || event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ShiftModifier)) {
        return false;
    }
    ImageItem *hit = nullptr;
    const QPointF scenePos = m_view->mapToScene(event->pos());
    for (QGraphicsItem *gi : m_view->canvasScene()->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = ii;
            break;
        }
    }
    if (!hit) {
        hit = m_view->targetItem();
    }
    if (hit && hit->isSelected() && hit->hasHandleAt(hit->mapFromScene(scenePos))) {
        // Fall through to QGraphicsView → ImageItem handle interaction.
        return false;
    }
    if (!hit) {
        return false;
    }
    m_itemInteract.beginRotate(hit, PlacementLinear::angleAbout(hit->scenePos(), scenePos),
                              (hit ? hit->placement() : ItemComponents::Placement{}));
    m_view->canvasScene()->clearSelection();
    hit->setSelected(true);
    m_view->viewport()->setCursor(Qt::CrossCursor);
    event->accept();
    return true;
}

bool WorkspaceController::tryMouseMoveWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.isRotating()) {
        return false;
    }
    const QPointF scenePos = m_view->mapToScene(event->pos());
    ImageItem *item = m_itemInteract.currentRotateItem();
    const qreal angle = item
        ? PlacementLinear::angleAbout(item->scenePos(), scenePos)
        : 0.0;
    // Shift is held to start free-rotate; Ctrl snaps 90°, Shift alone 45°.
    // Stage 2: press-time placement rotation from interact session Placement.
    const qreal rot = PlacementLinear::placementRotationFromDrag(
        m_itemInteract.currentDragStartPlacement().rotation,
        m_itemInteract.currentRotateStartAngle(), angle,
        event->modifiers() & Qt::ControlModifier,
        event->modifiers() & Qt::ShiftModifier);
    if (!item) {
        return false;
    }
    ItemComponents::Placement pl = m_itemInteract.currentDragStartPlacement();
    pl.rotation = rot;
    item->applyPlacement(pl);
    m_view->hostFraming().releaseFit();
    m_view->notifyStatusChanged();
    event->accept();
    return true;
}

void WorkspaceController::updateMouseMoveWorkspaceChromeHover(QMouseEvent *event)
{
    // Workspace: drive handle hover from the view so highlight matches the
    // view-owned hit path (rotated / covered items included).
    if (m_view->isWorkspaceMode() && m_view->currentTool() == Tool::Select && !m_itemInteract.isHandleDragging()
        && !groupSession().isScaleDrag() && !groupSession().isRotateDrag() && !m_view->hostChrome().isPanning()) {
        const QPointF scenePos = m_view->mapToScene(event->pos());
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_view->canvasScene()->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_view->liveItems().contains(ii)) {
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
            const bool groupHoverChanged = groupSession().setHoverHandle(gh);
            if (groupHoverChanged) {
                m_view->viewport()->update();
            }
            if (gh >= 0) {
                // 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L; 8–11 rotate
                switch (gh) {
                case 0: case 4: // TL, BR — NW–SE diagonal
                    m_view->viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case 2: case 6: // TR, BL — NE–SW diagonal
                    m_view->viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case 1: case 5:
                    m_view->viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case 3: case 7:
                    m_view->viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case 8: case 9: case 10: case 11:
                    m_view->viewport()->setCursor(Qt::CrossCursor);
                    break;
                default:
                    m_view->viewport()->setCursor(Qt::ArrowCursor);
                    break;
                }
                if (groupHoverChanged) {
                    const QString tip = GroupTransformGeometry::isRotateHandle(gh)
                        ? QCoreApplication::translate("ImageView", "Rotate selection")
                        : QCoreApplication::translate("ImageView", "Scale selection");
                    QToolTip::showText(m_view->viewport()->mapToGlobal(event->pos()), tip, m_view->viewport());
                }
            } else if (!m_view->hostChrome().isPanning()) {
                m_view->viewport()->unsetCursor();
                if (groupHoverChanged) {
                    QToolTip::hideText();
                }
            }
        } else {
            if (groupSession().hasHoverHandle()) {
                groupSession().clearHover();
                m_view->viewport()->update();
            }
            ImageItem *hoverOwner = nullptr;
            ImageItem::Handle hoverH = ImageItem::Handle::None;
            std::sort(candidates.begin(), candidates.end(),
                      [](ImageItem *a, ImageItem *b) {
                          return a->placement().z > b->placement().z;
                      });
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
                m_view->viewport()->update();
            }
            if (hoverOwner && hoverH != ImageItem::Handle::None) {
                using H = ImageItem::Handle;
                switch (hoverH) {
                case H::RotateTop: case H::RotateRight:
                case H::RotateBottom: case H::RotateLeft:
                    m_view->viewport()->setCursor(Qt::CrossCursor);
                    break;
                case H::ScaleTopLeft: case H::ScaleBottomRight:
                    // NW–SE diagonal
                    m_view->viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case H::ScaleTopRight: case H::ScaleBottomLeft:
                    // NE–SW diagonal
                    m_view->viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case H::ScaleTop: case H::ScaleBottom:
                    m_view->viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::ScaleLeft: case H::ScaleRight:
                    m_view->viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearTop: case H::ShearBottom:
                    // Horizontal shear: drag along local X (↔ when upright).
                    m_view->viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearLeft: case H::ShearRight:
                    // Vertical local shear: drag along local Y (↕ when upright).
                    m_view->viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::OpacitySlider:
                    m_view->viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                default:
                    m_view->viewport()->setCursor(Qt::PointingHandCursor);
                    break;
                }
                if (hoverChanged) {
                    const QString tip = ItemHandlePolicy::toolTip(hoverH);
                    if (!tip.isEmpty()) {
                        QToolTip::showText(m_view->viewport()->mapToGlobal(event->pos()), tip, m_view->viewport());
                    } else {
                        QToolTip::hideText();
                    }
                }
            } else if (!m_view->hostChrome().isPanning() && !m_itemInteract.isHandleDragging()) {
                m_view->viewport()->unsetCursor();
                if (hoverChanged) {
                    QToolTip::hideText();
                }
            }
        }
    }

}

bool WorkspaceController::tryMouseReleaseWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.isRotating() || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_itemInteract.currentRotateItem()) {
        m_view->hostPushItemTransformUndo(m_itemInteract.currentRotateItem(), m_itemInteract.currentDragStartPlacement(),
                              (m_itemInteract.currentRotateItem() ? m_itemInteract.currentRotateItem()->placement() : ItemComponents::Placement{}), QCoreApplication::translate("ImageView", "Rotate"));
    }
    m_itemInteract.endRotate();
    m_view->restoreToolCursor();
    event->accept();
    return true;
}
