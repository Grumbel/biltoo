# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2657.2-menu-polish-docs` (base `9740316`).

### Done
- **2657.1** Menu/shortcut/enablement polish.
- **2657.2** Deduped slideshow enablement; Help texts for Fit/Workspace/Export;
  `RELEASE_0.2.0.md` records batch appearance + menu polish as landed.

### Host before 0.2.0 tag
1. Build green.
2. Smoke: reorder, Export Session Images, Open Selection + committed crop.
3. Smoke: Esc chain; Workspace greys page export + slideshow; Fit/Workspace keys.
4. Batch: filmstrip multi-select orient/colour/crop; Even indices.

### Next (post-tag / 0.2.x)
- Shared BatchTargetPicker widget (crop + adjustments).
- Template-from-page crop; stack preview.
- Location bar navigate-vs-replace semantics.

### Apply
```bash
git pull --ff-only …/biltoo-2657.2-menu-polish-docs-9740316.bundle HEAD
```
