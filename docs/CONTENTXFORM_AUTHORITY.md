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

Assembled by `sessionAppearanceValue(id)` / sparse fill.

## Presentation-only (ephemeral)

| Field | Where |
|-------|--------|
| Applied ContentXform fingerprint | **On the live `ImageItem` only** |

Meaning: "pixels currently attached were materialized with this Value."

Used for: mid-edit while the same item is live; tile paint orient of host-native tiles.

## Structural rules (2179→2180)

1. **Materialize want** for a new underlay = sparse contentBake/crop only.
2. **Never** read ItemWorld `applied` residual as want for install (it outlived
   Workspace→Image and overrode sparse — random wrong orient).
3. On mode leave: **flush** item applied → sparse contentBake/crop, then
   **clear** ItemWorld applied for that id.
4. Host pixels in ImageCache are unoriented; `materializeDisplay(host, want)` once.
5. Tiles stay native grid; paint applies applied (item-local) orient.

## How orient enters the system

1. User rotates → `bakeItemRotate90` → absolute want → `setContentBake` + materialize
2. XDG seed once if sparse empty (`seedSessionAppearanceFromState`)
3. Project load → sparse tables

Nothing else may invent quarter turns for Image underlay.
