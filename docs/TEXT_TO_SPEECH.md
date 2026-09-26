<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Text-to-speech (TTS) plan

Status: **design only** (not implemented). Relates to the text overlay /
selection stack ([TEXT_OVERLAY.md](TEXT_OVERLAY.md), `TextSelection` in
`src/text/textselection.h`) and the external project
[text2sprech](https://github.com/Grumbel/text2sprech).

## Goals

- Read **selected text** or the **current page** aloud inside biltoo.
- Synthesis stays **local** (Piper); nothing is sent to the cloud.
- Reuse text2sprech’s **piper_server**, wire protocol, and client/playback
  ideas rather than reinventing a second Piper integration.
- Fit biltoo’s text model (page regions + multi-page selection), not
  text2sprech’s document-editor model.

## Non-goals (initial releases)

- Full text2sprech feature parity (HTML reader, presentation mode, export UI).
- In-process Piper linked into biltoo (keep the server process boundary).
- Automatic multi-page “read the whole book” with page-turn (later optional).
- Perfect NLP sentence segmentation (heuristic splitter is enough).

---

## What text2sprech already provides

| Component | Role | Reuse |
|-----------|------|--------|
| `piper_server/` | Unix-socket service: Piper CLI / Python / silent-test, voices, speed | **High** — external process |
| `PROTOCOL.md` | Big-endian length + UTF-8 JSON; `speak` / `audio` / `stop` / voices | **High** — do not fork casually |
| `PiperClient` | Qt `QLocalSocket` client, request ids, full WAV per sentence | **High** — extractable |
| `PiperServerManager` | Spawn/stop server, per-instance socket path | **High** |
| `PlaybackController` | Sentence queue, cancel, voice/speed, play WAV | **High** (trim document-only bits) |
| `SentenceSplitter` (+ Python twin) | Heuristic sentence boundaries | **High** — keep C++/Python aligned |
| DocumentView highlighting | `QTextEdit::ExtraSelection` on a text document | **Low** for biltoo — we highlight **page regions** |

text2sprech is a **document reader**. biltoo is an **image/session viewer** with
a text/OCR layer. Same synthesis backend; different “document” and highlight model.

### Protocol (summary)

Transport: Unix domain socket (or TCP `127.0.0.1` with the same framing).

```
[4 bytes BE uint32 length N][N bytes UTF-8 JSON]
```

Client → server (examples): `speak` `{id, text}`, `stop` / `stop_all`,
`set_voice`, `set_speed`, `list_voices`, `shutdown`.

Server → client: `ready` (voices, `audio` bool, `warning`), `audio` (base64 WAV
per sentence id), `error`, `voices`, `ack`.

Treat `audio: false` or a non-empty `warning` as **not ready for listening** —
do not play silent WAVs as success.

Speed is Piper **`length_scale` at synthesis time**, not `QMediaPlayer` rate
(avoids pitch distortion). Voice changes must go through the playback
controller so in-flight queues are cancelled.

Normative detail: text2sprech `PROTOCOL.md` and `AGENTS.md` (speech section).

---

## What biltoo already has

| Piece | Location / note |
|-------|------------------|
| Page text layer | `ThumtooCache::PageTextLayer`, `TextLayerController` |
| Region paint / hover / selection | `TextLayerSession`, panel sync |
| Multi-page selection bag | `TextSelection` / `TextSelRef` (`SessionImageId` + region + text snapshot) |
| Speakable string today | `TextLayerController::selectedText()`, `copySelectedText()` |
| Text panel | Region list; natural place for Speak / Stop chrome |
| Qt Multimedia | **Not** linked yet (text2sprech uses Widgets + Multimedia) |

### Speakable source priority

1. Non-empty **selection** — `selectedText()` / multi-page bag joined text.
2. Else **current page** — region texts in reading order (`sortReadingOrder`).
3. Else disabled + status: no text / run OCR / show text layer.

---

## Target architecture

```
┌────────────────────────────────────────────────────────────┐
│ biltoo UI                                                   │
│  Text panel / menu: Speak selection · Speak page · Stop    │
│  Optional: highlight region(s) while a sentence plays       │
└─────────────────────────────┬──────────────────────────────┘
                              │ plain QString
                              │ (+ optional region ↔ offset map)
┌─────────────────────────────▼──────────────────────────────┐
│ SpeechSession (biltoo-owned, thin)                          │
│  · Build speakable string from TextLayer / TextSelection    │
│  · Map sentence spans → region indices (best-effort)        │
│  · Drive highlight without necessarily clearing user sel.   │
└─────────────────────────────┬──────────────────────────────┘
                              │ sentences[]
┌─────────────────────────────▼──────────────────────────────┐
│ Shared / vendored (from text2sprech lineage)                 │
│  SentenceSplitter · PiperClient · PiperServerManager         │
│  PlaybackController (queue + WAV decode/play)                │
└─────────────────────────────┬──────────────────────────────┘
                              │ PROTOCOL (UDS)
┌─────────────────────────────▼──────────────────────────────┐
│ piper_server (text2sprech tree or packaged binary)           │
└────────────────────────────────────────────────────────────┘
```

**Do not** embed Piper in-process. Keep the server boundary for crash isolation,
shared voice models with text2sprech, and existing Nix packaging patterns.

---

## Shared library vs reuse

### Good candidates to share

- Wire protocol + `piper_server`
- `PiperClient`, `PiperServerManager`
- `SentenceSplitter` (and tests; Python remains the behavioural reference)
- Playback queue + WAV playback helpers

### Stay app-specific

- text2sprech `DocumentView` / HTML sanitizer / presentation mode
- biltoo `SpeechSession`, region mapping, Text panel chrome
- Each app’s Preferences layout

### Extraction strategy

1. **First:** vendor or path-depend the speech client + splitter + server into
   biltoo (or a small sibling repo) **without** a perfect library cut.
2. **After** biltoo Speak selection works end-to-end: promote to a shared
   package (e.g. `libtext2sprech-speech` / `piper-protocol`) with PROTOCOL as
   the public contract.

Avoid a big-bang monorepo refactor before Phase A ships.

---

## Fit to biltoo text infrastructure

| Concern | Approach |
|---------|----------|
| Input | Selection first; else current page regions in reading order |
| Multi-page selection | “Speak selection” uses bag snapshots; “Speak page” = current `SessionImageId` only |
| Highlight while reading | Map each sentence to region indices via containment/overlap of region text in the flat string; prefer a **speech highlight** style so multi-page user selection is not destroyed |
| Page change mid-speech | Stop or finish the current sentence; no auto page-follow in v1 |
| Empty layer | Disable Speak; point user at OCR / show regions |
| Non-speakable segments | Drop punctuation/symbol-only chunks (mirror text2sprech `hasSpeakableContent`) |

The splitter operates on a **flat UTF-16 string**. Build it once and keep a side
table `regionIndex → [start, end)` into that string for highlight mapping.

Related: [TEXT_OVERLAY.md](TEXT_OVERLAY.md), `src/text/textselection.h`,
`TextLayerGeometry::sortReadingOrder`.

---

## UI (v1)

- **Menu / Text panel / context:** Speak (selection or page), Stop.
- **Shortcuts (proposal):** e.g. Speak `Ctrl+Shift+S`, Stop `Ctrl+.` — avoid
  stealing slideshow **Space**.
- **Status bar:** “Speaking…”, “No Piper voices”, server `warning` text.
- Voice/speed: Preferences later; hard-code defaults from server `ready` for A.

No full text2sprech player bar in v1.

---

## Packaging and dependencies

- biltoo: **Qt6 Multimedia** (or Qt6 audio APIs) for WAV playback.
- Ship or document **piper_server** + model directories (same conventions as
  text2sprech: e.g. `~/.local/share/piper/voices`,
  `~/.local/share/text2sprech/voices/`, env overrides).
- Nix: optional flake output that pulls `piper-server` and a default voice
  (mirror `text2sprech-full`).

---

## Implementation phases

### Phase A — Speak selection (vertical slice)

- Connect to piper_server (spawn via manager or external `--piper-socket`).
- `SentenceSplitter` → queue → play WAV.
- Actions: Speak selection / Speak page, Stop.
- No live region highlight yet.

### Phase B — Highlight + panel

- Map sentences → regions; highlight current sentence on the page.
- Text panel reflects speaking state.

### Phase C — Shared packaging

- Extract client / server / splitter to a tree both projects consume.
- Align flake voice paths and docs.

### Phase D — Later

- Voice/speed in Preferences.
- “Read from search hit”.
- Export WAV (text2sprech export patterns).
- Continuous multi-page read with optional auto page advance.

---

## Risks and principles

1. **Do not fork the protocol** — change PROTOCOL only together with text2sprech.
2. **Keep C++ and Python splitters aligned** — Python is the reference in
   text2sprech; port carefully.
3. **GUI thread** — synthesis stays on the server; biltoo only queues and plays.
4. **Selection vs speech highlight** — separate visuals when possible.
5. **Honest failures** — respect `audio: false` / `warning`; surface them in UI.

---

## References

- Upstream app: <https://github.com/Grumbel/text2sprech>
- Upstream files: `PROTOCOL.md`, `piper_server/`, `src/speech/`,
  `src/document/SentenceSplitter.*`, `AGENTS.md` (speech / protocol notes)
- biltoo text: [TEXT_OVERLAY.md](TEXT_OVERLAY.md), `src/text/textlayercontroller.*`,
  `src/text/textselection.h`
