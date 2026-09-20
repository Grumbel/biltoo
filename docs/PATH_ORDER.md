# Path order dual model

## Two lists, two jobs

| Authority | Type | Owner | Role |
|-----------|------|-------|------|
| **SessionDocument** | paths + `SessionImageId`s | MainWindow | Session membership, filmstrip, identity, appearance key |
| **ImageView `m_pathOrderBook`** (`SessionPathOrder`) | paths + parallel ids | ImageView (Gallery-local) | Canvas row multiplicity for Gallery pack and **LoadAdd** |

They are **not** the same object. After dual-write was stopped (Tier 4 path-order),
mutations to the view book no longer write through to `SessionDocument`, and
membership queries for identity prefer the document when bound.

## Why the view book remains

1. **LoadAdd multiplicity** — `pathOrderOccurrences(path)` is view-book only.
   Gallery may show N tiles for one path (paste / multi-select place). The
   document counts session rows; the book counts *wanted decode/pack slots*.
   Consulting the document after `pathOrderClear()` would recreate tiles on a
   blank Workspace.

2. **Gallery pack order** — `applyLayout` / placeholders walk `pathOrderPaths()`
   / ids. Order can be pruned to live tiles without rewriting MainWindow session
   membership (session delete still goes through document + MainWindow).

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
(`pathorder-dual-model` CTest): independent mutation, LoadAdd multiplicity
on the book only, `pathOrderClear` leaves the document intact, appearance
survives book clear, open → Gallery → crop session-side id/crop, gallery
delete prune, aligned pack-order case, and mode-leave clear vs document.

Offscreen ImageView harness plan: [IMAGEVIEW_CHARACTERIZATION.md](IMAGEVIEW_CHARACTERIZATION.md).

`PackOrderView` (`src/packorderview.h`) is an immutable paths∥ids snapshot with
`fromBook` / `fromDocument` factories. Gallery pack will eventually walk a
`PackOrderView` so Tier 4 can switch the source without rewriting pack loops.
Characterization: `tests/packorderview_test.cpp` (`packorderview` CTest).

## Read path (post-1714)

Almost all pack/LoadAdd **reads** go through `ImageView::currentPackOrder()`
(`PackOrderView::fromBook`). Exceptions that still return references into the
book (call-lifetime stable): `pathOrderPaths()`, `pathOrderIds()`.

Mutations remain `pathOrderClear` / `SetOrder` / `AppendRow` on the book.
Public `setPathOrder` requires paths∥ids (or `PackOrderView`); the paths-only
overload was removed so session ids cannot be cleared by accident.

## Exit criteria (Tier 4 residual)

`git grep m_pathOrderBook` empty still requires an offscreen **ImageView**
harness (decode + framing) for open → Gallery → crop → return → Image so the
view can query the document for pack order without regenerating session tiles
incorrectly. Until then, keep the dual model and the accessors in
`imageview_private_methods.inc`.

See also: [IDENTITY.md](../IDENTITY.md), [DOMAIN.md](../DOMAIN.md) session open rules.
