# Biltoo GUI-thread responsiveness audit

**Date:** 2026-09-12  
**Tree tip at audit start:** `b552c50`  
**Focus:** Gallery scrolling and archive/session load stalls.

## Findings

| ID | Sev | Issue | Status |
|----|-----|-------|--------|
| G1 | P0 | MTime/FileSize sort QFileInfo in comparators | **Fixed 527** |
| G2 | P0 | preparePaths exists() on GUI | **Fixed 527** |
| G3 | P0 | installDisplayPixels storms | **Fixed 528** — ≤48 installs/turn + reschedule |
| G4 | P0 | decode window 3× O(n) host passes | **Fixed 528** — pass1+1b merged |
| G5 | P1 | Toolbar zoom sync decode window | **Fixed 527** |
| G6 | P1 | setInterest URI convert on GUI | **Fixed 528** — `warmUris` worker |
| G7 | P1 | Soft completion sync decode window | **Fixed 527** |
| G8 | P1 | Metadata Exiv2 / full load on GUI | **Fixed 528** — Exiv2 worker; no GUI full load |

## Residual

- Pass 2 still walks all items for on-screen/blank detection (debounced); off-screen
  interest/`rest` lists are capped during the scan (**529**).
- setInterest still builds interest vectors on GUI (URI cache hit after warm).
- `fillImageAnalysis` still on GUI when decodedHint is present (CPU only).

