// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_painter.hpp"

#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

namespace {

constexpr int kMaxAssemblePixels = 64 * 1024 * 1024;

bool assemble_patches(QPainter* painter, std::vector<TilePatchBlit> const& patches)
{
  if (!painter || patches.empty()) {
    return false;
  }
  QRectF bbox = patches.front().dst;
  for (size_t i = 1; i < patches.size(); ++i) {
    if (!patches[i].dst.isEmpty()) {
      bbox = bbox.united(patches[i].dst);
    }
  }
  if (bbox.width() < 1.0 || bbox.height() < 1.0) {
    return false;
  }
  int const x0 = static_cast<int>(std::floor(bbox.x()));
  int const y0 = static_cast<int>(std::floor(bbox.y()));
  int const x1 = static_cast<int>(std::ceil(bbox.right()));
  int const y1 = static_cast<int>(std::ceil(bbox.bottom()));
  int const bw = x1 - x0;
  int const bh = y1 - y0;
  if (bw < 1 || bh < 1) {
    return false;
  }
  if (static_cast<qint64>(bw) * static_cast<qint64>(bh) > kMaxAssemblePixels) {
    return false;
  }

  QImage buf(bw, bh, QImage::Format_ARGB32_Premultiplied);
  if (buf.isNull()) {
    return false;
  }
  buf.fill(Qt::transparent);

  {
    QPainter bp(&buf);
    bp.setRenderHint(QPainter::SmoothPixmapTransform, false);
    bp.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (TilePatchBlit const& p : patches) {
      if (p.patch.isNull() || p.dst.isEmpty()) {
        continue;
      }
      bp.drawImage(p.dst.translated(static_cast<qreal>(-x0), static_cast<qreal>(-y0)),
                   p.patch);
    }
  }

  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter->drawImage(QRectF(x0, y0, bw, bh), buf);
  return true;
}

}  // namespace

void paint_tile_patches(QPainter* painter, std::vector<TilePatchBlit> const& patches,
                        bool smooth)
{
  if (!painter || patches.empty()) {
    return;
  }
  if (smooth && assemble_patches(painter, patches)) {
    return;
  }
  painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth);
  for (TilePatchBlit const& p : patches) {
    if (p.patch.isNull() || p.dst.isEmpty()) {
      continue;
    }
    painter->drawImage(p.dst, p.patch);
  }
}

void paint_draw_plan(QPainter* painter, PaintDrawPlanArgs const& args)
{
  if (!painter || !args.plan) {
    return;
  }

  std::vector<TilePatchBlit> patches;
  patches.reserve(args.plan->commands.size());

  for (DrawCommand const& cmd : args.plan->commands) {
    if (cmd.dst_content.empty()) {
      continue;
    }
    QRectF const dst(cmd.dst_content.x, cmd.dst_content.y, cmd.dst_content.w,
                     cmd.dst_content.h);

    if (cmd.kind == DrawKind::Underlay && cmd.use_lqip && !args.lqip.isNull()) {
      painter->setRenderHint(QPainter::SmoothPixmapTransform, args.smooth);
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
    QRectF const src(cmd.src_uv.x, cmd.src_uv.y, cmd.src_uv.w, cmd.src_uv.h);
    // Copy exclusive subrect into a standalone image so assembly stays 1:1.
    QImage patch = img;
    if (src.x() > 0.5 || src.y() > 0.5 || src.width() + 0.5 < img.width()
        || src.height() + 0.5 < img.height()) {
      QRect const ir = src.toAlignedRect().intersected(img.rect());
      if (ir.isEmpty()) {
        continue;
      }
      patch = img.copy(ir);
    }
    if (patch.isNull()) {
      continue;
    }
    patches.push_back({dst, patch});
  }

  paint_tile_patches(painter, patches, args.smooth);
}

}  // namespace tilelod
