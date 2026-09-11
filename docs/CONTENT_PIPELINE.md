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
