// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_painter.hpp"

#include <QByteArray>
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
  QByteArray ba(reinterpret_cast<char const*>(bitmap.bytes.data()),
                static_cast<int>(bitmap.bytes.size()));
  return QImage::fromData(ba);
}

void paint_draw_plan(QPainter* painter, PaintDrawPlanArgs const& args)
{
  if (!painter || !args.plan) {
    return;
  }
  painter->setRenderHint(QPainter::SmoothPixmapTransform, args.smooth);
  // Half device-pixel overdraw on right/bottom closes hairline gaps between
  // exclusive cells under float transforms (not a content-line of overdraw).
  double gap = 0.0;
  if (args.device_per_content > 1e-9) {
    gap = 0.5 / args.device_per_content;
    if (gap > 0.25) {
      gap = 0.25;
    }
  }

  for (DrawCommand const& cmd : args.plan->commands) {
    if (cmd.dst_content.empty()) {
      continue;
    }
    QRectF dst(cmd.dst_content.x, cmd.dst_content.y, cmd.dst_content.w,
               cmd.dst_content.h);
    if (gap > 0.0) {
      dst.adjust(0.0, 0.0, gap, gap);
    }

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
    // Exclusive src_uv from plan.
    QRectF const src(cmd.src_uv.x, cmd.src_uv.y, cmd.src_uv.w, cmd.src_uv.h);
    painter->drawImage(dst, img, src);
  }
}

}  // namespace tilelod
