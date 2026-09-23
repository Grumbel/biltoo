# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2390-lqip-cache-only-seed-size-reply** (on top of `660c49c` stack).

Includes **2381–2389**.

### 2390
LQIP policy: use ImageCache when hot; seed only from `request_size` SizeReply
(EMB/LQIP with size row). Never `get_lqip` / generation. Memo size alone is
not a warm hit without underlay in ImageCache.

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2390.1-lqip-cache-only-seed-size-reply-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2389
- [x] 2390 LQIP cache-only + size-reply seed
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
