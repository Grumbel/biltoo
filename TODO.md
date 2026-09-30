# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2880.1-annotation-qhash-warning (on `ea477d6` + agent stack).

### 2880.1
- AnnotationSession::ensurePage: avoid `it.value()` after insert (GCC
  -Wnull-dereference false positive on QHashPrivate when inlined).

### Prior
- 2879.1 toolbar / tile fail
- 2878.1 dock/toolbar chrome

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
