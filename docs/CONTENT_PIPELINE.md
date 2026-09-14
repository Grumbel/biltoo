# Content appearance pixel pipeline

**One xform value. One materialize. One layout size. Applied fingerprint on the item.**

```text
want: ContentXform::Value   (session appearance — absolute)
native: logicalSize(path)   (file geometry, never oriented)

raw decode (disk / ladder / cache)
        │
        ▼
materializeDisplay(raw, want → WorkspaceItemState, kind)
        │  1. flips  2. quarter turns  3. crop  4. color
        ▼
display pixels + layoutSize(native, want)
        │
        ▼
ImageItem: pixels + applied = want
```

## Pure helpers (`src/contentxform.{h,cpp}`)

| Function | Role |
|----------|------|
| `ContentXform::Value` | flips, turns, crop, color — no placement |
| `layoutSize(native, x)` | file size → contentRect size (odd turns swap axes) |
| `equal(a, b)` | content fields only |
| `needsRematerialize(applied, want, shownEdge, incomingEdge)` | install decision |

## Rules

1. **SessionImageId** owns absolute appearance (`WorkspaceItemState`). Path is decode only.
2. **Raw in, display out** via `materializeDisplay` / `installDisplayPixels`.
3. **Never materialize twice** on the same buffer. Live `bakeRotate90` / `bakeFlip` update pixels **and** `item->setAppliedContentXform(want)`.
4. **Layout is pure:** `layoutSize(native, want)` — not a side effect of `QImage::transformed`.
5. **Next install:** if `applied == want` and no strict edge upgrade, skip; else rematerialize.

## Accept / attach

`canAcceptDisplaySample` uses `ContentXform::needsRematerialize(applied, want, shown, incoming)`
when the item has an applied fingerprint. Same xform + no edge upgrade → reject.
Soft still never demotes full decode.

`wantAppearanceForItem` is the single resolver for absolute want (session store,
path map, live flags).

## Entry points

| Path | API |
|------|-----|
| Attach to ImageItem | `ImageView::installDisplayPixels` |
| QImage-only | `applyContentToImage` → `materializeDisplay` |
| Tests | `tests/contentxform_test.cpp` |


# Content appearance pixel pipeline

**One function. One order. Every display path.**

```text
raw decode (disk / ladder / cache)
        │
        ▼
SessionAppearance::materializeDisplay(raw, state, kind)
        │  1. contentHFlip / contentVFlip
        │  2. contentQuarterTurns (QImage transform, same as bakeRotate90)
        │  3. cropRect in post-orient space
        │  4. color grade
        ▼
display pixels on ImageItem / filmstrip / slideshow blit
```

## Rules

1. **SessionImageId** owns `WorkspaceItemState` (IDENTITY.md). Path is decode only.
2. **Raw in, display out.** Callers pass undecoded-appearance pixels into
   `installDisplayPixels` or `applyContentToImage` / `materializeDisplay`.
3. **Never materialize twice** on the same buffer. Incremental user edits use
   `bakeFlip` / `bakeRotate90` on the item and update absolute state; the next
   full install from disk uses `materializeDisplay` with that state.
4. **Gallery soft and Image full use the same function.** Mode transitions cannot
   disagree on orientation if both go through `installDisplayPixels`.

## Accept / attach

`canAcceptDisplaySample` uses `ContentXform::needsRematerialize(applied, want, shown, incoming)`
when the item has an applied fingerprint. Same xform + no edge upgrade → reject.
Soft still never demotes full decode.

`wantAppearanceForItem` is the single resolver for absolute want (session store,
path map, live flags).

## Entry points

| Path | API |
|------|-----|
| Attach to ImageItem | `ImageView::installDisplayPixels` |
| QImage-only (filmstrip, slideshow) | `applyContentToImage` → `materializeDisplay` |
| Item that already holds raw full | `applyContentToItem` → `materializeDisplay` |

## Crop space

`cropRect` / `cropSourceSize` are in **post-orient** space (after flips/turns),
updated by `mapCropThrough*` when the user rotates/flips with a crop active.
`materializeDisplay` applies crop after orient so reload matches the live item.

## Attach sites that must tag `applied`

| Site | Notes |
|------|--------|
| `installDisplayPixels` | Primary path |
| `createItemFromImage` | Worker-baked LoadReplace |
| `applyContentToItem` | Raw item → materialize |
| `applyContentBakes` | Incremental flip/turn bake |
| Peer sync / crop undo | Copy or tag from known state |

