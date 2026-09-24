<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ImageView / ImageItem ownership (Phase 5 / 0.3 track)

Companion to [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md) (mode stashes) and
[CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) (who owns *want*).

This document is the **target graph** for shrinking `ImageView` as a god-object
and keeping `ImageItem` a thin scene node. Dual ImageView (0.3) depends on this
not being two copies of a 12k-line façade.

## Roles

| Object | Role |
|--------|------|
| **`ImageItem`** | Scene node: path, session id, display pixels, live pose, paint/hit chrome. **Does not** own tile bags, climb policy, or durable appearance. |
| **`ImageView`** | `QGraphicsView` shell + public API. Host for mode switches, selection, and thin **sole-external** mutators into private `ImageItem` fields (friend). Must not grow new pixel/tile authority. |
| **`DisplayPipelineController`** | Owns PreferCache climbs, **tile LOD bags** (Stage 2), `installDisplayPixels`, soft/full install. Friend of `ImageItem` for pixel/tile mutators. |
| **`ImageController`** | Image-mode enter, classic path, session edge keys, reload. Does not install pixels directly — uses pipeline / view host. |
| **`GalleryController` / `WorkspaceController`** | Mode enter/leave, stashes, pack/free-form. Gallery may set cell size via `GalleryLayout` friends. |
| **`SlideshowController`** | Overlay path; reuses pipeline tile sessions (cover paint), must not create a parallel pixel store. |
| **`ItemWorld`** | Durable sparse appearance (crop/orient/grade) keyed by `SessionImageId`. |
| **`CropSession`** | Draft crop sample; friend for draft-locked install. |

## Who may create / destroy `ImageItem*`

| Action | Owner |
|--------|--------|
| Create live item | `DisplayPipelineController` / load paths via `ImageView` host (`createItem*`) |
| Destroy live item | `ImageView` canvas clear / focus paths (detach tile bag **before** delete) |
| Off-scene stash | Mode controller only (`WorkspaceController::m_stashedItems`, `GalleryController::m_stashedItems`) — see MODE_OWNERSHIP |
| Image underlay | **New** underlay from load pipeline — **never** steal a Workspace stash pointer |

## Who may install pixels

| Path | API |
|------|-----|
| Primary install | `DisplayPipelineController::installDisplayPixels` |
| Attach already-materialized sample | `DisplayPipelineController::attachDisplaySample` (sole place that calls `setPreviewImage` / `setSourceImageReady`); `ImageView::attachDisplaySample` forwards |
| Soft preview only | `ImageView::setItemPreviewImage` → `setPreviewImage` |
| Clear display pixels | `ImageView::clearItemDecodedPixels` **or** pipeline (friend) during install/replace |
| Intrinsic / layout size | `ImageView::setItemIntrinsicSize` **or** pipeline; samples must not define geometry (SIZE.md) |

**Forbidden:** ad-hoc `item->setSourceImage*` / `clearDecodedPixels` outside the hosts above.

## Who owns tile LOD

| Concern | Owner |
|---------|--------|
| `tilelod::ItemBag` allocation / map | `DisplayPipelineController` |
| Attach / detach bag on item | Pipeline only (`attachTileLodBag` / `detachTileLodBag`) |
| Session viewport / tick / paint | Bag’s `TileLodController`; ImageItem reads via attached bag |
| Shared path RAM cache | `TileLodRegistry` (process-wide) |
| Suppress flag | Pipeline writes `bag.suppressed`; item is not a parallel authority |

ImageItem `tileLodBag()` asserts a pipeline-owned bag (Stage 2). Orphan static bag is debug-only.

## Who owns pose

| Concern | Owner |
|---------|--------|
| Live pose fields | Only `applyPlacement` / `GalleryLayout::applyItemPlacement` (Stage 2 single writer) |
| Workspace chrome drag | Item interaction → `applyPlacement` |
| Durable pose | Workspace `WorkspaceItemState` / ItemWorld as documented |

## ImageView is not a friend of ImageItem

**Done (2417+):** `friend class ImageView` removed.

1. Canvas host surface is **public** on `ImageItem` (pose, session bind, mode chrome, applied xform, colour).
2. Pixel mutators stay **private**; only pipeline / crop / gallery friends.
3. ImageView reaches pixels only via `DisplayPipelineController::hostClearDecodedPixels` /
   `hostSetIntrinsicSize` / `hostSetPreviewImage` / `attachDisplaySample` / `installDisplayPixels`.

## Mode vs item (pointer ownership)

See [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md). Summary:

- `liveItems()` = on-scene only for the **active** mode.
- Stashes exclusive for off-scene items.
- Image mode underlay ≠ Workspace tile pointer.

## Extraction backlog (ordered)

1. **Done:** graph written; ImageView clear/intrinsic via host wrappers.
2. **Done:** `attachDisplaySample` implementation on `DisplayPipelineController`; view one-line forward.
3. **Done:** `friend class ImageView` removed; public host surface + pipeline pixel hosts.
4. Dual ImageView shares pipeline + ItemWorld, not a forked façade.

## Related

- [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md)
- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md)
- [TILE_LOD.md](TILE_LOD.md) / [TILE_LOAD_COORDINATOR.md](TILE_LOAD_COORDINATOR.md)
- [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §4.8 (0.3 dual ImageView depends on this)
