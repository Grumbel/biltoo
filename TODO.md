# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2660.1-text-block-id` (base `9740316`).
**Thumtoo:** `thumtoo-339.1-text-block-id-8f6d77c` (requires tip with `TextRegion::block_id`).

### Done
- Thumtoo: `block_id` on extract (MuPDF PDF/EPUB); TTL4 cache format.
- Biltoo: `TextRegion::blockId`; Find searches **per block** (columns do not
  form one phrase stream); reading-order sort is block-major.
- Unit tests: 11 textsearchpolicy including threeColumns_noCrossJoin.

### Later (0.3+)
- Tesseract / OCR-LLM for image-only pages and layouts beyond MuPDF blocks.

### Apply
```bash
# thumtoo first
git -C thumtoo pull --ff-only …/thumtoo-339.1-text-block-id-8f6d77c.bundle HEAD
# biltoo
git pull --ff-only …/biltoo-2660.1-text-block-id-9740316.bundle HEAD
# bump flake thumtoo input if needed
```

**Note:** Old cached text layers (TTL3) have `blockId=-1` until re-extracted.
