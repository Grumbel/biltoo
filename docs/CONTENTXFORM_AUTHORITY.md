<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ContentXform authority (must not get orient wrong)

## Durable ground truth

| Field | Store |
|-------|--------|
| Quarter turns / content flips | ItemWorld `contentBake` (SessionImageId) |
| Crop | ItemWorld `crop` |
| Grade | ItemWorld `color` |
| Workspace pose | ItemWorld `Placement` (or Workspace durable snapshot of pose only) |

Assembled by `sessionAppearanceValue(id)` / sparse fill.
`ItemWorld::hasContentOrient(id)` ≡ `hasContentBake(id) || hasCrop(id)`.

## Presentation-only (ephemeral)

| Field | Where |
|-------|--------|
| Applied ContentXform fingerprint | **On the live `ImageItem` only** |

Meaning: "pixels currently attached were materialized with this Value."

Used for: mid-edit while the same item is live; tile paint orient of host-native tiles.

## Structural rules (2180→2222)

1. **Materialize want** for a new underlay = sparse contentBake/crop only.
2. **Never** read ItemWorld `applied` residual as want for install (it outlived
   Workspace→Image and overrode sparse — random wrong orient).
3. On mode leave: **flush** item applied → sparse contentBake/crop, then
   **clear** ItemWorld applied for that id.
4. Host pixels in ImageCache are unoriented; `materializeDisplay(host, want)` once.
5. Tiles stay native grid; paint applies applied (item-local) orient.
6. **Placement-only** durable rows (Placement and/or Color without contentBake/crop)
   must not drive layout or materialize orient. Gate:
   `SessionAppearance::orientAuthorityWant(hasContentOrient, want)` (2211).
7. **Workspace durable snapshot** (`m_savedItems`) for bound ids is **pose only**.
   See [Workspace Placement lifecycle](#workspace-placement-lifecycle) below.
8. **SessionImageId** for content edit / layout / chrome:
   - ImageView: `resolveContentEditSessionId` (2215)
   - DisplayPipelineController: `resolveItemSessionId` (2219–2220)
   Both: preferred / item sid → Image-mode cursor only. Gallery/Workspace unbound
   items never inherit the Image cursor. Peer sync stays strict `item->sessionId()`.


## Workspace Placement lifecycle (2212–2222)

Bound SessionImageId pose travels through three sites; all use the Placement
bridge (`placementFromState` / `applyPlacementToState`). Content never enters
`m_savedItems` for bound ids.

| Phase | Function | What is stored |
|-------|----------|----------------|
| Leave | `WorkspaceController::snapshot` | `clearedContentOps(freeze)` → pose + path/sid only; `setPlacement` into ItemWorld |
| Mid-edit | `updateWorkspaceSavedAppearance` | Full live Placement onto matching snapshot slots (2222) |
| Restore | `WorkspaceController::restore` | `slot = sessionAppearanceValue(sid)` then `applyPlacementToState(slot, snapshotPose)` |
| Load restore | `completeLoadRestore` | Same merge: store content + snapshot Placement |

Unbound path rows may still carry content on the path map; bound ids never
dual-write crop/orient into the Workspace snapshot.


## Path-XDG load / save helpers (2228–2229)

| Helper | Direction | Notes |
|--------|-----------|--------|
| `applyStoredContentAppearance` | XDG → `WorkspaceItemState` | `includeGrade` / `includeCrop` |
| `fillStoredContentAppearance` | state → XDG | `writeCrop=false` for bound ids |

**Bound SessionImageId:** path XDG may hold orient/flip/grade as a file-level
hint; **crop is id-only** (duplicates share a path). ItemWorld seed always
passes `includeCrop=false`. Image underlay never path-XDG seeds.

**Unbound:** full path XDG including crop is allowed.

## How orient enters the system

1. User rotates → `bakeItemRotate90` → absolute want → `setContentBake` + materialize
2. XDG seed once if sparse empty (`seedSessionAppearanceFromState`) — **Gallery /
   Workspace cold pack only**; never Image underlay install/create
3. Project load → sparse tables

Nothing else may invent quarter turns for Image underlay.

## Image underlay (2181 / 2205)

`installDisplayPixels` in Image mode sets
`appearance = sessionAppearanceValue(sid)` then
`orientAuthorityWant(hasContentOrient(sid), appearance)`.
It does **not** call `wantAppearanceForItem` and does **not** path-XDG seed.
Applied is written onto the new item after materialize to match that sparse want.

`createItemFromImage` / `contentLayoutSize` / `applyContentLayoutSize` /
`wantAppearanceForItem` use the same orient-authority gate.

## First open Workspace→Image (2182)

**Bug:** XDG seed on Image install/enter wrote path orient into contentBake for
ids that only had Workspace placement. Workspace showed host-raw; Image applied
XDG → rotated unrotated images (first open; worse after other images via seed).

**Rule:** Image materialize uses explicit contentBake/crop only. No XDG seed on
Image install/enter. Placement-only rows force identity content for underlay.

## Layout vs pixels (2183)

User log: `Image install sid=1 turns=0 bake=0` yet still looked rotated.
Cause: `contentLayoutSize` applied **path XDG** for bound sessions → transposed
layout box while materialize used identity host pixels.

Bound SessionImageId layout/paint content ops = ItemWorld only. Path XDG only
for unbound path rows.
