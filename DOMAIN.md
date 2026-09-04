# Biltoo domain model

A language-level description of the program: entities, modes, and operations.
Independent of Qt, widgets, and file I/O. Implementation must serve this model;
contradictions here are logic faults, not UI noise.

## Domain

**Session**  
An ordered list of **session images**. This is the user’s working set.
Each session image has an index and names a file on disk (path). The same
path may appear more than once; those entries are independent images.

**Session image**  
One entry in the session: decode source (path) plus user-applied **content**
appearance (crop, flips, content 90° turns). Identity is the session index,
not the path string.

**Image (decoded)**  
Pixels derived from a session image’s path and content appearance. Does not
know about view zoom; presentation does.

**Canvas**  
What is shown in the main area. The canvas always has a **mode**. What may sit
on the canvas, and what the user may do to it, is entirely determined by mode.

**Selection (session)**  
Which session indices are current for navigation and for the thumbnail strip.
In Image mode this is a single index. In Workspace it may be several paths that
are also on the canvas.

**Selection (canvas)**  
Which canvas objects are the target of transform operations. Only meaningful in
Workspace (and weakly in Gallery for “open this one”).

## Modes (mutually exclusive)

The program is always in exactly one mode. There is a single source of truth for
mode (`ImageView::ViewMode`). Shell UI must not keep a parallel boolean that can
diverge from it.

### Image mode — “look at one picture”

- Canvas holds **at most one** image: the session’s **current session image**
  (cursor index). Editing here changes that session image’s content appearance.
- The **view** frames that image (fit, fill, 1:1, zoom, pan).
- The **image** may still carry rotation and flips; framing must not erase them
  unless the user asks to reset.
- User may step previous / next / first / last through the session.
- User may run a slideshow (timed next).
- User may open edges or keys to go previous / next.
- User may not place a second image on the canvas.
- User may not drag the image as a free object (pan moves the view).

### Gallery mode — “see the whole session laid out”

- Canvas holds **one object per session image**, arranged by a **layout**
  (horizontal, vertical, grid, masonry columns/rows, masonry fill variants).
- Layout is pure arrangement: positions and cell scales. Objects are not
  free-moved by the user.
- **Grid Crop** (square cells with cover-crop) is temporarily **disabled** in the
  UI; it conflicts with session/manual crop. Code paths remain for compatibility.
- **Masonry Fill** / **Masonry Rows Fill**: after packing, scale each column or
  row so heights or widths match — outer shape is a clean rectangle (no dangling
  last row/column).
- User may change layout; the same set of paths is re-packed.
- User may activate one object → leave Gallery, enter **Image** on that path,
  remembering enough to **return** (layout, scroll, which cell was current).
- Arrow keys move the session cursor among tiles by **scene position** (spatial neighbour); Home/End first/last; Enter opens.
- Grid / Masonry column or row count is user-configurable (toolbar spin; 0 = automatic for grid).
- User may rotate (±90°) and flip selected tiles; scale/opacity/stack remain Workspace-only.
- Linear viewer navigation (slideshow, prev/next) is not the primary job here.

### Workspace mode — “arrange several pictures freely”

- Canvas holds **zero or more** images, each a free object: position, scale
  (uniform or non-uniform), rotation, flips, opacity, stacking order.
- Session still exists; the canvas may show a **subset** of session images.
  Placement on the Workspace is optional. Each canvas object is associated
  with exactly one session image (slot); Workspace adds free placement
  (position, scale, free tilt, opacity, stacking).
- User may select one or more objects and transform them.
- User may raise / lower stacking order.
- User may duplicate a selection: new session image (same path allowed) and
  a new canvas object with independent content appearance and placement.
- Leaving Workspace **keeps** object transforms (snapshot) for when the user returns.
- **Layout panel** (dock, toolbar toggle): apply a packaged arrangement
  (horizontal, vertical, grid, masonry, masonry fill, …) to the **current
  selection** only; does not switch to Gallery mode. Columns/rows + Apply.
- **Page Guide** menu actions live under **Workspace** and are disabled outside
  Workspace mode.
- Entering Workspace from Gallery **restores** the durable Workspace snapshot;
  Gallery packing is not adopted as free-form content.
- Slideshow and edge-next are off: this is not a linear viewer.

**Law:** Gallery is not a kind of Workspace. Workspace is not a kind of Gallery.

### Persistence and export (non-destructive)

- **Project file** (`.biltoo`): ordered session images (stable ids), content
  appearance (including session crop rect, **cropRotation**, content flips /
  quarter turns), optional Workspace free-form poses, asset list with SHA-256.
  Does not write into source image files.
- **Export PNG / PDF**: render canvas region (content bounds or page guide) to a
  new file. Page guide is optional framing for print/export, not the definition
  of Workspace.
- **Identity:** thumbnail canvas membership, duplicate tiles, and content edits
  key on `SessionImageId`, never path alone when duplicates exist.

Image is not a multi-object canvas.

## Shared operations (mode-filtered)

### Session

| Operation | Meaning | Allowed when |
|-----------|---------|--------------|
| Open / replace session | New ordered path list; current index set | Always |
| Append to session | Add paths; keep current if possible | Always |
| Remove from session | Drop paths; fix current index; Gallery Delete uses this (undoable) | Always |
| Remove from canvas only | Drop Workspace objects; session unchanged | Workspace Delete |
| Set current index | Point session at a path | Always |
| Sort session | Reorder paths | Always |

### Presentation (Image-oriented)

| Operation | Meaning | Allowed when |
|-----------|---------|--------------|
| Fit / Fill / 1:1 / Zoom ± | Change how the **view** frames the image | Image (primary); view pan/zoom may exist elsewhere |
| Pan view | Move the view, not object local position | Image; Workspace pan tool; Gallery scrollbars / wheel |
| Wheel zoom | Scale the view transform | Image and Workspace only — **not** Gallery |
| Previous / Next / First / Last | Change current index; show that path in Image | Image, session size > 1 |
| Slideshow start/stop | Timed Next (from Gallery, enters Image first) | Image or Gallery start; not Workspace |
| Fullscreen | Chrome on/off | Always |

### Object transforms

| Operation | Meaning | Allowed when |
|-----------|---------|--------------|
| Rotate ±90° / free rotate | Change object rotation | Image (single object); Gallery (selection, ±90°); Workspace (targets, free rotate) |
| Flip H/V | Mirror | Image; Gallery (selection); Workspace |
| Reset rotation / reset scale | Identity orientation or scale | Same |
| Move object | Change object position on canvas | **Workspace only** |
| Scale / stretch | Change object scale | **Workspace only** (Image uses view zoom) |
| Opacity | Transparency of object | **Workspace only** |
| Raise / Lower | Stacking order | **Workspace only** |
| Duplicate selection | New canvas objects, same paths, independent transforms | **Workspace only** |

### Mode changes

| Operation | Meaning | Rules |
|-----------|---------|--------|
| Enter Image on path P | Mode := Image; canvas shows P; session current := P | From Gallery/Workspace open or session navigation |
| Return to Gallery | Mode := Gallery; restore remembered layout/scroll/cell | Only if Gallery was left via “open image” |
| Return to Workspace | Mode := Workspace; restore free-object snapshot/stash | Only if Workspace was left via “open image” |
| Enter Gallery with layout L | Mode := Gallery; pack all session paths with L | From UI layout actions |
| Enter Workspace | Mode := Workspace; restore free objects if snapshotted | Explicit user choice or return-from-Image |
| Leave Workspace | Snapshot free objects; then Image or Gallery | Snapshot must not be silently discarded |

## Transform targets

| Mode | Targets of rotate / flip / reset |
|------|----------------------------------|
| Image | The single canvas image (primary) |
| Workspace | Canvas selection; if empty and only one object, that object |
| Gallery | Selected tiles (±90° rotate / flip; pack preserves transforms) |

## Invariants

1. **One mode at a time.** Shell state must not claim Workspace while the canvas is Gallery (or Image).
2. **Session list is the only ordered set of paths.** Canvas never invents a competing session order.
3. **In Image mode, canvas cardinality ≤ 1** and that object’s path equals session current (when a current exists).
4. **In Gallery mode, canvas paths = session paths** (one each); positions come only from the active layout.
5. **In Workspace mode, transform chrome and free move exist; in other modes they do not.**
6. **View framing must not erase user orientation** (rotation/flips) unless the operation is explicitly “reset” or “load fresh image.”
7. **Slideshow ⇒ Image mode and session size > 1.** Otherwise slideshow is idle.
8. **Gallery → Image → Return** restores Gallery state; it does not open Workspace.
9. **Workspace → Image → Return** restores the Workspace free-object stash/snapshot; it does not open Gallery.
10. **Targets of transform** follow the table above.

## Pseudo-API (behavioural)

```text
Session: paths[], currentIndex

Mode = Image | Gallery | Workspace

Canvas:
  Image     → { single?: ImageObject }
  Gallery   → { objects: ImageObject[]; layout: Layout }
  Workspace → { objects: FreeImageObject[]; selection: id[] }

enter Image(path):
  require path in Session
  Mode := Image
  Session.current := path
  Canvas := { single: load(path) }

enter Gallery(layout):
  Mode := Gallery
  Canvas := pack(Session.paths, layout)

openFromGallery(object):
  remember GalleryState
  enter Image(object.path)

returnToGallery():
  require remembered GalleryState
  enter Gallery(restored)

enter Workspace():
  Mode := Workspace
  Canvas := restore(WorkspaceSnapshot) if present, else empty
  // Do not adopt Gallery packing or session list as free-form content.
  // Seeding is explicit (drop, thumbnail membership, project load,
  // Layout panel Apply on selection).

leave Workspace():
  WorkspaceSnapshot := Canvas.objects
  // Snapshot must survive Gallery; only explicit clear / new project wipes it.

rotate / flip / resetOrientation:
  require Mode in {Image, Workspace}
  apply to transformTargets(Mode)

move / scale / opacity / raise / lower / duplicate:
  require Mode = Workspace
  apply to Canvas.selection

layoutWorkspaceSelection(packagedLayout, params):
  require Mode = Workspace and non-empty canvas selection
  // Packaged algorithms (grid, masonry, masonry-fill, …) on selection only.
  // Masonry Fill / Rows Fill equalize column heights or row widths (rectangle).
  // Does not enter Gallery. Undoable.

crop draft (Image or single Workspace target):
  default: draft clamped to image bounds
  Expand on: draft may extend outside; Apply pads with view background
  Move = centre grip only; frame interior starts a new rubber-band
  Workspace: enter/apply keeps crop region fixed on canvas (item pose shifts around it);
  placement rotation follows the crop-frame angle on Apply (cancel restores prior pose)
  Ctrl = resize from centre; Shift = square (resize)
  Rotate knob = free angle about centre; Ctrl → 45° snap; Shift → 15° snap; Close bakes axis-aligned pixels
  Resize under rotation uses crop-local axes; centre mapped back through θ
  Apply commits; Cancel / Esc discards; toolbar crop-off commits (same as Apply)
  cropRotation + cropSourceSize in session appearance and project file
  captureState / commit / undo-redo must preserve cropRotation

next / previous / slideshow:
  require Mode = Image and Session.size > 1
```

## Ownership contracts (framing vs object)

| Mode | Position | Scale | Rotation / flip | View matrix |
|------|----------|-------|-----------------|-------------|
| Image | Fixed origin | Object scale typically 1; **view** zooms | Object owns | View owns fit/zoom/pan |
| Gallery | Layout (incl. Masonry Fill) | Layout (cell) | Neutral (layout may ignore user orient) | View scroll |
| Workspace | Object | Object (possibly non-uniform) | Object | View pan/zoom of whole scene |

## Thumbnail strip

- Reflects **Session**, not a second file list.
- In Workspace, multi-select on the strip drives which paths are requested on the canvas.
- That multi-select behaviour is “session selection for Workspace,” not a fourth mode.



## Shell session API (MainWindow)

Named transitions (prefer these over ad-hoc mode toggles):

| Method | DOMAIN operation |
|--------|------------------|
| `enterGalleryMode(layout)` | Mode := Gallery; pack session with layout |
| `showPathInImageMode(path)` | Mode := Image; session current := path; optional gallery-return snapshot |
| `returnToGallery()` | Restore Gallery from snapshot after Image |
| `enterWorkspaceMode()` | Mode := Workspace |
| `setCurrentIndex(i)` | Session cursor; **only Image** reloads the single-image canvas |
| `duplicateSelected()` | Workspace: clone selection (same path, independent transforms) |

`ImageView::focusSessionPath(path)` selects the canvas object for a session path
(and ensures visibility in Gallery). Public for the shell.

## Thumbnail strip vs mode

`ThumbnailBar::setMultiSelectEnabled(true)` means **multi-select of session paths
for canvas membership**, not a fourth app mode. (Deprecated alias:
`setWorkspaceMode`.) App mode remains solely `ImageView::ViewMode`.
`MainWindow::syncCanvasFromThumbnailSelection` applies that selection to the
canvas (Workspace paths or Gallery seed).

## Chrome / handles (Workspace)

- Geometry lives under the item transform (scale, rotation, position).
- Interaction chrome is painted in **device pixels** and hit-tested primarily
  by **ImageView** (view-owned path). Item mouse handlers are a fallback that
  call the same `beginHandleInteraction` API.
