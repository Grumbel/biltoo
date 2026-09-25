# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2659.3-cross-region-text-search` (base `9740316`).

### Verify (agent, Qt 6.4 offscreen)
| Suite | Result |
|-------|--------|
| textsearchpolicy | 10 passed |
| batchtargets | 11 passed |
| contentundomacro | 5 passed |
| croprecipe | 13 passed |
| **Total** | **39 passed, 0 failed** |

### 2659.3
- Overlapping same-line boxes treated as tight join (mid-word PDF).
- Extra unit test for overlapping mid-word split.

### Apply
```bash
git pull --ff-only …/biltoo-2659.3-cross-region-text-search-9740316.bundle HEAD
ctest -R 'textsearchpolicy|batchtargets|contentundomacro|croprecipe' --output-on-failure
```
