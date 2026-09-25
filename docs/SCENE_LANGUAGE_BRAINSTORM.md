<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Scene language brainstorm — HyperCard, documents, Nelson

**Status:** brainstorm only. Not a roadmap, not an implementation plan.  
**Related:** [DOMAIN.md](../DOMAIN.md), [SESSION.md](../SESSION.md), [IDENTITY.md](../IDENTITY.md),
[CONTENT-VARIANT.md](../CONTENT-VARIANT.md), Workspace free-form poses / page guide / export.

---

## 1. Motivation

Biltoo today is a **session-based media workbench** with three mutually exclusive
**modes** (Image, Gallery, Workspace) on one canvas. The domain law is clear and
valuable (“Gallery is not Workspace”), but mode transitions are **implicit
application state**: Gallery → open tile → Image → return, with scroll/layout
memory, scrollbar policy, sceneRect, and stash/snapshot rules wired in C++.

A recurring class of bugs (return path, off-centre Gallery, restore races) is
exactly what you get when **navigation and presentation ownership are not data**.

**Wild idea:** drive presentation and click behaviour with a **scene description
language** (SDL). Gallery and Image stop being the only hard-coded “apps inside
the app.” Archive/directory/PDF/EPUB traversal **generates** scenes. Clicking a
thing runs declared actions (`go`, `return`, `open_media`, …), not a mode enum
folklore path.

This note captures the idea, how it meets documents that **already have links**,
what Workspace already contributes, and which **Ted Nelson** notions are worth
stealing — without proposing to rebuild biltoo as HyperCard.

---

## 2. HyperCard, in one page

HyperCard was roughly:

| Idea | Meaning |
|------|---------|
| **Stack** | Document: ordered cards + shared resources |
| **Card** | One screenful of UI and content |
| **Background** | Shared chrome/structure across cards |
| **Buttons / fields / paint** | First-class interactive and visual objects |
| **Script** | Behaviour is data (`on mouseUp go to card id 12`) |
| **Browse vs author** | Same surface; mode of *use*, not a different program |

What mattered was not the paint bucket. It was: **structure and behaviour are
authorable (or generable) data**, and **navigation is an explicit link**, not a
hidden state machine.

Biltoo’s edge remains thumtoo, session appearance (`SessionImageId`), and
non-destructive crop/orient — not general multimedia authoring. An SDL should
serve the media workbench, not replace it with a mediocre HyperTalk.

---

## 3. Mapping onto biltoo today

| HyperCard-ish | Biltoo today | SDL-shaped biltoo |
|---------------|--------------|-------------------|
| Stack | Session + optional `.biltoo` project | Stack document over media refs + instance ids |
| Card / scene | Implicit “current mode + canvas” | Named scene: `gallery`, `focus`, custom index |
| Button | Tile hit, edge affordance, toolbar | Declarative targets + closed action set |
| `go to card` | `setViewMode` + restore paths | `go scene …` with return stack / camera |
| Generated stack | Open dir → `m_files` | Open dir/archive/doc → emit scene graph |
| Authoring | Workspace free placement | Editable scene kind that writes back |

**Hardcoded modes need not die on day one.** They become **scene kind runtimes**:
the SDL selects *gallery-engine* vs *focus-engine* vs *workspace-engine*; C++
still owns pixels, LOD, and identity.

### 3.1 Tiny example (illustrative, not a proposed syntax)

```text
stack "vacation-2024"
  media:
    a = file("…/a.jpg")
    b = file("…/b.jpg")

  scene overview:
    kind: gallery
    layout: masonry
    items: [a, b, …]
    on activate(item):
      go focus(item) with return_to=overview, restore_camera

  scene focus(id):
    kind: image
    show media[id]
    on back: return
    on next / prev: go focus(neighbour)
```

Directory open becomes a **compiler** into this shape, not a special case in the
shell.

---

## 4. Workspace is already a partial scene language

Workspace is the closest thing biltoo already has to “cards as data”:

- **Free-form poses** — position, scale, rotation, opacity, z — persisted in
  project / snapshot when leaving Workspace.
- **Selection + transforms** — targets are canvas objects, not “the mode.”
- **Layout panel** — packaged packers applied to the *selection* (not a switch
  into Gallery). That is already “run a layout procedure over a set of nodes.”
- **Page guide** — framing rectangle for export/print; scene-level chrome, not
  session order.
- **Export PNG/PDF** — render a region of the composed scene.
- **Background** — project/session backdrop as scene environment.

So Workspace is not “mode three.” It is an **editable spatial scene** whose
state is largely declarative already (poses + guide + background). Gallery is a
**derived spatial scene** (poses computed from layout + session order). Image is
a **focus scene** (one underlay + view framing).

An SDL can treat these as **typed scene kinds** with different affordances
(packed vs free vs focus), preserving DOMAIN.md’s law instead of dissolving it
into one mushy canvas.

```text
kind gallery   → poses from packer; no free drag; activate → focus
kind image     → single media; view zoom/pan; content crop/flip
kind workspace → free poses; chrome; export region; no linear slideshow
```

---

## 5. Generators: directories, archives, and “compile to scenes”

Anything that can produce an ordered list of decodable surfaces can emit a stack:

| Source | Natural generation |
|--------|--------------------|
| Directory | One `overview` + one `focus` per file (or recursive chapter scenes) |
| Zip/tar/… | Same, paths as archive members (thumtoo TOC) |
| Session / `.biltoo` | Restore media list + optional saved scenes/poses |
| PDF | See §6 |
| EPUB | See §7 |

Generators should emit **media refs + instance ids + scenes + links**, not only
`QStringList files`. Session order remains the linear spine where needed
(slideshow, filmstrip); scenes may *also* expose hierarchical or cross links.

---

## 6. PDF — documents that already link

PDFs already carry structure biltoo partially flattens into “pages in a
session”:

| PDF feature | Today (typical viewer flattening) | SDL opportunity |
|-------------|-----------------------------------|-----------------|
| Page tree | Session images 1…N | `focus(page=i)` scenes; overview as contact sheet |
| Outline / bookmarks | TOC panel → jump page | Outline entries as `go focus(page)` or `go scene chapter_k` |
| Internal link annotations | Often “go to page” in host | First-class edges in the scene graph |
| Named destinations | Resolved ad hoc | Stable link targets in the stack |
| File attachments / embedded files | Separate open paths | Child media nodes or nested stacks |
| Page labels | Display only | Labels on scenes, not only indices |

**Interaction model:**

1. **Import compiler** reads outline + link annotations (via whatever backend
   already supplies page rasters — MuPDF path, etc.).
2. Emits:
   - media nodes per page (or per renderable surface),
   - a default `overview` (Facing / Flow are layout params on that scene),
   - **link table**: `{ from: page+rect or object id → to: page|dest|uri }`.
3. Runtime hit-test on focus scene: if click in link rect → `go` target;
   else existing pan/zoom/crop behaviour.

Gallery “open page” and PDF “follow link” become the **same action vocabulary**.
Facing layout is not a separate app mode; it is overview layout policy for a
stack whose media happens to be pages.

**Caveats:**

- PDF links are often **rect on page** in PDF user space; must map through
  biltoo content appearance (crop, orient) like any other content-space feature.
- External URIs are not `go scene`; they are `open_external` (policy decision).
- Huge outlines: generate outline as a **nav scene** (list card), not only as a
  dock panel — docks can *view* the same link table.

---

## 7. EPUB — spine, nav, and hypertext that is already text

EPUB is closer to the web and to classical hypertext than PDF:

| EPUB feature | Meaning | SDL opportunity |
|--------------|---------|-----------------|
| **Spine** | Default reading order | Linear `next`/`prev` chain of content documents / CSS pages |
| **Nav document** | TOC, landmarks, page list | Nav scene or outline edges → content scenes |
| **Internal `href`s** | Chapter → chapter, footnotes | Graph edges; footnote as overlay scene or side card |
| **Multiple content docs** | Not one giant page raster | Scenes per XHTML doc *or* per paginated surface |
| **Media overlays / a11y** | Parallel audio, semantics | Out of scope early; don’t block later parallel tracks |

Biltoo already leans on **rasterised pages** for a unified image pipeline
(thumtoo tiles, Gallery, Workspace). That conflicts slightly with EPUB’s native
**reflow + HTML links**. Two honest strategies:

**A. Raster-primary (near-term, fits biltoo)**  
Paginate or snapshot to page images; compile nav + in-page link rects (if
known) into the same link table as PDF. Text/find may still use text layer
where the host has it (EPUB find UI already exists in spirit).

**B. Hybrid (longer)**  
`kind: text_document` scene: HTML/viewport for reading and native `<a href>`,
with optional “place this page on Workspace as a raster snapshot.” Navigation
still goes through the stack’s `go` so Gallery/overview stay unified.

**Footnotes and back-links:** EPUB often has one-way `href`s. See Nelson (§9) —
even a **synthetic reverse edge** (“return from note”) improves the product
without full Xanadu.

---

## 8. Closed action vocabulary (not a general script engine)

Prefer a **small verb set** over HyperTalk:

| Action | Role |
|--------|------|
| `go scene` | Navigate; optional transition |
| `return` | Pop return stack (Gallery/Workspace return path) |
| `open_media` | Ensure focus on instance; may create focus scene |
| `run_slideshow` | Timed `go` along a declared sequence |
| `open_external` | URI / default app (policy-gated) |
| `apply_layout` | Workspace selection pack (already conceptual) |
| `export_region` | Workspace/page-guide export |

No user-defined loops or file I/O in v1. Behaviour stays auditable and
host-enforced (identity, decode, permissions).

---

## 9. Ted Nelson ideas worth applying

Nelson’s programme (hypertext, Xanadu, intertwingularity) is larger than
biltoo. A few ideas map cleanly without cosplay.

### 9.1 Links as first-class, not only hierarchy

PDF outlines and EPUB nav are **trees**. Media is often a **graph** (cross-refs,
“see also,” repeated figures). An SDL link table can store **arbitrary edges**
while still offering a linear spine for slideshow and filmstrip.

### 9.2 Two-way links (even if source data is one-way)

Xanadu stressed **bidirectional** links. PDF/EPUB usually give A→B only.
Runtime can maintain:

- forward edges from the file,
- **reverse index** B→{A} for “what points here?”

Useful for: footnote back, “pages that link to this figure,” debugging generated
stacks. Need not be a user-facing Xanadu UI on day one.

### 9.3 Transclusion (content by reference)

Nelson: the same content **appears** in many places without copying bytes.
Biltoo already almost does this:

- same **path** / content hash, multiple **SessionImageId** variants (crop A vs B);
- Gallery tile and Image focus and Workspace object all **reference** the same
  decode source;
- thumtoo cache is a shared pixel substrate.

An SDL should **name media once** and place **instance refs** on scenes
(transclusion of media into cards), not duplicate file entries per scene.
CONTENT-VARIANT’s content id vs variant id is the identity half of transclusion.

### 9.4 Parallel documents / visible connection

“Transpointing windows” — show source and target at once with a visible link.
Lightweight biltoo analogues:

- dual-pane or edge peek: focus page + linked page thumbnail;
- Workspace: two instances of the same page with different crops side by side
  (already possible as data; links could *propose* that placement).

### 9.5 Versioning and durable identity

Xanadu cared about long-lived references as documents change. Practical slice:

- stable **instance ids** (already: `SessionImageId`);
- stable **content ids** (thumtoo hash / CONTENT-VARIANT);
- links target **ids**, not “page 17 of whatever file was open,” so reorder and
  replace-session don’t silently retarget.

### 9.6 Intertwingularity (with restraint)

“Everything is deeply intertwingled” is a warning as much as a goal. Biltoo
should allow **rich links among media scenes** without making every UI surface
a general hypertext editor. Typed scene kinds keep Gallery/Image/Workspace
laws intact while the **link graph** spans them.

### 9.7 What not to import from Nelson (yet)

- Full micropayment / royalty systems  
- Universal worldwide doc store  
- Mandatory visible link lines on every navigation  
- Replacing the image pipeline with a general xanalogical media OS  

Steal **reference, reverse links, transclusion of media into scenes**; leave the
civilisation-scale ambitions alone.

---

## 10. Layer cake (if this ever left the napkin)

```text
┌──────────────────────────────────────────────────────────┐
│  Scene description (data)                                │
│  media refs, instance ids, scenes, links, actions        │
├──────────────────────────────────────────────────────────┤
│  Generators                                              │
│  dir | archive | pdf | epub | .biltoo  →  scene doc      │
├──────────────────────────────────────────────────────────┤
│  Host runtime (existing biltoo engines)                  │
│  SessionImageId, appearance, thumtoo, tile LOD, crop     │
│  gallery pack | image frame | workspace poses            │
└──────────────────────────────────────────────────────────┘
```

**Phased imagination (still not a commitment):**

1. **Navigation graph only** — express today’s Gallery↔Image↔return and
   slideshow as data; C++ runtimes unchanged.  
2. **Document link import** — PDF annotations + EPUB hrefs → same graph.  
3. **Workspace as editable scene** — poses/guide write into the document.  
4. **Optional authoring** — limited actions; still not HyperTalk.

Round-trip test for phase 1: one real folder session’s behaviour reconstructible
from a scene document alone (plus host pixel services).

---

## 11. Tensions and non-goals

| Tension | Note |
|---------|------|
| Domain law vs free hypertext | Keep typed scene kinds; don’t merge Gallery and Workspace |
| Raster pipeline vs reflow EPUB | Prefer explicit hybrid later; don’t pretend HTML is a JPEG |
| Generator richness vs hand-authored stacks | Generators first; authoring is Workspace-shaped |
| Script power vs safety | Closed verbs only for a long time |
| Product focus | Session media workbench first; HyperCard nostalgia second |

**Non-goals for this brainstorm:** shipping a scripting language, replacing
Qt widgets with a HyperCard clone, or blocking current Gallery/Image fixes on
SDL design.

---

## 12. Open questions

- Is the scene document **the** project format, or an optional layer beside
  `.biltoo` session + workspace snapshot?
- Do filmstrip and TOC panels become **views of the link/spine graph**, or stay
  independent shell chrome?
- Should slideshow be a **scene type**, a **mode of traversal** over a sequence,
  or a timed series of `go` actions?
- How do **duplicates** (two instances, one content id) appear in link targets —
  content vs instance?
- PDF link rects under **session crop** — content space mapping ownership?
- For EPUB, is “page” a host pagination artifact or a spine entry?

---

## 13. One-sentence summary

**Biltoo could treat overview, focus, and composition as typed scenes in a
generated, link-aware stack — compiling directories and archives, absorbing
PDF/EPUB links into one graph, formalising what Workspace already stores as
spatial data, and borrowing Nelson’s emphasis on reference, reverse links, and
transclusion — without becoming HyperCard or Xanadu.**

---

*End of brainstorm.*
