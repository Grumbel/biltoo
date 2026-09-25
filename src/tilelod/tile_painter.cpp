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

  // Stable overdraw: left→right, top→bottom so +overlap strips win.
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
                     // Exact over coarser when same cell key space differs
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
      // Stretch LQIP into the cell's content region (placeholder until tiles).
      painter->drawImage(dst, args.lqip);
      continue;
    }
    if (cmd.kind != DrawKind::ExactTile && cmd.kind != DrawKind::CoarserTile) {
      continue;
    }
    if (!args.resolve) {
      continue;
    }
    // resolve is expected to load from session cache; pass empty bitmap hint
    TileBitmap dummy;
    QImage img = args.resolve(cmd.src_key, dummy);
    if (img.isNull()) {
      continue;
    }
    // ExactTile only: expand exclusive dest when bitmap has +kTileOverlap.
    // CoarserTile uses parent UV into a subrect — must not expand from parent size.
    QRectF src(cmd.src_uv.x, cmd.src_uv.y, cmd.src_uv.w, cmd.src_uv.h);
    if (cmd.kind == DrawKind::ExactTile) {
      int const sc = cmd.src_key.scale;
      int const factor = (sc > 0) ? (1 << sc) : 1;
      int const exclW =
          dst.width() > 0.0 ? int(dst.width() / double(factor) + 0.5) : 1;
      int const exclH =
          dst.height() > 0.0 ? int(dst.height() / double(factor) + 0.5) : 1;
      if (img.width() > exclW) {
        dst.setWidth(dst.width() + double(img.width() - exclW) * double(factor));
      }
      if (img.height() > exclH) {
        dst.setHeight(dst.height()
                      + double(img.height() - exclH) * double(factor));
      }
      src = QRectF(0, 0, img.width(), img.height());
    }
    // QPainter hairline gaps: overdraw ~¾ device-pixel in content space when
    // caller passes device_per_content; else a fixed half content-unit.
    {
      double gap = 0.5;
      if (args.device_per_content > 1e-9) {
        gap = 0.75 / args.device_per_content;
      }
      dst.adjust(-gap, -gap, gap, gap);
    }
    painter->drawImage(dst, img, src);
  }
}

}  // namespace tilelod
