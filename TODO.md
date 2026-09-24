# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2422.1-drop-legacy-setSourceImage** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- **Removed** dead `ImageItem::setSourceImage` (geometry re-entry + double grade risk)
- Sole private install mutators: `setSourceImageReady` + `setPreviewImage`
- Docs: IMAGEVIEW_ITEM_OWNERSHIP friends section + backlog 3c updated

### Verification notes (prior)
- Static friend/call-site graph clean at 2421.3
- Full `nix build` not run in this sandbox

### Next
- Dual ImageView (0.3) — product track
- Phase 6 (REFACTOR): header closure → paint/input/size-book/rematerialize collaborators

### Apply
```bash
git pull --ff-only …/biltoo-2422.1-drop-legacy-setSourceImage-d80d461.bundle HEAD
```
