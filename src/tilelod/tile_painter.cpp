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

  // Exclusive dests abut — order is only for determinism.
  // ExactTile src_uv is exclusive level pixels (not the +1 overlap strip):
  // mapping 257 source → 256 dest scaled every cell and broke checkerboards.
  std::vector<DrawCommand const*> ordered;
  ordered.reserve(args.plan->commands.size());
  for (DrawCommand const& cmd : args.plan->commands) {
    ordered.push_back(&cmd);
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](DrawCommand const* a, DrawCommand const* b) {
                     if (a->src_key.y != b->src_key.y) {
                       return a->src_key.y < b->src_key.y;
                     }
                     if (a->src_key.x != b->src_key.x) {
                       return a->src_key.x < b->src_key.x;
                     }
                     return static_cast<int>(a->kind) < static_cast<int>(b->kind);
                   });

  for (DrawCommand const* pcmd : ordered) {
    DrawCommand const& cmd = *pcmd;
    if (cmd.dst_content.empty()) {
      continue;
    }
    QRectF const dst(cmd.dst_content.x, cmd.dst_content.y, cmd.dst_content.w,
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
    // src_uv from plan: exclusive for ExactTile; parent subrect for CoarserTile.
    QRectF const src(cmd.src_uv.x, cmd.src_uv.y, cmd.src_uv.w, cmd.src_uv.h);
    painter->drawImage(dst, img, src);
  }
}

}  // namespace tilelod
