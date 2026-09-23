# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2352-src-shell-subdir.**

Source layout phases 1–4:

- `src/session/`, `src/crop/`, `src/gallery/`, `src/shell/`

`icons.qrc` paths adjusted to `../../data/…`. Next optional: **`src/display/`**.

See [docs/SRC_LAYOUT.md](docs/SRC_LAYOUT.md). Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2352.1-src-shell-subdir-2f201f6.bundle HEAD
```

## Backlog (0.2.0)
- [x] session / crop / gallery / shell
- [ ] display (optional pre-tag)
- [ ] RC smoke; VERSION 0.2.0 + tag
