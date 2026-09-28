# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2775.1-annot-page-space-paint` (base `b8a0cf3`).

### 2775.1
- Annotation paint in **scene space** (was view pixels under scene transform → wrong place)
- Mapping aligned with `regionImageRect` / OCR_COORDINATES + CONTENT_COORDINATES
- Docs: ANNOTATION_OVERLAY §5, cross-links from OCR/CONTENT

### Apply
```bash
git pull --ff-only …/biltoo-2775.1-annot-page-space-paint-b8a0cf3.bundle HEAD
```
