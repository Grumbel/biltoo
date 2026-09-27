# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2720.2-session-input-path` (base `f7c0a46` / origin/master).

### 2720.2 — SessionInputPath + tests (URL ≠ filesystem path)
- `SessionInputPath::classify` / `canonicalForSession` / `urlPathLeafName` /
  `remoteCachePath` — QUrl classification; no QFileInfo on URL strings.
- `SessionExpand` uses it for http(s) download-to-cache and canonical paths.
- Unit test `sessioninputpath`.

### 2720.1 — Open http(s) via expand (download to cache)

### Already on origin
- 2719.x scripting brainstorm docs, 2718.2 world vs viewpoint, 2717.1 gdb quit.

### Open product direction (not scheduled)
- TTS across modes / pages; Gallery speech-cursor highlight.
- Activity reporting as world state.
- Scripting remains napkin-level until a command-table inventory is worth doing.
- Grid crop layout-only; OCR OSD.

### Apply
```bash
git pull --ff-only …/biltoo-2720.2-session-input-path-f7c0a46.bundle HEAD
```
