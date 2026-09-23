# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2347-release-0.2.0-location-crop-menu.**

[docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md) extended known gaps / later work:

- §4.1 Text search per region only
- §4.2 Cold-cache open (PDF, 7z, progress)
- §4.3 PDF Embedded Images testing
- §4.4 Location (Ctrl+L) session vs leaf URL semantics
- §4.5 Crop can be lost on some actions (Open Selection) — RC must-test
- §4.6 Menu/GUI cleanup notes
- §4.8 Dual ImageView → **0.3.0**, not 0.2

Requires **thumtoo-323**. Code tip includes 2318–2343.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2347.1-release-0.2.0-location-crop-menu-2f201f6.bundle HEAD
```

Next: **2348** (release cut / RC fixes) or post-0.2 items above.

## Backlog
- See RELEASE_0.2.0.md §§4.1–4.8
- Release cut: VERSION 0.2.0, tag, flake pin, smoke matrix (incl. crop + Location)
