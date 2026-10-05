# Folders themes in the DreamPi web page's style

Two openMenu **Folders** themes with the web page's look: a dark page, rounded boxes with a coloured border and the page's palette.
The colours are the web page's network colours, so a theme can follow the network the DreamPi has selected.

| Folder | Name | Colour | Web page |
|---|---|---|---|
| `out/FOLDERS_8` | WebOrange | orange `#e8761c` / border `#f6b27a` | DCNow! |
| `out/FOLDERS_9` | WebBlue | blue `#1c6fe8` / border `#80b1f6` | DCNET |

Each folder has `THEME.INI`, `BG_L.PNG` / `BG_R.PNG` and `BG_L.PVR` / `BG_R.PVR`. `out/*_preview.png` shows the empty background.

**Two sets, same slots.** `out/` is the still version. `out_animated/` is the same two themes (named `WebOrangeAnim` / `WebBlueAnim`) with an
animated backdrop: slow silk-like waves in the theme colour behind the boxes. All ten Folders slots are taken (`FOLDERS`, `FOLDERS_0` to
`FOLDERS_7` ship, 8 and 9 are these), so install one set or the other. `out_animated/*_preview.png` is one frame of the animation.

### The animated backdrop

- **Needs the rebuilt openMenu** (this branch): it adds the `backdrop=1` and `backdrop_color=r,g,b` theme keys. A build without it ignores them and
  would show the see-through picture on black, so use the still set there.
- **How it is drawn.** `draw_backdrop()` in `draw_kos.c` draws a 16 x 12 grid of Gouraud-shaded strips in the opaque pass (about 410 vertices a
  frame), shaded from the slope of two travelling sine waves, lit from the upper left. The theme picture is then drawn over it in the
  translucent pass: the page and the gaps are see-through, the boxes 80 % opaque, like the web page's boxes over its background.
- **Memory.** No texture and no video memory for the backdrop: it is vertex colours only. The picture is ARGB4444 instead of RGB565, both 16 bits per
  pixel, so the two background textures are the same size as before (512x512 and 128x512, 640 KB together). The CPU side is about 3 KB of
  static arrays and roughly 220 sine/cosine pairs a frame; the PVR has about 400 more vertices to bin (the vertex buffer is 256 KB).
- **Compared with the web page's version** (a Three.js wave plane with the Dreamcast BIOS texture and a cylinder): this is the wave look
  only, procedural and in one colour, since a textured 3D scene would cost video memory and frame time.
- **Dither.** Over a moving gradient the console's dithering shows as fine noise, which is how a 16-bit picture shows a smooth gradient. The
  see-through boxes are blended over it, so they carry that noise too; the still set has no such pattern. If it bothers you, make the box
  alpha 255 in `build_theme.py` (`a = 204`) and rebuild.
- **Not run on a Dreamcast.** I tested the arithmetic natively (about 410 vertices a frame) and the texture files by decoding them back, but I
  have not built openMenu with this, so the frame rate and the look in motion are unknown. If it is slow, lower `BACKDROP_COLS` / `BACKDROP_ROWS`
  in `draw_kos.c`.


## Install

Copy `FOLDERS_8` and `FOLDERS_9` into `tools\\openMenu\\menu_data\\theme` (the folder GD MENU Card Manager copies to the card), open
Card Manager with the card in and click **Save Changes**. In openMenu's Settings, set **Style** to `Folders`, then pick
**WebOrange** or **WebBlue** under **Theme**. (Slots 8 and 9 were free; the default ones use `FOLDERS` and `FOLDERS_0` to `FOLDERS_7`.)

## Rebuild

`python3 build_theme.py` (needs Pillow and numpy) rewrites `out/` and `out_animated/`. The colours, box positions and the `THEME.INI` values are at the top
of the script. The logo and the button legend are cut out of the default Folders background in the Card Manager's theme folder.
`pvr.py` writes the Dreamcast texture files; it reproduces the default theme's `BG_L.PVR` and `BG_R.PVR` byte for byte from their PNGs.

## Games being played online

With the rebuilt openMenu, a game in the Folders list that somebody is playing online right now is drawn in the theme's `online_color` (these
themes use the web page's bright green, `72,216,96`; without the key openMenu uses a built-in green) and has a small telephone after its title
(when the title leaves room). The selected row keeps its highlight colour and gets the telephone. It works from the player list of the DC Now!
window, matched to the card by title: the same name or one inside the other once case, punctuation and bracketed parts like "(USA)" are
ignored. With **DC Now! Auto-Refresh** on, openMenu keeps that list fresh in the background while connected, so you do not need to open the
window; with it Off, the list is only as fresh as your last manual refresh. The marks go away when the connection or DC Now! does.

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
