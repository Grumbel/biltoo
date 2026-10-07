// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_CORE_HPP
#define BILTOO_TILELOD_CORE_HPP

#include <thumtoo/lod/tile_types.hpp>

/// The Qt-free tile LOD core (planner, TileLoader, TileScheduler,
/// TileSession, draw plan, source records) lives in thumtoo as
/// thumtoo::lod, shared with Galapix (thumtoo docs/TILE_LOD.md). biltoo's
/// Qt glue stays in namespace tilelod and sees the core names through this
/// using-directive, so tilelod::TileSession and friends keep working.
namespace tilelod {
using namespace thumtoo::lod;
}  // namespace tilelod

#endif
