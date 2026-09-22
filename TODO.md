# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2320-session-export-images.**

Session bake export: **File → Export Images…** (folder / CBZ / multi-page PDF).
Bakes rotate/flip/crop via `SessionAppearance::applyContentToImage`; never
overwrites sources. Workspace page exports renamed and mode-gated.

See `docs/SESSION_EXPORT_AND_ORDER.md`. Includes 2318–2319. Requires **thumtoo-323**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-323.1-try-exif-external-linkage-bd9cca0.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2320.1-session-export-images-999be36.bundle HEAD
```

Next: **2321**.

## Backlog

- Session **reorder UI** (filmstrip/list) — `docs/SESSION_EXPORT_AND_ORDER.md` §2
- Export: progress dialog / cancel; EXIF strip option; selection from Gallery multi-select when filmstrip hidden
