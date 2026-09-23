# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2346-release-0.2.0-pdf-embedded-notes.**

[docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md) known gaps:

- §4.1 EPUB/PDF Find — per text region only
- §4.2 Cold-cache open — progress, slow PDF, 7z
- §4.3 Image → PDF Embedded Images — needs more testing

Requires **thumtoo-323**. Code tip includes 2318–2343.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2346.1-release-0.2.0-pdf-embedded-notes-2f201f6.bundle HEAD
```

Next: **2347** (release cut) or post-0.2 work above.

## Backlog
- EPUB/PDF text search across text-box boundaries (§4.1)
- Cold-cache open: stage progress, pathological PDF, 7z (§4.2)
- PDF Embedded Images: corpus testing and hardening (§4.3)
- Release cut: VERSION 0.2.0, tag, flake pin, smoke matrix
