# Path order dual model

## Two lists, two jobs

| Authority | Type | Owner | Role |
|-----------|------|-------|------|
| **SessionDocument** | paths + `SessionImageId`s | MainWindow | Session membership, filmstrip, identity, appearance key |
| **ImageView `m_pathOrderOverlay`** (`PackOrderOverlay`) | Explicit paths∥ids or FollowDocument | ImageView (Gallery-local) | Canvas row multiplicity for Gallery pack and **LoadAdd**; Explicit empty = mode-leave suppress |

They are **not** the same object. After dual-write was stopped (Tier 4 path-order),
mutations to the view book no longer write through to `SessionDocument`, and
membership queries for identity prefer the document when bound.

## Why the view book remains

1. **LoadAdd multiplicity** — `pathOrderOccurrences(path)` is view-book only.
   Gallery may show N tiles for one path (paste / multi-select place). The
   document counts session rows; the book counts *wanted decode/pack slots*.
   Consulting the document after `pathOrderClear()` would recreate tiles on a
   blank Workspace.

2. **Gallery pack order** — `applyLayout` / placeholders walk `currentPackOrder()`
   (paths ∥ ids). Order can be pruned to live tiles without rewriting MainWindow
   session membership (session delete still goes through document + MainWindow).

3. **Stash** — Gallery stash snapshots path order with tiles; restore puts the
   book back without touching `SessionDocument`.

## When they align

| Event | Document | View book |
|-------|----------|-----------|
| `loadFiles` / expand | `setPaths` / `replaceAll` | `setWorkspacePaths` → `pathOrderSetOrder` |
| Gallery paste / place | MainWindow may `append` | `pathOrderAppendRow` in `addImageForSession` |
| Gallery Delete (bound) | MainWindow remove | prune / setOrder after remove |
| Mode leave / clear | unchanged or clear | `pathOrderClear` (local only) |
| `rebindWorkspaceSession` | source of truth for ids | **does not** rewrite the book |

Identity for slideshow / crop / filmstrip: **`firstSessionIdForPath`** prefers
`m_sessionDoc` when bound; the book is fallback only.

## Characterization

Pure dual-model contracts are locked by `tests/pathorder_dual_model_test.cpp`
(`pathorder-dual-model (book + PackOrderOverlay cases)` CTest): independent mutation, LoadAdd multiplicity
on the book only, `pathOrderClear` leaves the document intact, appearance
survives book clear, open → Gallery → crop session-side id/crop, gallery
delete prune, aligned pack-order case, and mode-leave clear vs document.

Offscreen ImageView harness plan: [IMAGEVIEW_CHARACTERIZATION.md](IMAGEVIEW_CHARACTERIZATION.md).

`PackOrderView` (`src/packorderview.h`) is an immutable paths∥ids snapshot with
`fromBook` / `fromDocument` factories. Gallery pack will eventually walk a
`PackOrderView` so Tier 4 can switch the source without rewriting pack loops.
Characterization: `tests/packorderview_test.cpp` (`packorderview` CTest).

## Read path (post-1883)

All pack/LoadAdd **reads** go through `ImageView::currentPackOrder()` →
`m_pathOrderOverlay.resolve(m_sessionDoc)`. Book-reference accessors
(`pathOrderPaths` / `pathOrderIds`) were removed (biltoo-1817). Dead
`PackOrderReadSource` / `packOrderForRead` removed (1887).

Mutations: `pathOrderClear` / `SetOrder` / `AppendRow` on the overlay
(Explicit; setOrder may collapse when aligned). Public `setPathOrder` requires
paths∥ids (or `PackOrderView`).

## PackOrderOverlay (tips 1881–1884)

Owner of view pack-order state. Header: `src/packorderoverlay.h`.
Characterization: `tests/packorderoverlay_test.cpp` (`packorderoverlay` CTest).

### Modes

| Mode | `resolve(doc)` | Models |
|------|----------------|--------|
| **FollowDocument** | `PackOrderView::fromDocument(*doc)` (or empty) | Pack aligns with session membership; no extra storage |
| **Explicit** | `PackOrderView::fromBook(held order)` | Multiplicity, ad-hoc place, stash restore, **or cleared** |

**Explicit empty is distinct from FollowDocument.** After `pathOrderClear` /
mode leave, pack must stay blank while `SessionDocument` still lists open
files. If pack fell through to the document, blank Workspace / leave Gallery
would regenerate session tiles — the dual-model reason the book still exists.

### Mapping from today’s book API

| Today (ImageView) | Overlay |
|-------------------|---------|
| `pathOrderClear()` | `clearExplicit()` → Explicit + empty |
| `pathOrderSetOrder(paths, ids)` | `setExplicit` + `tryCollapseToFollowDocument` |
| `pathOrderAppendRow(path, id)` | `appendExplicitRow(path, id, m_sessionDoc)` (seed on promote) |
| `currentPackOrder()` | `m_pathOrderOverlay.resolve(m_sessionDoc)` |
| `pathOrderOccurrences(path)` | `countPathOccurrences(path, m_sessionDoc)` |
| Stash snapshot | `PackOrderView` of `resolve(...)`; restore via `setExplicit` |
| Post-loadFiles align | Optional later: `followDocument()` when order matches doc |

### Migration steps (do not skip)

1. **Design type + pure tests** (1881) — `PackOrderOverlay` + resolve
   invariants locked without touching ImageView.
2. **Adopt storage** (1883) — `m_pathOrderOverlay` replaces `m_pathOrderBook`;
   host mutators are thin wrappers (`clearExplicit` / `setExplicit` /
   `appendExplicitRow`). Behaviour identical (always Explicit, seeded like
   the former empty book via `clearExplicit()` in the ImageView ctor).
3. **Optional collapse** (1884) — `pathOrderSetOrder` calls
   `tryCollapseToFollowDocument` when the new order aligns with the bound
   document. `pathOrderClear` stays Explicit empty. `appendExplicitRow` seeds
   from the document when promoting out of FollowDocument so membership is
   not dropped on LoadAdd.
4. **ImageView harness green** — open → Gallery → crop → return → Image with
   decode + framing ([IMAGEVIEW_CHARACTERIZATION.md](IMAGEVIEW_CHARACTERIZATION.md)).
5. **Drop dual-model residual** — pack readers use overlay resolve only;
   policy confidence after harness (no bare `SessionPathOrder` member since
   step 2).

Until step 4, do **not** switch pack readers to `SessionDocument` alone
(Explicit-empty / mode-leave must still suppress pack).

### Read policy

Pack / LoadAdd / size-resolve readers use `ImageView::currentPackOrder()` →
`m_pathOrderOverlay.resolve(m_sessionDoc)` (FollowDocument when collapsed and
aligned; Explicit otherwise, including Explicit empty after pathOrderClear).

SessionDocument membership alone is **not** a pack drop-in. Identity lookups
may use the document (`firstSessionIdForPath`). The old `PackOrderReadSource` /
`packOrderForRead` helpers were removed (1887); all pack reads go through the
overlay.

## Exit criteria (Tier 4 residual)

Storage is on `PackOrderOverlay` (no `m_pathOrderBook`). Remaining dual-model
risk is **semantic**: pack must not use SessionDocument alone while
Explicit-empty / mode-leave suppress regeneration. An offscreen **ImageView**
harness (decode + framing) for open → Gallery → crop → return → Image is
still required before trusting FollowDocument collapse or document-only pack.
Until then, host mutators stay Explicit-only.

See also: [IDENTITY.md](../IDENTITY.md), [DOMAIN.md](../DOMAIN.md) session open rules.
