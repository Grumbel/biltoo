# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2393-virtual-window-lqip-seed** (on top of `660c49c` stack).

Includes **2381–2392**.

### 2393
`syncVirtualWindow` seeds each materialized cell from ImageCache underlay
(request_size SizeReply) immediately — not only on a later decode tick.

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2393.1-virtual-window-lqip-seed-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2392
- [x] 2393 virtual window LQIP seed
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
