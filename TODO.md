# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2385-image-zoom-enter-persist** (on top of `660c49c` stack).

Includes **2381–2384**.

### 2385
- Defer Image framing until item size is reliable (avoid fitInView on 1×1 →
  ~5000% zoom).
- Capture view scale/pan on Image leave; keep sticky preference across modes.
- Size-arrival reframe uses preserved/sticky path when set.

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2385.1-image-zoom-enter-persist-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2384
- [x] 2385 Image zoom enter / persist
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
