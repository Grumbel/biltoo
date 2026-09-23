# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2397-size-first-all-layouts-emb** (on top of `660c49c` stack).

Includes **2381–2396**.

### 2397
- Size gate active for **all** packaged Gallery layouts (including grid)
- Tiles blocked until session sizes settle
- SizeReply underlay always → ImageCache; Gallery install accepts EMB ≤320

**Next:** RC smoke; confirm LQIP/EMB appears after sizeReady when Store has blob.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2397.1-size-first-all-layouts-emb-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2396
- [x] 2397 size-first all layouts + EMB underlay
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
