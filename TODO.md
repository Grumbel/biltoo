# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2703.3-cold-gallery-size-book` (base `6c3e877`).

### 2703.3 — Cold Gallery sizeReady vs book / dual pack
Audit: cold open size gate → virtual plan packs from size **book**.
`Bridge::sizeReady` skipped `rememberImageSize` when any definitive existed,
so a stale larger book entry kept wrong plan geometry while tiles used
thumtoo native (top-left paint). Parallel `ContentChange` applyLayout during
the gate raced the virtual plan.

Fix:
- sizeReady: take+remember when book ≠ SizeReply; always applyProbedImageSize
- applyProbedImageSize: no ContentChange pack while size gate active

### 2703.2 — Workspace delete BSP UAF
### 2703.1 — Soft F5 size book take

### Apply
```bash
git pull --ff-only …/biltoo-2703.3-cold-gallery-size-book-6c3e877.bundle HEAD
```
