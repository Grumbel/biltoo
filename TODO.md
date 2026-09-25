# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2657.2-menu-polish-docs` (base `9740316`).

### Agent verification (static)
- BatchTargets / orient helpers / colour+crop batch / filmstrip provider /
  ContentUndoMacro / menus (Document, Layout, Export Session Images): **present**
- Slideshow enablement single path: **OK**
- Full Qt/`nix build`: **not available** in agent sandbox — host must compile

### Host before 0.2.0 tag
1. `git pull` this bundle; build green.
2. Smoke: reorder, Export Session Images, Open Selection + committed crop.
3. Smoke: Esc chain; Workspace greys page export + slideshow; Fit/Workspace keys.
4. Batch: filmstrip multi-select orient/colour/crop; Even indices; autocrop wait.

### Post-tag / 0.2.x (not blocking)
- Shared BatchTargetPicker widget.
- Template-from-page crop; stack preview.
- Location bar navigate-vs-replace.

### Apply
```bash
git pull --ff-only …/biltoo-2657.2-menu-polish-docs-9740316.bundle HEAD
```
