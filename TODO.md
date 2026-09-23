# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2345-release-0.2.0-cold-open-notes.**

[docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md) — release plan plus known gaps:

- §4.1 EPUB/PDF Find is per text region only (no cross-box phrases)
- §4.2 Cold-cache open needs more testing/debug: weak progress, slow PDFs
  without thumbs, slow 7z/solid archives

Requires **thumtoo-323**. Code tip includes 2318–2343.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2345.1-release-0.2.0-cold-open-notes-2f201f6.bundle HEAD
```

Next: **2346** (release cut) or post-0.2 open/search work.

## Backlog
- EPUB/PDF text search across text-box boundaries (RELEASE_0.2.0 §4.1)
- Cold-cache open: stage progress, pathological PDF, 7z (RELEASE_0.2.0 §4.2)
- Release cut: VERSION 0.2.0, tag, flake pin, smoke matrix
