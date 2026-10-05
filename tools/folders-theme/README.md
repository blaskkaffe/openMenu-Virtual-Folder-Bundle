# Folders themes in the DreamPi web page's style

Two openMenu **Folders** themes with the web page's look: a dark page, rounded boxes with a coloured border and the page's palette.
The colours are the web page's network colours, so a theme can follow the network the DreamPi has selected.

| Folder | Name | Colour | Web page |
|---|---|---|---|
| `out/FOLDERS_8` | WebOrange | orange `#e8761c` / border `#f6b27a` | DCNow! |
| `out/FOLDERS_9` | WebBlue | blue `#1c6fe8` / border `#80b1f6` | DCNET |

Each folder has `THEME.INI`, `BG_L.PNG` / `BG_R.PNG` and `BG_L.PVR` / `BG_R.PVR`. `out/*_preview.png` shows the empty background.

## Install

Copy `FOLDERS_8` and `FOLDERS_9` into `tools\\openMenu\\menu_data\\theme` (the folder GD MENU Card Manager copies to the card), open
Card Manager with the card in and click **Save Changes**. In openMenu's Settings, set **Style** to `Folders`, then pick
**WebOrange** or **WebBlue** under **Theme**. (Slots 8 and 9 were free; the default ones use `FOLDERS` and `FOLDERS_0` to `FOLDERS_7`.)

## Rebuild

`python3 build_theme.py` (needs Pillow and numpy) rewrites `out/`. The colours, box positions and the `THEME.INI` values are at the top
of the script. The logo and the button legend are cut out of the default Folders background in the Card Manager's theme folder.
`pvr.py` writes the Dreamcast texture files; it reproduces the default theme's `BG_L.PVR` and `BG_R.PVR` byte for byte from their PNGs.

## Notes

- **Rounded popups need the rebuilt openMenu.** The boxes in the background are pictures, so they are rounded in any build. openMenu's
  popups (Settings, Credits, Disc Options ...) are drawn in code: this branch adds a `menu_corner_radius` theme setting (default 0 =
  square, so every other theme looks as before). These themes set it to 10. An openMenu without the change ignores the key and shows
  square popups.
- **No dither pattern.** The Dreamcast's 16-bit picture (RGB565) dithers any colour it cannot show exactly, which shows as a pattern on a
  flat area. A texture value is widened to 8 bits exactly only for dark colours (red and blue of 0, 8, 16 or 24, green a multiple of 4 up
  to 60), so the page and the box fills use those (`PAGE`, `CARD`, `tint` in `build_theme.py`) and every colour in `THEME.INI` is
  snapped to a value the console draws exactly. The thin bright borders and anti-aliased corners still carry other values.
  This is my explanation of the pattern you saw; I have not seen it on hardware, so tell me if any remains.
- The controls box is drawn like one of the web page's info boxes (a label, rows divided by thin lines, the page's font), with the
  button icons redrawn; it no longer uses the default theme's pixel-font legend. The logo swirl is recoloured by brightness, so it has no
  rim of the old orange.
- The list, artwork and details positions in `THEME.INI` are moved to sit inside the rounded boxes (17 rows, a 202 px cover).
- Not looked at on a real Dreamcast or a TV yet: the previews are the picture only (no list text or cover art on top).
