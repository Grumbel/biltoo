# Biltoo 0.2.0 — release notes and open work

**Status:** `VERSION` is `0.2.0-dev`. This document is the product checklist
for cutting **0.2.0** (tag / package), not a substitute for git history.
Release may slip a few days for **source tree layout** (§3a) — intentional.

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

### Batch appearance (multi-select)

| Surface | Behaviour |
|---------|-----------|
| **Adjustments** | Apply colour grade to Current / Selection (canvas ∪ filmstrip) / index range / even / odd indices |
| **Crop panel** | Manual margins or autocrop (+ soft-sample wait); same target modes; live preview on current |
| **Orient** | Flip / rotate / reset content appearance use the same expanded targets |
| **Undo** | Multi-target ops group under one undo macro when N > 1 |

### Menu and shortcut polish

- File: **Export Session Images…** distinguished from Workspace page PNG/PDF export
- Image → **Document** submenu (EPUB Layout, PDF Embedded Images — experimental)
- Gallery → **Layout** submenu; Edit clipboard vs session separators
- Shortcuts dialog notes viewer letter chords; Fit **Ctrl+Shift+F**, Workspace **Ctrl+Shift+W**
- Esc chain: crop cancel → leave slideshow → leave fullscreen → leave Image mode

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
   | PDF Embedded Images | One image-heavy PDF → Image menu → Gallery + export a few leaves (§4.3) |
   | Location Ctrl+L | Typed path replaces session (document); note leaf vs whole-doc confusion (§4.4) |
   | Crop + Open Selection | Commit crop → Open Selection in New Window still shows crop (§4.5) |

5. **Docs**  
   - Point [TODO.md](../TODO.md) / [AGENTS.md](../AGENTS.md) tip at the release commit  
   - Mark Gallery-canvas reorder **done** in SESSION_EXPORT_AND_ORDER.md  
   - Short user-facing blurb in README if features are listed there  

6. **Package**  
   `nix build` (or project equivalent); About box version; `.desktop` still valid.

---

## 3a. Source tree layout (in scope for 0.2.0)

Flat `src/` is ~250 translation units with a single subdirectory (`src/tilelod/`).
After the identity / controller / export refactor, **moving code into domain
subdirectories is planned for 0.2.0**, not deferred to 0.3. Accept a short slip
of the tag (days, not weeks) to land this cleanly.

### Goals

- Navigable tree for humans and agents
- Same pattern as `tilelod/`: **bounded subsystem**, explicit edges
- No behaviour change: move + CMake/include fix only (no renames in the same step)

### Target layout (sketch — adjust while moving)

| Directory | Contents (indicative) |
|-----------|------------------------|
| `src/tilelod/` | Unchanged |
| `src/session/` | `sessiondocument`, `sessionopen`, `sessionexport`, `sessionexpand`, `sessionreorderdialog`, path expand helpers tied only to session |
| `src/crop/` | `crop*`, `cropappearancecommand`, crop geometry/session |
| `src/gallery/` | `gallerycontroller`, layout/pack/size-resolve, gallery decode book/SM |
| `src/shell/` | `mainwindow*`, icons entry, centre progress, help panel wiring owned by shell |
| `src/workspace/` | workspace controller + geometry/group transform session pieces owned by Workspace |
| `src/pixels/` or `src/display/` | `imagecache`, display pipeline/controller, display quality/edge policy |
| `src/` (root) | Thin façades still shared: `imageview*`, `imageitem*`, `imageloader`, `pagepath`, `main.cpp` until a later cut |

Do **not** dump all `imageview_*.cpp` into one folder without ownership: prefer
leaving the façade at `src/` until controllers own more call sites (see
[REFACTOR.md](../REFACTOR.md)).

### Execution rules

1. **One domain per commit** (or small stack): e.g. only `session/` first.
2. Update `BILTOO_LIB_SOURCES` paths; add `target_include_directories` so
   `#include "session/sessiondocument.h"` keeps working **or** switch that domain to
   `#include "session/sessiondocument.h"` consistently in the same commit.
3. No symbol renames, no logic edits, no clang-format-only noise mixed in.
4. Build green after each domain; smoke open + Gallery + crop once at end.
5. Refresh [AGENTS.md](../AGENTS.md) “where things live” if paths change.
6. Agents: still deliver **full-stack git bundles** from the agreed base.

### Order (suggested)

1. `session/` — newest export/reorder code; fewest paint dependencies  
2. `crop/` — already typed as CropSession / CropController  
3. `gallery/` — controller + pack  
4. `shell/` — mainwindow splits  
5. `display/` / `pixels/` — if time; else leave for immediate post-tag  

`tilelod/` stays as the reference for how a leaf directory looks.

**Progress:** Phases 1–31 complete. Root is façade-only (`imageview*`, `imageitem*`, `main.cpp`). See [SRC_LAYOUT.md](SRC_LAYOUT.md).

### Out of this move

- Dual ImageView (§4.8)  
- Behaviour fixes for Location / cross-region Find / cold open  
- Mass include-style churn beyond what the move requires  

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

### 4.3 Image → PDF Embedded Images (more testing needed)

**What it is**

- Menu: **Image → PDF Embedded Images** (`openPdfAsEmbeddedImages`).
- Re-opens the current PDF (or the PDF behind a `//page:` / existing
  `//pdfimage:` session) as a session of **native embedded images**:
  collection URI `path.pdf//pdfimages` expands to leaves
  `path.pdf//pdfimage:1..N`.
- Decode is **thumtoo-backed** (no host fallback for embedded image leaves).
  Intended for comics/illustrated PDFs where the useful content is the
  embedded bitmaps rather than rendered full pages.

**Limitation — needs more testing**

This path is **newer and less exercised** than page-render (`//page:N`) open:

| Concern | Notes |
|---------|--------|
| **Coverage** | Which PDFs expose a useful `//pdfimages` set vs empty / partial / wrong order |
| **Count and order** | Image indices vs visual reading order; duplicates; masks / soft masks |
| **Cold open** | Same class of stalls as §4.2 when thumtoo must extract many large embeds with weak progress |
| **Thumbnails** | Early filmstrip/Gallery ghosts may be missing or tiny until Store fills |
| **Toggle / round-trip** | Page session → Embedded Images → back to pages; selection and appearance |
| **Export** | Export Images of `//pdfimage:N` leaves (bake path already special-cased for stems; needs corpus checks) |
| **Non-PDF** | Action correctly no-ops on EPUB/DjVu; confirm messaging |

**0.2.0 expectation:** ship the action as **experimental / use with care**;
file issues with sample PDFs. Not a tag blocker if page-mode PDF open remains
the primary document path. Add a short smoke: one image-heavy PDF → Embedded
Images → scroll Gallery → Export folder of a few leaves.

### 4.4 Location bar (Ctrl+L) and URL / path semantics

**What works today**

- **File → Open Location…** / **Ctrl+L** focuses the location toolbar
  (`commitLocationBar` on Enter).
- Accepts plain paths, `file://` URLs, and virtual refs (`//page:`, `//archive:`,
  `//pdfimage:`, …).
- Heuristic: if the user strips `//page:N` back to the bare document path while
  the current session entry is a page of that document, `loadFiles` expands the
  whole document and tries to **start at the previous page index**.

**Confusion (session vs single leaf)**

`commitLocationBar` always ends in **`loadFiles(...)`**, which **replaces the
session**. There is no first-class distinction between:

| Intent | Desired behaviour | Today |
|--------|-------------------|--------|
| **Focus / navigate** | Keep the session; jump to an existing row or page | Not available via Location; only index nav / filmstrip |
| **Open whole document, focus page** | Expand PDF/EPUB/archive to all leaves; select page N | Partial: bare path + prior page heuristic; typing `doc.pdf//page:5` still tends to open **that leaf as the session** (or expand only that ref depending on expand rules) |
| **Open only this leaf** | Session becomes a single path (one page or one file) | Default for many typed refs — **destroys** a multi-page session |

So users cannot reliably “go to page 12 of this book” from the location bar
without risking a full session replace, and cannot express “add this path” vs
“replace with this document”.

**Post-0.2 direction — explicit URL / commit modes**

1. **Syntax or prefix** (examples for design, not final grammar):
   - `doc.pdf` or `doc.pdf#session` → expand whole document (keep or replace
     session by policy).
   - `doc.pdf//page:12` → leaf-only open **or** whole session + focus page 12,
     depending on a chosen default documented in the bar placeholder.
   - Optional: `+path` append to session; plain path = replace (browser-like).
2. **Commit modifiers:** Enter = navigate-in-session if the path matches an
   existing row; Shift+Enter = replace session; Ctrl+Enter = append.
3. **Display:** location bar shows **session identity + focus** (e.g. document
   name + `p.12`) rather than only the raw leaf URI when in a multi-leaf
   document session.
4. Document the chosen rules in DOMAIN / help panel; align filmstrip badge and
   Ctrl+L text.

**0.2.0 expectation:** Location remains a **power-user open** path; document the
replace-session behaviour. Not a tag blocker.

### 4.5 Crop / appearance lost on some actions

**Reported:** crop (and possibly other content appearance) can disappear across
actions such as **Open Selection in New Window**.

**Intended behaviour (this cycle)**

- Open Selection snapshots `sessionAppearanceValue` / `freezeItemAppearance` and
  re-applies via `setSessionAppearance` on new ids (`loadSessionSnapshots`).
- Export bakes appearance into new files; sources stay untouched.

**Why crop may still be lost (test matrix)**

| Case | Risk |
|------|------|
| Crop **draft** not committed | Only live crop chrome; ItemWorld has no durable crop yet |
| `hasSessionAppearance` true but **content-empty** | Snapshot may copy a sparse pose-only slot and skip live freeze |
| Image mode primary vs multi-select | Wrong id / unbound primary when selection is filmstrip-only |
| Mode switch / reload / hard reload | Appearance path vs id mismatches (IDENTITY) |
| Location `loadFiles` replace | New session; old ids gone — expected unless user re-opens same files with Store-only orient (crop is id-scoped, not path XDG) |

**0.2.0 expectation:** treat as **must-test** before tag; if Open Selection still
drops **committed** crop, that is a **release bug** (fix before 0.2.0). Uncommitted
draft loss is acceptable if documented (commit crop before Open Selection).

Suggested smoke: crop in Image → Apply → Gallery → select → Open Selection in
New Window → crop still applied; undo stack in source window unchanged.

### 4.6 Menu and GUI cleanup (audit notes)

Not a full HIG rewrite; things that look **outdated or confusing** after 0.2
features landed:

**File**

- Open / Open Location / Add / Open Directory / Open Selection in New Window are
  clustered — OK; consider grouping “Open session” vs “Open document leaf”.
- **Print / Page setup / Export page PNG/PDF** sit next to **Export Images…** and
  **Export Text…**. Page exports are Workspace-primary (often hidden in Gallery);
  session export is the 0.2 deliverable. A separator or submenu
  (“Export page…” vs “Export session images…”) would reduce mix-ups.
- Recent Projects under File is fine; ensure enabled-state matches empty session.

**Edit**

- Workspace **Copy / Cut / Paste** next to **Duplicate** and **Reorder Session…**
  mixes canvas-clipboard with session order. Consider a “Session” subsection or
  moving Reorder next to sort actions if Sort lives under View/Gallery.
- **Find on page** is document-text Find; keep near Edit but help text should
  stress per-region limit (§4.1).

**Image**

- Rotate / flip / reset / crop / attention, then **EPUB layout** and **PDF
  Embedded Images**. Document actions are not really “image transforms” —
  a “Document” submenu (or View-adjacent) would age better.
- PDF Embedded Images is experimental (§4.3); status tip should say so until
  hardened.

**View / Gallery**

- Layout modes (Grid, Masonry, Flow, **Facing** two-up, …) are Gallery pack
  modes — not dual Image-mode viewers (§4.8). Naming in UI (“Facing pages”) is
  fine; avoid implying two independent Image controllers.
- Location bar pin / show actions: verify they are not duplicated under View and
  File.

**Toolbars**

- After Export Images and Reorder, check overflow and that Workspace-only
  actions do not leave empty gaps in Gallery.

**0.2.0 expectation:** optional polish commit; not required to tag if labels are
honest. Prefer one cleanup pass over drive-by renames during RC.

### 4.7 Other deferred items (optional later)

| Item | Notes |
|------|--------|
| Print from Gallery / Image | Matrix still weak; Workspace page print is primary |
| Export page PNG from Image mode | “Maybe” single view — not required for 0.2 |
| Workspace session export | Export Images is Gallery/Image-first; Workspace optional |
| Deeper tile / performance work | Overlaps §4.2; engineering track |
| Cross-region text search | §4.1 |
| Cold-cache / pathological PDF & 7z | §4.2 |
| PDF Embedded Images hardening | §4.3 |
| Location / URL session vs leaf | §4.4 |
| Crop transfer / commit edge cases | §4.5 |
| Menu regroup | §4.6 |

### 4.8 ImageView: two images at once → **0.3.0** candidate

**Idea:** a true **dual Image-mode** surface (two focused session images side by
side for compare / proof), with independent or linked pan-zoom.

**Not the same as** Gallery **Facing** layout (two-up **pack** of many session
tiles for reading order). Facing stays a Gallery layout mode in 0.2.

**0.3.0 scope sketch (out of 0.2)**

- Shell layout: split view or two ImageView controllers sharing one session
  document (or two focus ids into one session).
- Navigation: which pane receives ←/→; optional lock-step page turn for books.
- Identity: each pane binds a `SessionImageId`; crop/grade remain per id.
- Explicitly defer until after 0.2 release cut and Location/crop hardening.
- **ImageView ownership extraction** (see TODO.md 0.3 backlog): continue Phase 5
  controller pattern — paint/input/session-bind/rematerialize Hosts — so dual
  ImageView is not two copies of a 12k-line façade. Not a directory-only move.

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

1. Land **source subdirectories** (§3a) domain-by-domain; build green each step.  
2. RC smoke: reorder + export + open selection + PDF pages + **committed crop → Open Selection**.  
3. Optional menu polish (§4.6) if still confusing after the tree move.  
4. Doc tip + AGENTS paths; `VERSION` → `0.2.0`.  
5. Tag `v0.2.0`; publish package / flake consumers.

Tag may land **a few days later** than the feature freeze of reorder/export; that is expected.

**Post-tag priorities (pick by product need):**

- **Cross-region text search** (§4.1)
- **Cold-cache open / progress / pathological PDF & 7z** (§4.2)
- **PDF Embedded Images** hardening (§4.3)
- **Location / URL session vs leaf** semantics (§4.4)
- **Crop transfer** if anything remains after RC (§4.5)
- **Menu regroup** if not done pre-tag (§4.6)
- Finish any leftover directory moves (`display/` / `pixels/`) if deferred mid-§3a  
- **0.3.0:** dual ImageView / two-up compare (§4.8); ImageView ownership extraction; Gallery size-resolve throughput + failure diagnostics (TODO.md)
