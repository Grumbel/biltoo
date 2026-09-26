# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.2-tool-pan-select-text` (base `b65f69e`).

### 2712.2 — Tool-aware pan + Select→text rubber
- Left pan follows **Pan tool** (all modes); middle always pans; Alt+left pans.
- **Space** held = temporary pan when tool is *not* Pan (Space still toggles slideshow when Pan is active).
- Image **Select** tool: left-drag text rubber-band when regions exist; Shift+drag still works under Pan.
- Image left-drag legacy preference only applies together with Pan tool.

### 2712.1 — Shared ViewInteraction tool ownership

### Next
- Crop on toolbox (modal enter)
- Multi-page TextSelection data
- Optional: click-select single text region without drag

### Apply
```bash
git pull --rebase …/biltoo-2712.2-tool-pan-select-text-b65f69e.bundle HEAD
```
