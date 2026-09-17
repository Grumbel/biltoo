// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_SOURCE_HPP
#define BILTOO_TILELOD_TILE_SOURCE_HPP

#include "tilelod/tile_types.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace tilelod {

/**
 * Async tile backend. Completions may arrive on any thread; TileSession
 * queues them and applies on pump().
 *
 * Production: thumtoo Client. Tests: FakeTileSource.
 */
class TileSource {
public:
  virtual ~TileSource() = default;

  using Completion = std::function<void(TileKey key, std::optional<TileBitmap> bitmap)>;

  /// Request tiles; invoke on_each once per key (success or nullopt failure).
  virtual void request(std::vector<TileKey> keys, Completion on_each) = 0;

  /// Best-effort cancel; in-flight work may still complete (session ignores stale).
  virtual void cancel(std::vector<TileKey> const& keys) = 0;

  virtual void cancel_all() = 0;
};

}  // namespace tilelod

#endif
