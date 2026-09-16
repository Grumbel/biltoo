<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Environment variables

Biltoo and the embedded thumtoo client honour a number of environment
variables for debugging, concurrency limits, and the Nix development shell.
None of these are required for normal use.

Flag-style variables treat a value as **on** when it is non-empty and not
`0`, `f`/`F`, or `n`/`N` (case-insensitive first character). Numeric limits
clamp to the ranges noted below.

Also see CLI flags `--debug` and `--thumtoo-debug` in [README.md](../README.md)
and `man biltoo`.

---

## Runtime debug traces

| Variable | Effect |
|----------|--------|
| **`THUMTOO_DEBUG`** | Enable thumtoo ladder / tile / interest traces on stderr and append to `$XDG_CACHE_HOME/biltoo/thumtoo-debug.log` (or `~/.cache/biltoo/…`). Also enables some biltoo load-path traces that key off the same name. |
| **`BILTOO_THUMTOO_DEBUG`** | Same as `THUMTOO_DEBUG` (biltoo-prefixed alias). |
| **`BILTOO_LOAD_DEBUG`** | Extra ImageView load-pipeline timestamps (also implied by `THUMTOO_DEBUG` / `BILTOO_THUMTOO_DEBUG`). |
| **`BILTOO_DEBUG_SLIDESHOW`** | Slideshow transition / handoff traces (also implied when `THUMTOO_DEBUG` is on). |
| **`BILTOO_DEBUG_FILMSTRIP`** | Filmstrip (thumbnail bar) schedule and soft-sample diagnostics. |
| **`BILTOO_DEBUG_DROP`** | Drag-and-drop / session-append path logging. |
| **`BILTOO_DEBUG_CROP`** | Crop-mode geometry and bake diagnostics. |
| **`BILTOO_DEBUG_APPEARANCE`** | Session appearance / materialize path logging. |
| **`BILTOO_PERF`** | Paint and decode-window timing (FPS-style HUD path). Also enabled when `THUMTOO_DEBUG` is on. |
| **`THUMTOO_DEBUG_OVERLAY`** / **`BILTOO_DEBUG_OVERLAY`** | Stamp a tiled watermark + border on decoded samples so soft vs full vs host origin is visible on the canvas. |

### Thumtoo cache policy (library ≥ 234)

| Variable | Effect |
|----------|--------|
| *(default)* | **Tiles-first:** soft/overview levels are not written; tiles + full_native still are. |
| **`THUMTOO_SOFT_LEVELS=1`** | Restore durable soft/overview level writes (rollback). |
| **`THUMTOO_TILES_ONLY=0`** | Same as soft levels on. |
| **`THUMTOO_TILES_ONLY=1`** | Explicit tiles-first (same as default). |

Requires biltoo ≥1007 (`scheduleSoftPixels`) for filmstrip/Gallery soft via PreferCache when tiles exist.
With **thumtoo ≥ 280**, PreferCache is fully Store-backed (no legacy soft levels);
`scheduleSoftPixels` uses TileSynth/PreferCache when durable tiles exist, else SoftOnly.

CLI `--debug` turns on `biltoo.slideshow` Qt logging categories and libexiv2
warnings, and enables thumtoo debug the same way as the env vars above.
`--thumtoo-debug` enables only the thumtoo traces.

---

## Concurrency knobs

| Variable | Default | Range | Effect |
|----------|---------|-------|--------|
| **`BILTOO_THUMTOO_PIXEL_JOBS`** | 8 | 1–16 | Max concurrent thumtoo `request_pixels` jobs (soft/ladder band). |
| **`BILTOO_GALLERY_DECODE_CONCURRENCY`** | (compile-time `kMaxConcurrentGalleryDecodes`) | 1–32 | Concurrent Gallery soft-decode workers. |
| **`BILTOO_FILMSTRIP_THUMB_LOADS`** | 24 | 1–64 | Concurrent filmstrip thumbnail load jobs (separate from thumtoo pixel jobs). |

---

## Paths (standard)

| Variable | Effect |
|----------|--------|
| **`XDG_CACHE_HOME`** | Base for durable cache and `thumtoo-debug.log` (`$XDG_CACHE_HOME/biltoo/`). Thumtoo pixel cache: `$XDG_CACHE_HOME/thumtoo/`. |
| **`XDG_DATA_HOME`** | Thumtoo user overlays (`user.sqlite`: tags, collections) at `$XDG_DATA_HOME/thumtoo/` so they survive cache wipe. Passed as `Client::open` `data_root`. |
| **`HOME`** | Fallback when `XDG_*` is unset (`~/.cache/…`, `~/.local/share/thumtoo`). |

Qt settings still follow the usual Qt paths (e.g. `~/.config/biltoo/biltoo.conf`
on Linux).

---

## Development shell (`nix develop`)

These are set or consumed by the flake helpers (`biltoo-configure`,
`biltoo-build`, `biltoo-run`, `biltoo-run-gdb`). They are **not** read by the
application binary itself.

| Variable | Effect |
|----------|--------|
| **`BILTOO_SOURCE`** | Absolute path of the biltoo checkout (set in `shellHook`). |
| **`BILTOO_BUILD_DIR`** | Out-of-tree CMake build directory (default `/tmp/biltoo-build`). |
| **`THUMTOO_SOURCE_DIR`** | Path to a thumtoo tree for nested CMake (`add_subdirectory`). Prefer a live checkout over a `/nix/store` snapshot for incremental work. |
| **`CMAKE_BUILD_TYPE`** | Default `Debug` in the shell. |
| **`QT_PLUGIN_PATH`** | Unwrapped binary: Qt imageformats / iconengines plugins. |
| **`XDG_DATA_DIRS`** | Prepends `$BILTOO_SOURCE/data` so theme icons resolve in-tree. |

See also [AGENT-ENV.md](../AGENT-ENV.md) and [AGENTS.md](../AGENTS.md).


## THUMTOO_STORE_ONLY

**Ignored** in thumtoo ≥262 (and permanently after ≥272): Client is always
Store-only. Legacy `Database` / `BlobStore` classes were removed from thumtoo;
setting `THUMTOO_STORE_ONLY=0` has no effect.

Durable pixels/meta live on the redesign Store (`index.sqlite` + `bulk.sqlite`
at the cache root by default).
