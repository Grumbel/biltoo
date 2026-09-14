# Content appearance pixel pipeline

**One want. One materialize. One attach. No special cases.**

```text
want = ContentXform::Value from session appearance
raw  = decode / ImageCache (never oriented)

materializeDisplay(raw, want)   // worker if edge > kGuiMaterializeMaxEdge
        │
        ▼
attachDisplaySample(item, display, want, kind)
        │  pixels + layoutSize(native, want) + applied fingerprint
        ▼
ImageItem
```

## Rules

1. **All** display attaches go through `attachDisplaySample` (or `installDisplayPixels` which ends there).
2. **layoutSize(native, want)** is the only geometry rule (crop → baked sample size).
3. **applied** fingerprint on the item drives `needsRematerialize` / `canAccept`.
4. GUI may materialize only when long edge ≤ `kGuiMaterializeMaxEdge`; larger samples use worker or incremental bake + async pure rematerialize.
5. No landscape/portrait heuristics. No path-only orientation without want.

## Entry points

| Path | API |
|------|-----|
| Decode / ladder / soft | `installDisplayPixels` → materialize → `attachDisplaySample` |
| Live rotate/flip ≤512 host | `tryRematerializeFromHost` → `attachDisplaySample` |
| Live rotate/flip multi-MP | incremental bake + `scheduleAsyncHostRematerialize` → `finishAsync` → `attachDisplaySample` |
| Tests | `tests/contentxform_test.cpp` |


## Crop

Entering crop loads the full frame with content flips/turns only (no crop bake).
`installFullImageForCrop` uses the same host rematerialize / attach path; multi-MP
falls back to incremental content bake + async pure rematerialize.


## ImageView::rematerializeItemContent

When an item already holds (or host has) raw pixels and absolute want is known,
use this instead of `SessionAppearance::applyContentToItem`. Same rules:
host ≤512 materialize + attach; multi-MP schedules async pure materialize.

