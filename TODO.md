# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2772.1-annot-freehand-highlighter` (base `b8a0cf3`).

### Decisions (user)
1. Embed annotations in `.biltoo`
2. Default tool = freehand highlighter
3. Sticky notes → Phase C
4. Mouse only for now

### 2772.1 Phase A foundation
- `src/annotation/` types, session, controller
- Freehand highlighter, Multiply blend, page/source space
- Paint + mouse via ViewShellChrome
- Tools toolbar + Image menu (Clear page)
- Project save/load `annotations` JSON array

### Next
- Text-snapped highlight (region quads)
- Colour/width UI, undo, stroke simplify
- Verify Multiply on GL viewport with dark text

### Apply
```bash
git pull --ff-only …/biltoo-2772.1-annot-freehand-highlighter-b8a0cf3.bundle HEAD
```
