// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/thumtoo_tile_backend.hpp"

#include "host/thumtoocache.h"
#include "tilelod/tile_painter.hpp"

#include <QImage>
#include <QVector>
#include <cstring>
#include <memory>

namespace tilelod {

std::optional<TileBitmap> decode_tile_payload(
    int width, int height, std::string const& codec,
    std::vector<std::uint8_t> const& bytes)
{
  if (bytes.empty()) {
    return std::nullopt;
  }
  TileBitmap raw;
  raw.width = width;
  raw.height = height;
  raw.codec = codec;
  raw.bytes = bytes;

  // Prefer keeping rgba8 in the RAM cache so the paint path stays cheap.
  if (codec == "rgba8" || codec.empty()) {
    if (width > 0 && height > 0 &&
        bytes.size() >= static_cast<size_t>(width * height * 4)) {
      raw.codec = "rgba8";
      return raw;
    }
  }
  if (codec == "rgb888" && width > 0 && height > 0 &&
      bytes.size() >= static_cast<size_t>(width * height * 3)) {
    TileBitmap out;
    out.width = width;
    out.height = height;
    out.codec = "rgba8";
    out.bytes.resize(static_cast<size_t>(width * height * 4));
    for (int i = 0; i < width * height; ++i) {
      out.bytes[static_cast<size_t>(i * 4 + 0)] = bytes[static_cast<size_t>(i * 3 + 0)];
      out.bytes[static_cast<size_t>(i * 4 + 1)] = bytes[static_cast<size_t>(i * 3 + 1)];
      out.bytes[static_cast<size_t>(i * 4 + 2)] = bytes[static_cast<size_t>(i * 3 + 2)];
      out.bytes[static_cast<size_t>(i * 4 + 3)] = 255;
    }
    return out;
  }

  QImage img = tile_bitmap_to_qimage(raw);
  if (img.isNull()) {
    return std::nullopt;
  }
  img = img.convertToFormat(QImage::Format_RGBA8888);
  TileBitmap out;
  out.width = img.width();
  out.height = img.height();
  out.codec = "rgba8";
  out.bytes.resize(static_cast<size_t>(out.width * out.height * 4));
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(out.bytes.data() + static_cast<size_t>(y * out.width * 4),
                img.constScanLine(y), static_cast<size_t>(out.width * 4));
  }
  return out;
}

ThumtooTileBackend::ThumtooTileBackend(QString path)
    : m_path(std::move(path))
{
}

void ThumtooTileBackend::fetch(std::vector<FetchRequest> cells, ResultFn on_result)
{
  if (cells.empty() || !on_result) {
    return;
  }
  QVector<ThumtooCache::TileCoord> coords;
  coords.reserve(static_cast<int>(cells.size()));
  for (FetchRequest const& c : cells) {
    coords.push_back({c.key.scale, c.key.x, c.key.y});
  }
  auto reqs = std::make_shared<std::vector<FetchRequest>>(std::move(cells));
  ThumtooCache::requestTileCells(
      m_path, coords,
      [reqs, on_result = std::move(on_result)](std::size_t index,
                                               ThumtooCache::TileCellResult r) {
        if (index >= reqs->size()) {
          return;
        }
        FetchResult out;
        out.key = (*reqs)[index].key;
        out.ticket = (*reqs)[index].ticket;
        out.status = r.status;
        out.error = std::move(r.error);
        if (r.status == FetchStatus::Ok) {
          if (r.tile && r.tile->valid()) {
            out.bitmap = std::move(*r.tile);
          } else {
            out.status = FetchStatus::Failed;
            out.error = "tile payload could not be decoded";
          }
        }
        on_result(std::move(out));
      });
}

void ThumtooTileBackend::cancel(std::vector<TileKey> const& keys)
{
  if (keys.empty()) {
    return;
  }
  QVector<ThumtooCache::TileCoord> coords;
  coords.reserve(static_cast<int>(keys.size()));
  for (TileKey const& k : keys) {
    coords.push_back({k.scale, k.x, k.y});
  }
  (void)ThumtooCache::cancelTileCells(m_path, coords);
}

}  // namespace tilelod
