# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2400-gallery-underlay-policy** (on top of `660c49c` stack).

Includes **2381–2399**.

### 2400 (spec alignment)
- One `tryInstallGalleryUnderlay` for all Gallery underlay installs
- Offline virtual slots paint ImageCache underlay when hot
- ImageCache underlay-aware LRU + 1024 entry cap

**Next:** RC smoke cold + large session + second open.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2400.1-gallery-underlay-policy-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2399
- [x] 2400 gallery underlay policy
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
