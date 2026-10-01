# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2883.1-tts-document-speak (linear stack on `e345338`).

### 2883.1
- TTS reads the **whole session document** (`SpeakScope::FullDocument`); page
  breaks become paragraph breaks, not stop points.
- Selection is a **start anchor** (optional mid-box bias from click X in the
  region). Changing selection while speaking **seeks** via `seekToTextOffset`.
- Image mode **follows** the page of the active span during document speak.

### 2882.1
- Double view / spread: place pages at slot centre + height-match scale.

### 2881.2 / 2881.1
- PipeWire LD_LIBRARY_PATH for Qt Multimedia; Gallery↔Image latency doc.

### Bundle policy
Work-line base: `e345338`. No parallel histories. Tip = `e345338..HEAD`.
