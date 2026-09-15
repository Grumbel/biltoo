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
    basis = cropSourceSize (or oriented)
    if basis orientation class ≠ oriented:
        // Stale crop (turns changed without mapCropThrough*): map through
        // one 90° step matching QImage::trueMatrix — never linear-scale
        // across an orientation change (wrong size unless crop aspect == full).
        map crop + basis through ContentXform::mapCropRectThroughContentRotate90
    scale cropRect from basis → oriented space (resolution only)
    return crop size          // never full-frame when cropped
else:
    return oriented
```

After a content ±90°, callers must run `mapCropThroughContentRotate90` so
cropRect / cropSourceSize stay in the new post-orient space. layoutSize's
orientation-mismatch path is defense in depth.

| Input | Result |
|-------|--------|
| No crop, 0 turns | `native` |
| No crop, odd turns | swapped `native` |
| Crop 800×600 on 4000×3000 | `800×600` |
| Soft 400×300 + crop recorded at 4000×3000 | scaled crop size |
| turns=1, crop still on native 4000×3000 (stale) | mapped crop size (not linear scale) |

**Bug class avoided:** cropped pixels painted into a full-frame intrinsic → stretch;
rotate after crop with stale `cropSourceSize` linear-scaled to wrong intrinsic.

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

## Install invariant (bound tiles)

For any item with a valid `SessionImageId`:

1. **ImageCache** holds only **unoriented host** (path-keyed). Never write a
   content-baked sample under the path key (duplicate/peer/undo display must
   not `ImageCache::put` the bake).
2. **`installDisplayPixels`** is the sole host→display gate. Incoming is
   host-raw. When store want has crop/orient/grade:
   - GUI-safe host (long edge ≤ `kGuiMaterializeMaxEdge`):  
     `display = materializeDisplay(host, want)` → `attachDisplaySample`.
   - Larger host: clamp to max edge, materialize as **SoftPreview** stand-in
     (crop visible immediately), then `scheduleAsyncHostRematerialize` when
     FullSource was requested. **Never** attach raw host under want.
3. **`appliedContentXform` is set only inside `attachDisplaySample`** after a
   real bake. Multi-MP helpers that cannot bake crop (e.g. `applyContentToItem`
   edge limit, `applyContentBakes`) must not claim full want including crop.
4. SoftPreview **includes crop** (scaled into soft space). Helpers must not
   strip `hasCrop` before `materializeDisplay`.
5. Path duplicates: one host sample in ImageCache; **per-id** materialize on
   each tile from `m_appearance.get(sid)`.
6. **Already-baked display** (peer sync, undo after-image, Workspace
   duplicate of live pixels): `attachDisplaySample` only — never
   `installDisplayPixels` / `createItemFromImage` with baked pixels.

Gallery soft ladder, PreferCache climbs, `createItemFromImage` (content want),
and restore/Workspace reapply all go through this gate or
`rematerializeItemContent`. Post-install “if applied ≠ want, rematerialize”
checks are not a substitute for the gate.

## Entry points

| Path | API |
|------|-----|
| Decode / ladder / soft | `installDisplayPixels` → materialize → `attachDisplaySample` |
| `createItemFromImage` + want | seed chrome → `installDisplayPixels` |
| Restore / Workspace full miss | `rematerializeItemContent` |
| Live rotate/flip ≤512 host | `tryRematerializeFromHost` → `attachDisplaySample` |
| Live rotate/flip multi-MP | incremental bake + `scheduleAsyncHostRematerialize` → `finishAsync` → `attachDisplaySample` |
| Peer / duplicate / undo after | `attachDisplaySample` (no ImageCache put) |
| Crop Apply | write `m_appearance` by id → materialize → attach; emit id-keyed filmstrip |
| Tests | `tests/contentxform_test.cpp` |

## Crop

**Full contract:** [CROP_MODE.md](CROP_MODE.md).

Critical Apply rule: if enter installed FullSource, soft crop attach must
`clearDecodedPixels()` first — otherwise `setPreviewImage` is a no-op and the
filmstrip receives a full-frame override.

## Crop

**Full contract:** [CROP_MODE.md](CROP_MODE.md).


Entering crop loads the full frame with content flips/turns only (no crop bake).
`installFullImageForCrop` uses the same host rematerialize / attach path; multi-MP
falls back to incremental content bake + async pure rematerialize.

Apply: record crop in session appearance under **crop target id**, materialize,
set intrinsic from `layoutSize` (crop box), keep placement scale identity where
Workspace footprint must not jump (see recent crop tips).

## ImageView::rematerializeItemContent

When an item already holds (or host has) raw pixels and absolute want is known,
use this instead of `SessionAppearance::applyContentToItem`. Same rules as
install: host ≤512 materialize + attach; multi-MP clamps to SoftPreview
stand-in (crop visible) then schedules async FullSource materialize. Never
sets applied without attaching a matching bake.

## Propagation (modes and widgets)

After any content transform write (`commitItemSessionEdit`):

| Consumer | Update |
|----------|--------|
| `SessionAppearanceStore` | Written in bake/record/persist (by `SessionImageId`) |
| Live peers + Workspace/Gallery **stashed** items same id | `syncSessionEditPeers` — pixels, intrinsic, crop flags, applied xform, grade |
| Workspace saved snapshot | `updateWorkspaceSavedAppearance` |
| **Filmstrip** | `sessionAppearanceChanged` / `sessionCropApplied` (id-keyed) |
| **Gallery** | `requestDebouncedGalleryPack(ContentChange)` |
| **Workspace** | `updateWorkspaceSceneRect()` |
| **Image mode** | tight `sceneRect` around the item |

Entry points that must end in `commitItemSessionEdit` (or call the same hub):
toolbar rotate/flip, Workspace chrome, crop Apply/Reset, grade changes that
persist appearance, paste/bind that freezes appearance.

Do **not** invent a second pack/filmstrip path beside this hub.

