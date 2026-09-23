# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2392-gallery-emb-underlay-accept** (on top of `660c49c` stack).

Includes **2381–2391**.

### 2392
Gallery SoftPreview underlay accept band is EMB (≤320), not only ThumbHash
(≤96). sizeReady path installs + preview fallback.

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2392.1-gallery-emb-underlay-accept-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2391
- [x] 2392 Gallery EMB underlay accept
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
