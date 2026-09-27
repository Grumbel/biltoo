# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.18-tr-string-fix` (base `bcbb97e`).

### Coding rule (agent)
**Never put a raw newline inside a C/C++ `"..."` string.** Use either:
- adjacent literals: `"line1\n" "line2\n"`, or
- one string with explicit `\n`.

Raw newlines in quotes break the compile (`missing terminating " character`).

### Apply
```bash
git pull --ff-only …/biltoo-2714.18-tr-string-fix-bcbb97e.bundle HEAD
```
