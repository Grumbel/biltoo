// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_THUMTOO_TILE_SOURCE_HPP
#define BILTOO_TILELOD_THUMTOO_TILE_SOURCE_HPP

#include "tilelod/tile_source.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace tilelod {

/**
 * TileSource backed by thumtoo Client tile APIs.
 *
 * Does not include thumtoo headers here so translation units without
 * BILTOO_HAVE_THUMTOO can still see the type. Construction requires a live
 * FetchFn that calls Client::request_tiles / get_tile.
 *
 * Decode: JPEG/rgb888 → rgba8 TileBitmap off the completion path (caller may
 * already be on a worker via thumtoo Executor).
 */
class ThumtooTileSource : public TileSource {
public:
  struct TileCoord {
    int scale = 0;
    int x = 0;
    int y = 0;
  };

  /**
   * Request a batch of tiles for the bound URI.
   * on_cell(index, optional rgba TileBitmap) — index matches coords order.
   */
  using FetchFn = std::function<void(
      std::string const& uri, std::vector<TileCoord> const& coords,
      std::function<void(std::size_t index, std::optional<TileBitmap>)> on_cell)>;

  using CancelFn = std::function<void(std::string const& uri,
                                      std::vector<TileCoord> const& coords)>;

  ThumtooTileSource(std::string uri, FetchFn fetch, CancelFn cancel = {});

  void set_uri(std::string uri);
  std::string const& uri() const { return m_uri; }

  void request(std::vector<TileKey> keys, Completion on_each) override;
  void cancel(std::vector<TileKey> const& keys) override;
  void cancel_all() override;

private:
  std::string m_uri;
  FetchFn m_fetch;
  CancelFn m_cancel;
  std::mutex m_mu;
  std::uint64_t m_batch_id = 0;
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
