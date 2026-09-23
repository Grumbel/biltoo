# Biltoo 0.2.0 — release notes and open work

**Status:** `VERSION` is `0.2.0-dev`. This document is the product checklist
for cutting **0.2.0** (tag / package), not a substitute for git history.

**Related:** [SESSION_EXPORT_AND_ORDER.md](SESSION_EXPORT_AND_ORDER.md),
[IDENTITY.md](../IDENTITY.md), [DOMAIN.md](../DOMAIN.md), [TODO.md](../TODO.md),
[AGENTS.md](../AGENTS.md).

**Thumtoo:** tip stacks in this cycle require a pinned **thumtoo ≥ 323**
(Store-only + page text/LQIP as integrated). Lock the flake input before tag.

---

## 1. Theme of 0.2.0

0.1 established session identity (`SessionImageId`), Gallery / Image / Workspace
modes, soft/ladder pixels, and basic document viewing.

**0.2.0** is the “session as a deliverable” release:

- Reorder the session without re-opening files
- Export baked images (appearance applied) without touching sources
- Open a selection in a new window with appearance preserved
- Gallery and filmstrip stay aligned on identity-correct operations

---

## 2. Landed for 0.2.0 (user-facing)

### Session reorder

| Surface | Behaviour |
|---------|-----------|
| **Filmstrip** | Drag rows; insertion line; multi-select move; undoable |
| **Keyboard / context** | Alt+arrows (or vertical strip equivalents); Move Left/Right/Start/End |
| **Reorder Session…** | Edit / Gallery menu dialog (list drag + Move buttons) |
| **Gallery canvas** | Drag selected tiles to a drop slot; multi-select kept on press; drag ghost (~128px, count badge) |
| **Filmstrip → Gallery** | Drop reorders (does not append duplicates) |

All paths share `reorderSessionRows` / `SessionReorderCommand` and
`SessionImageId` focus after mutate.

### Export Images (File → Export Images…)

- Containers: **folder of images**, **.cbz**, **multi-page PDF**
- Formats: JPEG / PNG (raster containers); PDF is multi-page of baked bitmaps
- Bakes **content appearance** (crop, flip, quarter turns, colour grade)
- **Never overwrites** source files (new paths only)
- Scope: entire session or current selection (filmstrip ∪ canvas)
- Progress + cancel; dialog remembers settings; long-edge presets; optional
  open destination when finished
- Virtual session paths (`//page:`, `//archive:`) export correctly (no false
  “refusing to overwrite source”)

Distinct from Workspace **Export page as PNG/PDF…** (composed page guide).

### Open Selection in New Window

- Opens filmstrip ∪ Gallery/Workspace selection in a new `MainWindow`
- Transfers **content appearance** (crop/flip/grade/attention), not Workspace pose
- Fresh `SessionImageId`s; preserves selection order (no re-sort)
- Status feedback with counts

### Other stability in this cycle

- Public APIs for undoable session reorder (QUndoCommand-safe)
- Gallery drag press tracking uses raw `ImageItem*` (not `QObject`/`QPointer`)

---

## 3. Release checklist (before tag)

1. **Integrate tip on `origin/master`**  
   Full stack from the agreed base through the tip recorded in [TODO.md](../TODO.md)
   (export + reorder + open-selection + Gallery drag pixmap). Do not ship a
   delta-only bundle as the release artifact.

2. **Pin thumtoo**  
   Flake lock / CI must resolve a known-good thumtoo revision (≥ 323 for this
   stack). Smoke soft ladder, size probes, page text layer, export decode.

3. **`VERSION`**  
   For the release commit: set `VERSION` to `0.2.0` (drop `-dev`) unless the
   packaging scheme intentionally keeps `0.2.0.N+gHASH` only on CI builds.

4. **Smoke matrix**

   | Area | Checks |
   |------|--------|
   | Open | Directory, PDF pages, archive members, mixed session |
   | Modes | Gallery pack; Image crop/flip/grade; Workspace place; return |
   | Reorder | Filmstrip multi-drag; Gallery multi-drag; dialog; undo/redo |
   | Export | Folder + CBZ + PDF; selection-only; PDF `//page:` sources |
   | Open Selection | Multi + crop → new window keeps crop |
   | Multi-window | Shortcuts not process-wide (Space, Ctrl+Q, …) |
   | Cold open | Wipe Store or new files: dir of JPEGs stays responsive; note PDF/7z stalls (§4.2) |

5. **Docs**  
   - Point [TODO.md](../TODO.md) / [AGENTS.md](../AGENTS.md) tip at the release commit  
   - Mark Gallery-canvas reorder **done** in SESSION_EXPORT_AND_ORDER.md  
   - Short user-facing blurb in README if features are listed there  

6. **Package**  
   `nix build` (or project equivalent); About box version; `.desktop` still valid.

---

## 4. Known limitations (0.2.0 — not blockers)

### 4.1 EPUB / PDF text search does not cross text-box boundaries

**What works today**

- Document pages can expose a **text layer** (regions with string + bbox).
- Find (search bar) matches the query against **each region independently**
  via `TextSearchPolicy` (`normalizeForSearch`, optional fuzzy / alnum).
- Hits highlight per matching region on the current page.

**Limitation**

- Matching is **per text box / region**. A phrase that is split across two
  adjacent regions (line break, column, hyphenation, separate PDF text objects,
  EPUB fragment boxes) will **not** match even when the concatenated reading
  order would contain the phrase.
- Fuzzy mode still operates inside a single region’s string; it does not join
  neighbours.

**Why**

- Extractors often emit one region per run/box, not a continuous reading-order
  string with break metadata.
- Current `recomputeTextSearchMatches()` loops `regionAt(i)` and calls
  `regionMatchesQuery(r.text, …)` only.

**Post-0.2 direction (not scheduled for this tag)**

1. Build a **reading-order stream** (ordered region indices + optional
   inter-region separator: space / newline).
2. Run the query on the concatenated normalized stream; map match ranges back
   to one or more region indices for highlight.
3. Careful handling of hyphenation and RTL; keep per-region path for single-box
   hits (fast path).

Until then: prefer shorter queries that fit one box, or accept that some
multi-box phrases need manual scanning.

### 4.2 Cold-cache open: speed, progress, pathological inputs

**What works today**

- Open / History runs expand → size gate → Gallery pack with progressive
  pixels (LQIP / soft / tiles depending on path and thumtoo Store state).
- Centre HUD and status can show “Opening…”, indexing, and per-count export
  progress; expand reports are rate-limited so huge listings do not flood the
  GUI thread.
- Warm Store (repeat open of the same files) is much cheaper than first touch.

**Limitation — needs more testing and debugging before calling open “done”**

Cold open (empty or thin thumtoo Store) can still feel **stuck or arbitrarily
slow**, with **suboptimal progress reporting**: the UI may not show which file
or stage is blocking, so long stalls look like hangs.

Especially pathological:

| Input | Observed pain |
|-------|----------------|
| **Some PDFs** | Extremely slow page decode; **no useful thumbnails** for a long time (or ever on first pass). Size probes and full raster compete; user sees empty/soft placeholders while one bad page burns the worker pool. |
| **7z (and similar solid archives)** | Member listing and sequential extract are costly; solid compression makes random member access worse. Expand + first-pixel path can stall with little incremental feedback. |
| **Huge mixed drops** | Directory + archive + multi-page PDF in one open: progress jumps between expand, sort, size resolve, and decode without a single clear “now doing X of Y” story. |

**Why (engineering sketch)**

- Cold path pays extract + decode + optional soft encode; progress is often
  generation/count based, not **stage + current URI**.
- PDF page raster cost varies wildly (scanned full-page images vs text PDFs);
  lack of a cheap overview for some files means no early filmstrip/Gallery ghost.
- Solid archives do not allow cheap parallel member reads; one slow member
  serializes the queue.
- Worker priority (sizes vs tiles vs soft) can leave the HUD on a generic
  “Opening…” while the slow work is elsewhere.

**Post-0.2 direction (not a hard gate for the 0.2.0 tag, but a release note)**

1. **Stage-aware progress** — expand / size-resolve / soft / page-raster as
   distinct HUD lines; show **current path or page ref** when blocked >N ms.
2. **Pathological PDF policy** — cap concurrent page decodes; prefer cheap
   size/LQIP when available; skip or defer “no overview” pages without
   blocking the whole session; optional per-page timeout messaging.
3. **Archive policy** — for 7z/solid, emphasize sequential warm with countable
   progress (“member i/n”); avoid pretending random access is fast.
4. **Test corpus** — explicit cold-cache cases: large 7z comic, image-heavy PDF
   with no embedded thumbs, USB/slow disk, first open after wiping Store.

**0.2.0 expectation:** ship with honest docs; treat multi-second silent stalls
on known-bad files as **known issues** to file with sample paths, not as
blockers unless a regression makes normal JPEGs unusable.

### 4.3 Other deferred items (optional later)

| Item | Notes |
|------|--------|
| Print from Gallery / Image | Matrix still weak; Workspace page print is primary |
| Export page PNG from Image mode | “Maybe” single view — not required for 0.2 |
| Workspace session export | Export Images is Gallery/Image-first; Workspace optional |
| Deeper tile / performance work | Overlaps §4.2; engineering track |
| Cross-region text search | §4.1 |

---

## 5. Explicitly out of scope for 0.2.0

- Rewriting source files on disk (export is bake-to-new-path only)
- Cross-archive identity (session path is the decode key; identity is
  `SessionImageId`)
- Full “document editor” text editing (search/highlight only)
- Guaranteed perfect soft→full progressive behaviour on every archive (report
  regressions separately)

---

## 6. Agent / packaging notes

- Deliverables remain **git bundles** from the agreed base to tip (full stack),
  not delta-only patches.
- Handoff continuity: [TODO.md](../TODO.md) tip must match what a clean tree
  can `git pull` after the release commit is on origin.
- REUSE / SPDX: keep existing project license; new files use project headers.

---

## 7. Suggested cut sequence

1. Final smoke on tip (reorder + export + open selection + PDF pages).  
2. Doc commit: this file + SESSION_EXPORT + TODO/AGENTS tip.  
3. `VERSION` → `0.2.0`.  
4. Tag `v0.2.0` (or project tag scheme).  
5. Publish package / update flake consumers.

**Post-tag priorities (pick by product need):**

- **Cross-region text search** (§4.1)
- **Cold-cache open / progress / pathological PDF & 7z** (§4.2)
