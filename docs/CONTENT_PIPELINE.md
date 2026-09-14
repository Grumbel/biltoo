# Content appearance pixel pipeline

**One want. One materialize. One attach. No special cases.**

```text
want = ContentXform::Value from session appearance (SessionImageId)
raw  = decode / ImageCache (never oriented)

materializeDisplay(raw, want)   // worker if edge > kGuiMaterializeMaxEdge
        │
        ▼
attachDisplaySample(item, display, want, kind)
        │  pixels + layoutSize(fileNative, want) + applied fingerprint
        ▼
ImageItem
```

## Rules

1. **All** display attaches go through `attachDisplaySample` (or `installDisplayPixels` which ends there).
2. **`layoutSize(fileNative, want)`** is the only geometry rule. See below.
3. **applied** fingerprint on the item drives `needsRematerialize` / `canAccept`.
4. GUI may materialize only when long edge ≤ `kGuiMaterializeMaxEdge`; larger samples use worker or incremental bake + async pure rematerialize.
5. No landscape/portrait heuristics. No path-only orientation without want.
6. **ImageCache holds raw (unoriented) samples** for a path. Do not put
   appearance-baked FullSource over the host when live rotate/flip will
   rematerialize from host again (double-bake).

## layoutSize (geometry)

```text
oriented = swap W/H if odd quarter-turns, else native
if want.hasCrop && cropRect non-empty:
    scale cropRect from cropSourceSize → oriented space
    return crop size          // never full-frame when cropped
else:
    return oriented
```

| Input | Result |
|-------|--------|
| No crop, 0 turns | `native` |
| No crop, odd turns | swapped `native` |
| Crop 800×600 on 4000×3000 | `800×600` |
| Soft 400×300 + crop recorded at 4000×3000 | scaled crop size |

**Bug class avoided:** cropped pixels painted into a full-frame intrinsic → stretch.

`native` must be **file / probe size**, not an already-oriented display sample
(double-swap). Tests: `tests/contentxform_test.cpp`.

## Ownership of want

| Store | Key | Role |
|-------|-----|------|
| **`SessionAppearanceStore`** (`ImageView::m_appearance`) | `SessionImageId` | **Sole** content appearance for bound session images (crop, flips, turns, grade) |
| `SessionDocument` | list index | Paths + ids only (ordered session). No transforms. |
| Path-keyed Thumtoo XDG appearance | path / content id | **Orient/flip (and grade) only** as a file-level hint. Never crop for bound ids. Seed into `m_appearance` does not copy crop. Persist from bound edits does not write crop. |
| Filmstrip id overrides | `SessionImageId` | Derived view of store after emit; never path-wide on a bound strip |

Path is decode source only. Duplicates share a path and must keep independent
appearance under different ids. See [IDENTITY.md](../IDENTITY.md),
[CONTENT-VARIANT.md](../CONTENT-VARIANT.md).

## Entry points

| Path | API |
|------|-----|
| Decode / ladder / soft | `installDisplayPixels` → materialize → `attachDisplaySample` |
| Live rotate/flip ≤512 host | `tryRematerializeFromHost` → `attachDisplaySample` |
| Live rotate/flip multi-MP | incremental bake + `scheduleAsyncHostRematerialize` → `finishAsync` → `attachDisplaySample` |
| Crop Apply | write `m_appearance` by id → materialize → attach; emit id-keyed filmstrip |
| Tests | `tests/contentxform_test.cpp` |

## Crop

Entering crop loads the full frame with content flips/turns only (no crop bake).
`installFullImageForCrop` uses the same host rematerialize / attach path; multi-MP
falls back to incremental content bake + async pure rematerialize.

Apply: record crop in session appearance under **crop target id**, materialize,
set intrinsic from `layoutSize` (crop box), keep placement scale identity where
Workspace footprint must not jump (see recent crop tips).

## ImageView::rematerializeItemContent

When an item already holds (or host has) raw pixels and absolute want is known,
use this instead of `SessionAppearance::applyContentToItem`. Same rules:
host ≤512 materialize + attach; multi-MP schedules async pure materialize.
