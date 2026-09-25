# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2703.1-sizebook-tile-mismatch` (base `6c3e877`).

### 2703.1 — Gallery only top-left tile / wrong size (F5 no-op, reopen fixes)
Root cause: size book could keep a **stale larger definitive** while thumtoo
tile-native size was correct. Tiles planned/painted in native space only cover
the top-left of an oversized `contentRect`. Soft F5 did not `take()` the size
book (hard reload / reopen did). Probe callback also **skipped** install when
any definitive existed, so the correct size never applied.

Fix:
- Soft F5: `hostSizeBook().take(path)` like hard reload
- Size probe callback: if definitive ≠ probe size, take then install
- `rememberSizeFromDecode`: store cached size forces replace of differing book entry

### Prior
2702.x tile edge stretch / Image framing scale (in 0.2.0).

### Apply
```bash
git pull --ff-only …/biltoo-2703.1-sizebook-tile-mismatch-6c3e877.bundle HEAD
```
