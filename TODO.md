# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2639.1-gallery-pack-extent-measure** (base `e69cffa`).

## Research (off-centre when scrollbars appear)

### Qt facts
- `AlignCenter` only positions the scene while **sceneRect fits in the viewport**.
- Once a scrollbar appears, scene > viewport and **only scroll position** matters.
- Packing to the **full client width**, then a **vertical** bar eating ~extent px,
  makes sceneWidth > clientWidth → horizontal overflow / dual-bar look / “off centre”.

### What failed
- Toggling AlwaysOn mid-pack resized the viewport under the pack and fought AlignCenter.
- Expanding sceneRect to the no-bar client, then letting AsNeeded bars appear, recreated
  the same shrink-after-layout failure.
- Top-left alignment hid the symptom and broke zoom-out.

### Intended design (this tip)
1. **Never toggle scrollbar policy during pack.**
2. **measurePackClient**: from live `viewport()` size, subtract `PM_ScrollBarExtent`
   for any AsNeeded/AlwaysOn axis whose bar is **not already** taking space
   (do not double-subtract when the bar is visible).
3. Tight pack `sceneRect` only (no expand-to-viewport).
4. Enter pack: scroll values 0 (top of pack), not `centerOn(0,0)`.
5. `onViewResized`: if last pack was at ≤1×1 client, one `EnterGallery` repack
   (open often packs at 0×0 and previously never corrected).
6. `refreshScrollBarGeometry` still preserves scene centre across Off↔AsNeeded.

### Apply
```bash
git pull --ff-only …/biltoo-2639.1-gallery-pack-extent-measure-e69cffa.bundle HEAD
```
