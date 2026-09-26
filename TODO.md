# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2711.2-panels-toolbar-button` (base `b65f69e`).

### 2711.2 — Toolbar Panels popup
- Main toolbar **Panels** tool button (InstantPopup) shares `m_panelsMenu`
  with **View → Panels**.

### 2711.1 — Dock layout persistence + Panels menu
- Version-gated `dockLayoutState` / `dockLayoutVersion` (`kDockLayoutStateVersion = 1`).
  Bump the constant if a saved layout crashes on restore.
- **View → Panels** + **Reset Panel Layout**.

### 2710.5 — Selection/hover paint order fix

Needs thumtoo **343.6** for EPUB OCR.

### Apply
```bash
git pull --rebase …/biltoo-2711.2-panels-toolbar-button-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — KDDockWidgets (optional)
Only if QMainWindow docks prove insufficient. Spike against nixpkgs first.
