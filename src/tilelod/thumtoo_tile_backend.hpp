// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_THUMTOO_TILE_BACKEND_HPP
#define BILTOO_TILELOD_THUMTOO_TILE_BACKEND_HPP

#include "tilelod/core.hpp"
#include <thumtoo/lod/tile_backend.hpp>

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace tilelod {

/**
 * TileBackend for one session path via thumtoo `request_tile_cells`
 * (ThumtooCache::requestTileCells). thumtoo guarantees exactly one result per
 * cell, including Cancelled — see thumtoo TILES.md "Interactive cell contract".
 * Without thumtoo every cell fails immediately with a reason.
 */
class ThumtooTileBackend : public TileBackend {
public:
  explicit ThumtooTileBackend(QString path);

  void fetch(std::vector<FetchRequest> cells, ResultFn on_result) override;
  void cancel(std::vector<TileKey> const& keys) override;

  QString const& path() const { return m_path; }

private:
  QString m_path;
};

/**
 * Decode encoded TileBlob-like bytes into rgba8 TileBitmap.
 * codec: "jpeg", "rgb888", "rgba8", or empty (try QImage::fromData).
 */
[[nodiscard]] std::optional<TileBitmap> decode_tile_payload(
    int width, int height, std::string const& codec,
    std::vector<std::uint8_t> const& bytes);

}  // namespace tilelod

#endif
