# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2711.3-panels-top-level-menu` (base `b65f69e`).

### 2711.3 — Panels as top-level menu
- **Panels** is its own menu bar entry (not a View submenu).
- Toolbar Panels popup still shares `m_panelsMenu`.

### 2711.2 — Toolbar Panels popup
### 2711.1 — Dock layout persistence (version-gated) + Reset Panel Layout
### 2710.5 — Selection/hover paint order fix

Needs thumtoo **343.6** for EPUB OCR.

### Apply
```bash
git pull --rebase …/biltoo-2711.3-panels-top-level-menu-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — KDDockWidgets (optional)
Only if QMainWindow docks prove insufficient. Spike against nixpkgs first.
