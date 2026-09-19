# Canvas background model

Three layers, one material vocabulary.

## Layers

| Layer | Scope | UI |
|--------|--------|-----|
| **Preferences default** | App-wide solid / checker | Preferences → Background |
| **View background** | Session only (Gallery + Image) | View → View Background… / toolbar |
| **Workspace background** | Project | Workspace → Background… |

Slideshow **letterbox fill** (Solid / App background / ZoomBlur) is presentation
chrome for the slideshow, not a fourth canvas layer. It reuses the ZoomBlur
pipeline shared with View **Content blur**.

## Materials (`WorkspaceBackgroundMode`)

- **AppDefault / Preferences default** — follow Preferences
- **Solid** — one colour
- **Checkerboard** — technical grid
- **ImageTile** — repeating pattern image
- **ContentBlur** — cover-scaled blur of the current image (View / Image only;
  omitted from the Workspace project dialog; Gallery falls back to solid)

## Paint order

1. Workspace mode + project override → workspace materials  
2. Else Gallery/Image + session view override → view materials (including ContentBlur)  
3. Else Preferences solid / checker (`checkerWorkspaceOnly` still scopes checker)

Content blur is painted in viewport device space under the sharp `ImageItem`.
