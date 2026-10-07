// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * Shared tile-plan debug overlay (Debug → Tile plan overlay / BILTOO_TILE_DEBUG).
 * Used by ImageItem paint and Gallery virtual-slot paint — same HUD, no special cases.
 */

#include "tilelod/core.hpp"
#include <thumtoo/lod/draw_plan.hpp>
#include <thumtoo/lod/tile_session.hpp>
#include "content/contentxform.h"

#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

[[nodiscard]] bool tilePlanDebugOverlayEnabled();

/**
 * Paint Exact/Parent/Underlay cell washes + centred TILE / s=N / COMPLETE summary.
 * @p contentBounds is the item content box in the painter's current space.
 * @p contentOffset is ImageItem::offset() (usually zero for virtual slots).
 */
void paintTilePlanDebugOverlay(QPainter *painter, tilelod::TileSession *session,
                               const tilelod::DrawPlan &plan,
                               const QRectF &contentBounds,
                               const QPointF &contentOffset,
                               const QSize &nativeSize,
                               const ContentXform::Value &xform,
                               bool freeRotPainter,
                               const QString &itemPath);
