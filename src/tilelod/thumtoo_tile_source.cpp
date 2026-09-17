// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/thumtoo_tile_source.hpp"

#include "tilelod/tile_painter.hpp"

#include <QImage>
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

ThumtooTileSource::ThumtooTileSource(std::string uri, FetchFn fetch,
                                     CancelFn cancel)
    : m_uri(std::move(uri)), m_fetch(std::move(fetch)), m_cancel(std::move(cancel))
{
}

void ThumtooTileSource::set_uri(std::string uri)
{
  std::lock_guard<std::mutex> lock(m_epoch->mu);
  m_uri = std::move(uri);
  ++m_epoch->batch_id;
}

void ThumtooTileSource::request(std::vector<TileKey> keys, Completion on_each)
{
  if (!m_fetch || keys.empty() || !on_each) {
    return;
  }
  std::string uri;
  std::uint64_t epoch = 0;
  std::shared_ptr<EpochState> epochState = m_epoch;
  {
    std::lock_guard<std::mutex> lock(epochState->mu);
    uri = m_uri;
    // Capture epoch only — do NOT increment. Every request used to ++batch_id,
    // so a second issue_requests dropped all completions from the first and left
    // keys stuck InFlight in the RAM cache.
    epoch = epochState->batch_id;
  }
  if (uri.empty()) {
    for (TileKey const& k : keys) {
      on_each(k, std::nullopt);
    }
    return;
  }

  std::vector<TileCoord> coords;
  coords.reserve(keys.size());
  for (TileKey const& k : keys) {
    coords.push_back({k.scale, k.x, k.y});
  }

  // Keep keys by index for the batch callback.
  auto keyCopy = std::make_shared<std::vector<TileKey>>(std::move(keys));
  auto cb = std::make_shared<Completion>(std::move(on_each));

  // Capture epochState (not this): completions can outlive the TileSource when
  // the path is unbound while thumtoo still delivers durable/encode hits.
  m_fetch(uri, coords,
          [epochState, epoch, keyCopy, cb](std::size_t index,
                                           std::optional<TileBitmap> bitmap) {
            {
              std::lock_guard<std::mutex> lock(epochState->mu);
              if (epoch != epochState->batch_id) {
                return;
              }
            }
            if (index >= keyCopy->size()) {
              return;
            }
            (*cb)((*keyCopy)[index], std::move(bitmap));
          });
}

void ThumtooTileSource::cancel(std::vector<TileKey> const& keys)
{
  if (!m_cancel || keys.empty()) {
    return;
  }
  std::string uri;
  {
    std::lock_guard<std::mutex> lock(m_epoch->mu);
    uri = m_uri;
  }
  if (uri.empty()) {
    return;
  }
  std::vector<TileCoord> coords;
  coords.reserve(keys.size());
  for (TileKey const& k : keys) {
    coords.push_back({k.scale, k.x, k.y});
  }
  m_cancel(uri, coords);
}

void ThumtooTileSource::cancel_all()
{
  std::string uri;
  {
    std::lock_guard<std::mutex> lock(m_epoch->mu);
    ++m_epoch->batch_id;
    uri = m_uri;
  }
  if (m_cancel && !uri.empty()) {
    m_cancel(uri, {});
  }
}

}  // namespace tilelod
