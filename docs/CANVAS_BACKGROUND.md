# Canvas background model

Three layers, one material vocabulary.

## Layers

| Layer | Scope | UI |
|--------|--------|-----|
| **Preferences default** | App-wide solid / checker | Preferences → Background |
| **View background** | Session only (Gallery + Image) | View → Background… / main toolbar |
| **Workspace background** | Project | Workspace → Background… / main toolbar (Workspace mode) |

The main toolbar **Background…** button is mode-aware: Gallery and Image open
the session View dialog; Workspace opens the project Workspace dialog. The
vertical Workspace tools bar no longer duplicates the Background action (it
still has the temporary **Background Default** toggle).

Slideshow **letterbox fill** (Solid / App background / ZoomBlur) is presentation
chrome for the slideshow, not a fourth canvas layer. It reuses the ZoomBlur
pipeline shared with View **Content blur**.

## Materials (`WorkspaceBackgroundMode`)

- **AppDefault / Preferences default** — follow Preferences
- **Solid** — one color
- **Checkerboard** — technical grid
- **ImageTile** — repeating pattern image
- **ContentBlur** — Image: cover-scaled blur of the current image; Gallery: the
  configured solid color (View session only; omitted from Workspace project dialog)

## Paint order

1. Workspace mode + project override → workspace materials  
2. Else Gallery/Image + session view override → view materials (including ContentBlur)  
3. Else Preferences solid / checker (`checkerWorkspaceOnly` still scopes checker)

Content blur is painted in viewport device space under the sharp `ImageItem`.
