// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_painter.hpp"

#include <QByteArray>
#include <algorithm>
#include <cstring>

namespace tilelod {

QImage tile_bitmap_to_qimage(TileBitmap const& bitmap)
{
  if (!bitmap.valid()) {
    return {};
  }
  if (bitmap.codec.empty() || bitmap.codec == "rgba8") {
    QImage img(bitmap.width, bitmap.height, QImage::Format_RGBA8888);
    if (img.isNull()) {
      return {};
    }
    size_t const need =
        static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height) * 4u;
    if (bitmap.bytes.size() < need) {
      return {};
    }
    for (int y = 0; y < bitmap.height; ++y) {
      uchar* dst = img.scanLine(y);
      auto const* src =
          bitmap.bytes.data() + static_cast<size_t>(y * bitmap.width * 4);
      std::memcpy(dst, src, static_cast<size_t>(bitmap.width * 4));
    }
    return img;
  }
  if (bitmap.codec == "rgb888") {
    QImage img(bitmap.width, bitmap.height, QImage::Format_RGB888);
    if (img.isNull()) {
      return {};
    }
    size_t const need =
        static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height) * 3u;
    if (bitmap.bytes.size() < need) {
      return {};
    }
    for (int y = 0; y < bitmap.height; ++y) {
      uchar* dst = img.scanLine(y);
      auto const* src =
          bitmap.bytes.data() + static_cast<size_t>(y * bitmap.width * 3);
      std::memcpy(dst, src, static_cast<size_t>(bitmap.width * 3));
    }
    return img;
  }
  // jpeg / other encoded payloads
  QByteArray ba(reinterpret_cast<char const*>(bitmap.bytes.data()),
                static_cast<int>(bitmap.bytes.size()));
  QImage img = QImage::fromData(ba);
  return img;
}

void paint_draw_plan(QPainter* painter, PaintDrawPlanArgs const& args)
{
  if (!painter || !args.plan) {
    return;
  }
  painter->setRenderHint(QPainter::SmoothPixmapTransform, args.smooth);

  // When smooth: ExactTile with +1 overlap expands dest 1:1 with full bitmap so
  // bilinear can sample the shared edge (exclusive 256→256 clamps and seams).
  // When not smooth: exclusive src→dest only (pixel-exact; no 257→256 scale).
  // Expand is right/bottom → paint high x/y first so lower cell's strip wins.
  std::vector<DrawCommand const*> ordered;
  ordered.reserve(args.plan->commands.size());
  for (DrawCommand const& cmd : args.plan->commands) {
    ordered.push_back(&cmd);
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [smooth = args.smooth](DrawCommand const* a, DrawCommand const* b) {
                     if (smooth) {
                       if (a->src_key.y != b->src_key.y) {
                         return a->src_key.y > b->src_key.y;
                       }
                       if (a->src_key.x != b->src_key.x) {
                         return a->src_key.x > b->src_key.x;
                       }
                     } else {
                       if (a->src_key.y != b->src_key.y) {
                         return a->src_key.y < b->src_key.y;
                       }
                       if (a->src_key.x != b->src_key.x) {
                         return a->src_key.x < b->src_key.x;
                       }
                     }
                     return static_cast<int>(a->kind) < static_cast<int>(b->kind);
                   });

  for (DrawCommand const* pcmd : ordered) {
    DrawCommand const& cmd = *pcmd;
    if (cmd.dst_content.empty()) {
      continue;
    }
    QRectF dst(cmd.dst_content.x, cmd.dst_content.y, cmd.dst_content.w,
               cmd.dst_content.h);

    if (cmd.kind == DrawKind::Underlay && cmd.use_lqip && !args.lqip.isNull()) {
      painter->drawImage(dst, args.lqip);
      continue;
    }
    if (cmd.kind != DrawKind::ExactTile && cmd.kind != DrawKind::CoarserTile) {
      continue;
    }
    if (!args.resolve) {
      continue;
    }
    TileBitmap dummy;
    QImage img = args.resolve(cmd.src_key, dummy);
    if (img.isNull()) {
      continue;
    }
    QRectF src(cmd.src_uv.x, cmd.src_uv.y, cmd.src_uv.w, cmd.src_uv.h);
    if (cmd.kind == DrawKind::ExactTile && args.smooth) {
      int const sc = cmd.src_key.scale;
      int const factor = (sc > 0) ? (1 << sc) : 1;
      int const exclW =
          dst.width() > 0.0 ? int(dst.width() / double(factor) + 0.5) : 1;
      int const exclH =
          dst.height() > 0.0 ? int(dst.height() / double(factor) + 0.5) : 1;
      // 1:1 map full bitmap into dest grown by the overlap strip (not 257→256).
      if (img.width() > exclW) {
        dst.setWidth(dst.width()
                     + double(img.width() - exclW) * double(factor));
        src = QRectF(0, 0, img.width(), src.height() > 0 ? src.height() : img.height());
      }
      if (img.height() > exclH) {
        dst.setHeight(dst.height()
                      + double(img.height() - exclH) * double(factor));
        src = QRectF(src.x(), 0, src.width() > 0 ? src.width() : img.width(),
                     img.height());
      }
      if (img.width() > exclW || img.height() > exclH) {
        src = QRectF(0, 0, img.width(), img.height());
      }
    }
    painter->drawImage(dst, img, src);
  }
}

}  // namespace tilelod
