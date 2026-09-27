// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_display_paint.hpp"
#include "tilelod/tile_plan_debug_overlay.hpp"

#include <QTransform>
#include <QVector>
#include <QtMath>

namespace tilelod {

bool paint_tiles_display(PaintTilesDisplayArgs const &args)
{
  if (!args.painter || !args.session || !args.plan || !args.resolve) {
    return false;
  }
  if (args.native.width() < 1 || args.native.height() < 1) {
    return false;
  }

  ContentXform::Value const &x = args.xform;
  QPointF const &off = args.contentOffset;
  const bool orient = x.hFlip || x.vFlip
      || (ContentXform::normalizeQuarterTurns(x.quarterTurns) != 0);
  const bool freeRot = args.freeRotPainter;

  auto orientPatch = [&x](QImage patch) -> QImage {
    if (patch.isNull()) {
      return patch;
    }
    if (x.hFlip || x.vFlip) {
      Qt::Orientations axes;
      if (x.hFlip) {
        axes |= Qt::Horizontal;
      }
      if (x.vFlip) {
        axes |= Qt::Vertical;
      }
      if (axes) {
        patch = patch.flipped(axes);
      }
    }
    const int turns = ContentXform::normalizeQuarterTurns(x.quarterTurns);
    if (turns != 0) {
      QTransform rot;
      rot.rotate(90.0 * turns);
      patch = patch.transformed(rot, Qt::FastTransformation);
    }
    return patch;
  };

  auto extractUv = [](const QImage &img, const RectF &uv) -> QImage {
    if (img.isNull()) {
      return {};
    }
    const QRectF srcUv(uv.x, uv.y, uv.w, uv.h);
    if (srcUv.width() < 1.0 || srcUv.height() < 1.0) {
      return img;
    }
    if (srcUv.x() <= 0.5 && srcUv.y() <= 0.5
        && srcUv.width() + 0.5 >= img.width()
        && srcUv.height() + 0.5 >= img.height()) {
      return img;
    }
    const QRect ir = srcUv.toAlignedRect().intersected(img.rect());
    if (ir.isEmpty()) {
      return {};
    }
    return img.copy(ir);
  };

  struct TilePaintCmd {
    QRectF dst;
    QImage patch;
  };
  QVector<TilePaintCmd> paintCmds;
  paintCmds.reserve(static_cast<int>(args.plan->commands.size()));

  for (DrawCommand const &cmd : args.plan->commands) {
    if (cmd.kind != DrawKind::ExactTile && cmd.kind != DrawKind::CoarserTile) {
      continue;
    }
    QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y, cmd.dst_content.w,
                  cmd.dst_content.h);
    QImage img = args.resolve(cmd.src_key);
    if (img.isNull()) {
      continue;
    }
    QImage patch = extractUv(img, cmd.src_uv);
    if (patch.isNull()) {
      continue;
    }
    if (orient) {
      patch = orientPatch(patch);
    }
    if (patch.isNull()) {
      continue;
    }

    if (freeRot) {
      QRectF ori = ContentXform::mapSourceRectToOriented(srcBox, args.native, x);
      if (ori.isEmpty()) {
        continue;
      }
      paintCmds.push_back({ori, patch});
      continue;
    }

    QRectF oriented = ContentXform::mapSourceRectToOriented(srcBox, args.native, x);
    if (oriented.isEmpty()) {
      continue;
    }
    QRectF disp = oriented;
    if (x.hasCrop && !x.cropRect.isEmpty()) {
      const QRect contentCrop = ContentXform::orientedCropRect(args.native, x);
      if (contentCrop.width() < 1 || contentCrop.height() < 1) {
        continue;
      }
      const QRectF local = oriented.translated(-contentCrop.x(), -contentCrop.y());
      const QRectF cropLocal(0.0, 0.0, contentCrop.width(), contentCrop.height());
      disp = local.intersected(cropLocal);
      if (disp.isEmpty() || local.width() < 1e-6 || local.height() < 1e-6) {
        continue;
      }
      if (qAbs(disp.width() - local.width()) > 0.5
          || qAbs(disp.height() - local.height()) > 0.5) {
        const qreal u0 = (disp.left() - local.left()) / local.width();
        const qreal v0 = (disp.top() - local.top()) / local.height();
        const qreal uw = disp.width() / local.width();
        const qreal vh = disp.height() / local.height();
        const QRect pr =
            QRectF(u0 * patch.width(), v0 * patch.height(), uw * patch.width(),
                   vh * patch.height())
                .toAlignedRect()
                .intersected(patch.rect());
        if (pr.isEmpty()) {
          continue;
        }
        patch = patch.copy(pr);
      }
    }
    paintCmds.push_back({QRectF(disp.x() + off.x(), disp.y() + off.y(),
                                disp.width(), disp.height()),
                         patch});
  }

  if (paintCmds.isEmpty() && !args.debugOverlay) {
    return false;
  }

  QPainter *painter = args.painter;
  painter->save();
  if (!args.contentBounds.isEmpty()) {
    painter->setClipRect(args.contentBounds, Qt::IntersectClip);
  }
  if (freeRot && x.hasCrop && !x.cropRect.isEmpty()) {
    const QRect contentCrop = ContentXform::orientedCropRect(args.native, x);
    if (contentCrop.width() >= 1 && contentCrop.height() >= 1
        && !args.contentBounds.isEmpty()) {
      painter->translate(args.contentBounds.center());
      painter->rotate(-x.cropRotation);
      painter->translate(-QPointF(contentCrop.center()));
    }
  }

  painter->setRenderHint(QPainter::SmoothPixmapTransform, args.smooth);
  bool drew = false;
  for (TilePaintCmd const &pc : paintCmds) {
    if (pc.patch.isNull() || pc.dst.isEmpty()) {
      continue;
    }
    painter->drawImage(pc.dst, pc.patch);
    drew = true;
  }

  if (args.debugOverlay) {
    paintTilePlanDebugOverlay(painter, args.session, *args.plan,
                              args.contentBounds, off, args.native, x, freeRot,
                              args.path);
  }
  painter->restore();
  return drew;
}

}  // namespace tilelod
