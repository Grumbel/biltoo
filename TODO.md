# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2389-size-pack-regression-tests** (on top of `660c49c` stack).

Includes **2381–2388**.

### 2389
Regression tests for recurring size/pack bugs:
- `tests/imagesizebook_test.cpp` — provisional vs definitive, stand-in danger,
  plan eligibility (`hasDefinitive || isFailed`)
- `tests/gallerylayout_test.cpp` — Grid no clip, GridCrop square cell,
  masonry/side-by-side aspect preservation

Run: `ctest -R 'imagesizebook|gallerylayout'` (after configure).

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2389.1-size-pack-regression-tests-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2388
- [x] 2389 size/pack regression tests
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
