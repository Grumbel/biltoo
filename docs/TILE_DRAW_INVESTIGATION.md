<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile drawing & request path — investigation plan

Status: **investigation in progress** — Phase 0 audit done; underlay fixes in tree. Captures
current symptoms, architecture map, hypotheses, instrumentation needs, and
phased work to make paint **always show the best available pixels** and unify
filmstrip vs main-view paths where possible.

Related (existing, still authoritative for contracts):

- [TILE_LOD.md](TILE_LOD.md) — host LOD model, goals, non-goals
- [TILE_LOD_RUNTIME.md](TILE_LOD_RUNTIME.md) — manual checks, env flags
- [TILE_LOAD_COORDINATOR.md](TILE_LOAD_COORDINATOR.md) — request budgeting
- [PERFORMANCE.md](PERFORMANCE.md) — ladder / soft / tiles cost model
- [DISPLAY_SURFACE.md](DISPLAY_SURFACE.md) — install coordinator
- thumtoo `TILES.md` — durable grid encode

Filmstrip layout only: [FILMSTRIP_LAYOUT.md](FILMSTRIP_LAYOUT.md) (geometry, not pixels).

---

## 1. Observed symptoms (product)

| Symptom | User-facing effect |
|---------|-------------------|
| **LQIP rarely visible** when useful | While coarser/exact tiles are missing, holes or empty/replacement rects instead of LQIP |
| **Blur or replacement under motion** | Zoom/scroll → blurry cells or placeholders even when *some* tile scale exists in RAM |
| **Quality does not degrade cleanly** | Missing exact scale does not reliably fall back to coarser parent tiles |
| **Nondeterministic** | Same path/zoom sometimes filled, sometimes empty — race / eviction / generation |
| **Filmstrip slow** | Strip lags even when gallery/image already holds pixels for the same path |

**Cold cache** is allowed to be slower and briefly show replacement chrome; warm
cache must not look “empty” if any usable sample (LQIP, soft, any tile scale)
exists.

---

## 2. Product rules (target behaviour)

### Always paint something (warm)

Priority for a visible content rect (highest first):

1. **Exact** tile at target scale (Succeeded in RAM)
2. **Coarser parent** tile covering the rect (Succeeded)
3. **Finer** tile downscaled (optional; only if already in RAM — do not request finer for fallback alone)
4. **Soft / PreferCache** underlay for the item (if present)
5. **LQIP** underlay
6. **Alive placeholder** — not a dead grey void: subtle indeterminate mark
   (pulse / spinner / soft hatch) meaning “request in flight,” not “missing file”

### Cold cache

- Replacement / alive placeholder OK briefly.
- Prefer progress that reflects **in-flight** vs **failed** vs **not yet scheduled**.
- Do not block the GUI; keep request lanes fair (focus > visible > prefetch).

### Non-negotiables

- Paint path **must not** issue network/disk decode on the GUI thread.
- Eviction must **not** strip the last usable underlay for on-screen rects
  without a replacement plan in the same frame.
- Filmstrip and main view should share **pixel sources** (ImageCache / path
  soft / tile registry) so the strip is not a second slow climb for warm paths.

---

## 3. Architecture map (as of this writing)

```text
                    ┌─────────────────────────────────────┐
                    │ DisplayPipeline / ladder / ImageCache │
                    │  LQIP · soft · PreferCache frames     │
                    └──────────────┬──────────────────────┘
                                   │ underlay QImage
┌──────────────────────────────────▼──────────────────────────────────┐
│ ImageItem paint                                                     │
│  prepareTileLod → TileSession draw plan → tile_cover_paint          │
│  + soft/LQIP under holes                                            │
└──────────────┬───────────────────────────────┬──────────────────────┘
               │                               │
               │ tick / coordinator            │
               ▼                               ▼
┌──────────────────────────┐     ┌────────────────────────────────────┐
│ TileLodController        │     │ TileLoadCoordinator                │
│  target scale, visible   │────▶│  budget, gen, issue to thumtoo     │
│  keys, suppress          │     └────────────────────────────────────┘
└────────────┬─────────────┘
             │ shared per path
             ▼
┌──────────────────────────┐
│ TileLodRegistry          │  process-wide path → TileMemoryCache
│  trim budget, idle drop  │
└──────────────────────────┘

Filmstrip (ThumbnailBar) — largely SEPARATE:
  session-id / path pixmap overrides
  ImageCache / soft / filmstrip ladder edge
  NOT the same paint loop as ImageItem tile cover
```

### Core pieces

| Piece | Role |
|-------|------|
| `tilelod::build_draw_plan` | Exact → coarser parent → LQIP flag → empty |
| `TileSession` | Visible keys, target scale, pump, protect list for trim |
| `TileMemoryCache` | Succeeded/InFlight/Failed; `trim_to_budget`; `drop_finer_than` |
| `TileLodRegistry` | Path-keyed caches, global RAM budget |
| `TileLoadCoordinator` | Who gets issue slots this tick |
| `ImageItem` / `imageitem_tilelod.cpp` | Bind path, prepare plan, paint, LQIP attach |
| `ThumbnailBar` | List widgets + overrides; ImageCache / soft — **parallel path** |

### Known intentional gates (easy to misread as bugs)

- Gallery: tiles only when on-screen cell is large enough (`tileLodWanted` /
  ~device-px threshold); small cells stay soft/LQIP.
- Crop draft: tile LOD suppressed.
- Image mode: PreferCache climb skipped when `tileLodWanted` (tiles own display).

---

## 4. Hypotheses (ranked)

### H1 — Eviction strips usable fallbacks under motion

`trim_to_budget` protects visible keys + some parents, but aggressive pan/zoom
changes the protect set every tick. Coarser tiles or off-by-one parents may
evict just before paint. **LQIP is outside the tile cache** — if the host also
clears item LQIP/soft when “first tile arrives” or on path churn, underlay
disappears while the new scale is still InFlight.

**Check:** Log protect set vs trim victims vs paint plan kinds for 2s of
scroll/zoom.

### H2 — Draw plan has LQIP but host does not paint it

`build_draw_plan` sets `use_lqip` when `has_lqip` is true and no tile hit.
If `has_lqip` is false (LQIP never attached, cleared early, or only on another
item), commands are empty → holes / replacement only.

**Check:** Correlate `has_lqip`, ImageCache hit, and `DrawKind::Underlay` rate.

### H3 — Target scale jumps; parents not requested or already dropped

Policy: request exact target; parents are fallback only if already Succeeded.
If `drop_finer_than` or scale change drops mid-tier tiles, and exact is slow,
paint has nothing between soft and empty.

**Check:** Request stream (scale histogram) vs Succeeded scales in cache vs
plan fallback depth.

### H4 — Generation / suppress / session drop races

Session replace, mode switch, or `dropAllTileLodSessions` clears holders while
coordinator still delivers; paint sees empty until rebind. Feels random.

**Check:** Count session gen bumps vs path during scroll; failed installs.

### H5 — Coordinator starvation

Visible cells never get issue slots (budget / focus / gallery 2-cells-per-tick
style limits). Cache cold for current scale; underlay should still show.

**Check:** Issue vs defer counts per path; time-to-first-Succeeded after zoom.

### H6 — Filmstrip does not reuse warm ImageCache / soft

Strip schedules its own ladder edge / loadThumbnail path even when gallery
already has ≥256 or soft for that path. Looks “slow” relative to main view.

**Check:** Filmstrip install source (override / ImageCache / disk) when gallery
cell for same path is already HOST/soft.

### H7 — ItemCoordinateCache / pixmap freeze

Comments in `imageitem_tilelod.cpp` already mention frozen LQIP pixmap over
live tiles and singleShot storms. Inverse: freeze empty or stale underlay.

**Check:** Device-coordinate cache invalidation on tile Succeeded and underlay
change.

---

## 5. Investigation procedure

### 5.1 Instrumentation (add temporarily or behind `BILTOO_TILE_DEBUG`)

Per paint (or sampled):

- path / session id / target_scale / max_scale  
- visible key count  
- plan histogram: exact / parent / underlay / empty  
- `has_lqip`, soft underlay present  
- cache: succeeded_count, approx_bytes, in_flight count  
- last trim: bytes freed, keys dropped (scale distribution)

Per tick:

- coordinator: issued, deferred, cancelled  
- registry: path idle drop, budget pressure  

Filmstrip (separate channel `BILTOO_DEBUG_FILMSTRIP`):

- role source: session override / path icon / ImageCache / schedule  
- whether main view registry or ImageCache already has pixels for path  

### 5.2 Repro matrix (deterministic where possible)

| # | Setup | Action | Expect (target) |
|---|--------|--------|-----------------|
| R1 | Warm pyramid, Image mode | Zoom past soft, pan | Always tiles or parent; no empty if any scale Succeeded |
| R2 | Warm, drop network/disk slow (nice) | Fast zoom in/out | Underlay or parent, not void |
| R3 | Gallery, large cells | Ctrl+wheel + scroll | In-view cells refine; LQIP/soft under holes |
| R4 | Same path in gallery + filmstrip | Open session | Filmstrip uses warm soft/LQIP quickly |
| R5 | Cold CBZ | Open gallery | Alive placeholder → LQIP/soft → tiles if wanted |
| R6 | Session replace mid-zoom | Open other folder | No cross-session tile ghosts; new path recovers |

### 5.3 Code audit checklist (unify / bulletproof)

1. **`build_draw_plan`** — parent walk complete? max_scale bounds? UV correct for partial edge tiles?  
2. **`TileSession::pump` / protect list** — parents of all visible keys protected? optional: protect one full coarser scale.  
3. **`trim_to_budget` / registry idle** — never evict last Succeeded scale for a path that still has a bound visible item.  
4. **LQIP attach/detach** — when cleared; ensure clear only when *coverage* exists for the viewport, not first tile anywhere.  
5. **`tileLodWanted` vs paint** — small cells must still paint soft/LQIP; large cells must not skip underlay while tiles pending.  
6. **ImageItem paint order** — underlay first, then plan; no early return that skips underlay.  
7. **Filmstrip** — single “best display sample for path/id” helper shared with decode window (ImageCache → soft → LQIP → schedule).  
8. **Failed tiles** — DrawKind path does not block parent fallback forever.  
9. **Workspace multi-item same path** — shared registry; one item’s suppress must not starve the other.  

### 5.4 Tests to add or extend

- Unit: `build_draw_plan` with exact missing, parent present → parent command.  
- Unit: exact missing, no parent, `has_lqip` → underlay.  
- Unit: trim_to_budget does not drop protected parent of visible key.  
- Unit: markFailed-style analogue — InFlight + empty Succeeded still underlay.  
- Integration (optional): mock source fills coarse then fine; paint histogram monotonic quality.

---

## 6. Unification goals

### Shared “display sample” resolution

Introduce (name TBD) a small pure helper used by:

- ImageItem underlay choice  
- Gallery cell soft install  
- ThumbnailBar pixmap choice  

```text
resolveDisplaySample(path, id, minEdge, maxEdge) →
  appearance override |
  ImageCache best in [min,max] |
  path soft |
  LQIP |
  none (schedule if allowed)
```

Filmstrip should **not** be a separate climb policy; only a different
`minEdge`/`maxEdge` (filmstrip ladder edge).

### Shared tile paint entry

Keep Qt-free `tilelod` core; ensure **one** host wrapper for
“prepare plan + paint cover + underlay” used by Image and Gallery items.
Workspace items already share ImageItem — verify no fork.

### Explicit quality ladder in one function

Document and implement a single ordered fallback used by plan builder **and**
host underlay so LQIP is never “forgotten” when tiles are the primary system.

---

## 7. Cold-cache progress indicator (minimal)

Not a detailed progress bar. Ideas:

- Cell/page shows **soft hatch or dim pulse** while any tile or soft request is
  InFlight for that path.  
- Distinct from **Failed** (static icon / error tint).  
- Driven by existing InFlight flags + coordinator state — no new network protocol.

Ship after paint fallback is correct; cosmetic only if underlay already works.

---

## 8. Phased work

### Phase 0 — Observe (this doc + instrumentation)

- Enable/extend `BILTOO_TILE_DEBUG` histograms.  
- Capture R1–R4 traces.  
- Confirm or reject H1–H7 with evidence.

### Phase 1 — Paint fallback correctness (highest priority)

- Fix any gap where plan is empty but LQIP/soft/parent exists.  
- Fix LQIP clear timing (“coverage” not “first tile”).  
- Protect coarser scales for visible keys during trim.  
- Tests for plan + trim contracts.

### Phase 2 — Request / eviction stability under motion

- Coordinator fairness for visible set after zoom.  
- Avoid drop_finer / budget thrash during continuous scroll.  
- Generation handling so Succeeded still usable as fallback across gen (session
  already has comments — verify).

### Phase 3 — Filmstrip unification

- Shared sample resolver; strip prioritizes ImageCache/soft already fetched.  
- Measure time-to-icon with warm gallery.

### Phase 4 — Alive placeholder polish

- InFlight vs Failed vs empty chrome.  
- Optional pulse; keep cheap.

### Phase 5 — Doc sync

- Update TILE_LOD.md / RUNTIME with measured behaviour and new invariants.

---

## 9. Out of scope for this investigation

- Changing thumtoo tile schema or encode.  
- JPEG XL progressive as a pyramid replacement (see [FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md)).  
- Cross-process shared tile RAM.  
- Pixel-perfect upscaled coarse tiles (overlap research is separate).

---

## 10. Success criteria

1. Warm path, any visible hole: paint shows **parent tile or soft/LQIP**, not empty.  
2. Continuous zoom/scroll for 10s: empty plan rate ≈ 0 when underlay or any tile scale exists.  
3. Filmstrip median time-to-pixels for paths already soft in gallery drops materially.  
4. Cold open still allowed to show alive placeholder; Failed is visually distinct.  
5. Unit tests lock plan fallback and trim protect behaviour.  
6. Single documented fallback order shared by code and [TILE_LOD.md](TILE_LOD.md).

---

## 11. Immediate next step

Start **Phase 0** with a focused code audit of:

1. `draw_plan.cpp` parent walk  
2. ImageItem LQIP attach/clear in `imageitem_tilelod.cpp`  
3. `TileSession` protect list vs `trim_to_budget`  
4. ThumbnailBar pixmap resolution vs ImageCache  

Log findings under this document’s §4 hypotheses (accept/reject) before large
behavioural changes.

---

## 12. Investigation findings (Phase 0 audit)

Date: 2026-09-26. Code walk of `draw_plan`, `TileSession` protect/issue,
`ImageItem` underlay paint, filmstrip schedule.

### Plan sanity check

The investigation plan’s target fallback order matches product need. One
correction to the architecture map:

- **Draw-plan `use_lqip` was effectively dead** — host always called
  `setHasLqip(false)` in `prepareTileLodPlan`, so plan Underlay commands never
  fired. Whole-frame underlay is painted only in `ImageItem` paint, not via
  `tile_painter` LQIP args (except when an underlay QImage is passed into
  `paint()`).

Protect list for trim **does** include all coarser parents of visible keys
(full chain). Cancel-obsolete keeps the same set. H1 (eviction of parents) is
less likely for *visible* parents; still possible for non-visible mid scales
or path-idle registry drops.

### Accepted / refined hypotheses

| Id | Verdict | Evidence |
|----|---------|----------|
| **H2** | **Confirmed (primary)** | Gallery `tileLodWanted` path used **LQIP-only** underlay: soft/HOST samples were skipped (`galleryLqipOnlyUnderTiles`). If LQIP never landed, cells showed placeholder/holes while tiles streamed. Image mode keeps soft more often. |
| **H2b** | **Confirmed** | `setHasLqip(false)` every prepare — plan never marked underlay holes. |
| **H3** | **Partially confirmed** | Cold climb requests at most **one** coarser step; parent chain is **not** bulk-requested (by design). Fallback paint depends on underlay or residual parents already in RAM. |
| **H1** | Open | Protect list looks correct for visible parents; still verify under budget pressure with debug histograms. |
| **H5** | Open | Coordinator limits still relevant for time-to-exact, not for empty underlay. |
| **H6** | **Partially confirmed** | Filmstrip is a separate climb (`scheduleFilmstripTilePixels` / ImageCache). Warm ImageCache short-circuit exists; still not shared “best sample” helper with gallery. |
| **H4/H7** | Open | Need runtime traces. |

### Fixes landed with this investigation slice

1. **Gallery underlay:** Prefer LQIP when present; **allow soft/host when LQIP is missing** so tiles never float over empty cells.
2. **`setHasLqip`:** Set from presence of any whole-frame sample so draw-plan underlay flags stay consistent with host paint.

### Still TODO (Phase 1–3)

- Issue optional parent prefetch when exact is slow (without flooding coordinator).
- Shared `resolveDisplaySample` for filmstrip + gallery.
- Alive InFlight placeholder distinct from neutral provisional.
- Instrumentation histograms behind `BILTOO_TILE_DEBUG`.
