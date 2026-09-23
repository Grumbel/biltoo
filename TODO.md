# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2350-src-crop-subdir.**

Source layout phases 1–2:

- `src/session/` — session document/appearance/export/…
- `src/crop/` — crop controller/session/geometry/command/…

See [docs/SRC_LAYOUT.md](docs/SRC_LAYOUT.md). Next: **gallery/**.

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2350.1-src-crop-subdir-2f201f6.bundle HEAD
```

## Backlog (0.2.0)
- [x] `src/session/`
- [x] `src/crop/`
- [ ] `src/gallery/`
- [ ] `src/shell/`
- [ ] `src/display/` (optional)
- [ ] RC smoke; VERSION 0.2.0 + tag
