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

### The animated backdrop and glass panels

- **Needs the rebuilt openMenu** (this branch). It adds the theme keys `backdrop`, `backdrop_color`, `panel_0` to `panel_5`, `panel_border_color`,
  `panel_fill_color`, `panel_alpha`, `panel_radius`, `panel_border_width` and `menu_corner_radius`. An older build ignores them and would show the
  see-through picture on black, so use the still set there.
- **The backdrop.** `draw_backdrop()` in `draw_kos.c` draws a rolling, glowing surface in the theme colour behind the whole menu: three travelling
  sine waves make a height field that is lit from the upper left (a soft light plus a narrow shine on the ridges), and each grid point is pushed
  from or toward the screen centre by its height, so the surface swells and sinks like a 3D surface seen from above. It is a 24 x 18 grid of
  Gouraud strips in the opaque list (about 900 vertices a frame), with no texture and no video memory. The waves are 300 to 500 px long and move
  about two seconds a cycle: they have to be long, because the grid has a point every 32 px and shorter waves alias into streaks.
- **The panels.** The rounded boxes of the animated themes are drawn by openMenu, not baked into the picture: `draw_draw_panel()` draws a soft
  shadow, a fill that is a little lighter and more opaque at the top (alpha 170 at the top, a third less at the bottom), and a 3 px border, all
  real polygons with arcs for the corners, so the backdrop shows through the glass and the corners are true curves. The panel rectangles are
  `panel_0` to `panel_3` in `THEME.INI`, so you can move them. The same arc code draws the rounded popups (`menu_corner_radius`).
- **The picture.** `BG_L.PVR` / `BG_R.PVR` are ARGB4444 with only the logo, the Controls text and an empty disc; the page is see-through.
  Both are 16 bits per pixel, like the still set, so the two textures are the same size as before (640 KB).
- **Memory and cost.** No video memory for the backdrop or the panels. About 4 KB of static arrays; about 700 sine and cosine calls and 900
  vertices per frame for the backdrop, plus about 90 vertices per panel and per popup. The PVR vertex buffer is 256 KB; if a busy frame ever
  overflows it, lower `BACKDROP_COLS` and `BACKDROP_ROWS` in `draw_kos.c` (16 x 12 is about 410 vertices).
- **Why the first version showed no animation.** It drew the backdrop only in the margins: the boxes were baked into the picture at 80 % opacity
  and covered most of the screen, and the waves were slow and faint. Now the panels are much more transparent and the backdrop is livelier. I
  could not run it on a Dreamcast, so if you still see no movement tell me exactly where (margins only? nowhere? black?), because that points
  to a different cause.
- **Compared with the web page's version** (a Three.js wave plane with the Dreamcast BIOS texture and a cylinder): one colour and no texture, to
  stay light on memory and frame time.
- **Dither.** Over a moving gradient the console's dithering shows as fine noise, which is how a 16-bit picture shows a smooth gradient. The still
  set has no such pattern.
- **Previews.** `out_animated/*_preview.png` is one frame, drawn in Python with the same formulas (without the swell of the grid) and the panel
  look; it is an approximation of what the console shows.

## Games being played online

With the rebuilt openMenu, a game in the Folders list that somebody is playing online right now is drawn in the theme's `online_color` (these
themes use the web page's bright green, `72,216,96`; without the key openMenu uses a built-in green) and has a small telephone after its title
(when the title leaves room). The selected row keeps its highlight colour and gets the telephone. It works from the player list of the DC Now!
window, matched to the card by title: the same name or one inside the other once case, punctuation and bracketed parts like "(USA)" are
ignored. With **DC Now! Auto-Refresh** on, openMenu keeps that list fresh in the background while connected, so you do not need to open the
window; with it Off, the list is only as fresh as your last manual refresh. The marks go away when the connection or DC Now! does.

## Notes

- **Rounded popups need the rebuilt openMenu.** The boxes of the still set are pictures, so they are rounded in any build. openMenu's popups
  (Settings, Credits, Disc Options ...) are drawn in code: this branch adds a `menu_corner_radius` theme setting (default 0 = square, so every
  other theme looks as before). These themes set it to 10, drawn with arcs. An openMenu without the change ignores the key and shows square popups.
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
