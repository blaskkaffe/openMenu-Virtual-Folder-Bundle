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
`FOLDERS_7` ship, 8 and 9 are these), so install one set or the other. `out_lowres/` is a third set (`WebOrangeLow` / `WebBlueLow`, `backdrop=2` in THEME.INI) whose wave is 16 x 16 cells instead of 24 x 22: about half the triangles (about 510 instead of 1,060 for the plane) in case the full wave is too heavy. `out_animated/*_preview.png` is one frame of the animation.

### The animated backdrop and glass panels

The animated set draws the **same background as the DreamPi web page's Dreamcast background module**: the Dreamcast-BIOS-style sky and rippling
water, ported from its Three.js scene (`dc-background.js`, adapted from Robert Dale Smith's VMU Icon Maker, MIT).

- **Needs the rebuilt openMenu** (this branch). It adds the theme keys `backdrop`, `backdrop_clouds` (the cloud cylinder's opacity in percent of the web page's own, which is very faint: 100 = the page, 250 = what these themes use), `backdrop_color` (an optional tint; leave it out for the web
  page's own colours), `panel_0` to `panel_5`, `panel_border_color`, `panel_fill_color`, `panel_alpha`, `panel_radius`, `panel_border_width`
  and `menu_corner_radius`. An older build ignores them and would show the see-through picture on black, so use the still set there.
- **The scene, as the web page has it.** The same camera (at 0, -20, 7, a 75 degree field of view, looking along +y, with the page's 720 px high
  canvas centred on the 480 px screen), the same sky gradient, the same 64x64 cloud texture, a 50 x 30 plane whose height is four ripples
  going out from the middle and which fades out with the distance, and the big cylinder behind it that spins slowly and fades out downward.
  The lighting is the page's: ambient plus two directional lights on a flat plane (the page never recomputes the plane's normals, so the
  ripples show through perspective and the texture, not shading). Time runs at the page's rate, 0.016 a frame.
- **Compared with the page.** `render_backdrop_frame.py` builds the scene code from `draw_kos.c` on your computer, draws its triangles with a
  small software rasteriser and writes `backdrop_frame.png` (the theme previews use it). Against a Chromium screenshot of the page's scene at
  640x480 the mean difference was about 1.4 levels out of 255 (the rasteriser is not the PVR, so it is a check of the geometry, texture mapping,
  colours and timing, not of the console's output).
- **Why the scene is in the opaque list (the glitch fix).** The first version drew the scene in the translucent pass, where all the text is. The PVR
  keeps a short list of polygons for each 32 x 32 tile and a tile with text in it is already about two thirds full (every character is its own
  polygon), so a scene drawn there overflowed the lists: polygons were dropped, which showed as missing borders and panels, text that vanished,
  and square holes in pictures. It was not the depth buffer (the scene's depth values are tiny, so everything else sits in front of it as it
  should). Now the scene is opaque and goes in the opaque list, which nothing else uses: the page's fade to transparent is done with the
  vertex colours (the picture times opacity and light, plus the sky behind it times one minus the opacity, as the offset colour), which adds up
  to the same blend. The meshes are also kept near the screen, because the PVR draws polygons with huge coordinates wrongly: the plane's rows
  are spaced evenly on the screen and its width follows the screen's at that row, and only the front half of the cylinder and the rows that
  reach the screen are drawn. At most 12 polygon entries land in any tile (average 3.5), against the 32 a list holds.
- **Less in the translucent pass too.** The background picture is drawn as three pieces (logo, Controls, disc) instead of the whole screen, and
  the panels have no shadow, which saves an entry in most tiles.
- **What is cut down from the page.** Its plane has 101 x 101 points; this one has 25 x 23 (rows spaced evenly on the screen, columns crowded
  toward the middle, where the ripples are), and the cylinder the visible 16 x 8. A frame is about 1400 vertices. The page's specular highlight
  is folded into the plane's brightness. Nothing else is different.
- **Memory.** The 64x64 texture is 8 KB of video memory, allocated once the first time the backdrop draws (`backdrop_texture.h` holds it, RGB565
  and twiddled, made by `make_backdrop_texture.py`). If that allocation fails the sky gradient still draws. About 20 KB of static arrays. The
  theme's background picture is ARGB4444 instead of RGB565, both 16 bits per pixel, so its two textures are the same size as before (640 KB).
- **The panels.** The rounded boxes are drawn by openMenu (`draw_draw_panel()`): a soft shadow, a fill like the web page's boxes (rgba 20,20,20
  at .78; a little lighter at the top) and a 3 px accent border, with real arcs for the corners. The rectangles are `panel_0` to `panel_3` in
  `THEME.INI`. The same arc code draws the rounded popups (`menu_corner_radius`).
- **The picture.** `BG_L.PVR` / `BG_R.PVR` hold only the logo (with a soft shadow, because the white lettering sits on bright sky), the Controls
  text and an empty disc; the rest is see-through.
- **Compatibility.** The still set (`out/`) works on a stock openMenu: it is a plain RGB565 picture and standard theme keys (`menu_corner_radius` and
  `online_color` are ignored there, so popups are square and colours are the standard ones). The animated set needs this build: a stock openMenu
  draws the background picture in the opaque pass, where its see-through parts come out black, and has no backdrop or panels.
- **Not run on a Dreamcast beyond the first test.** If it is slow, lower `PLANE_COLS` / `PLANE_ROWS` or `CYL_SEGMENTS` / `CYL_ROWS` in `draw_kos.c`. If the scene does
  not show at all, tell me what you see (black, the plain gradient, no animation), because each points to a different cause: the gradient
  without the scene means the texture allocation failed.
- **Dither.** Over a gradient the console's dithering shows as fine noise, which is how a 16-bit picture shows smooth shading. The still set has
  no such pattern.
- **Previews.** `out_animated/*_preview.png` are one frame of that rendering with the panels and the picture over it.

## Event reminders

When the DreamPi's answer carries a reminder for a DC99 event (`EVN <minutes> <flag> <source> <title>` with the flag at 1), the Folders list shows a small box
between the logo and the clock for 20 seconds: the source and the time ("Sega Discord: in 12 min") and the title. It comes up again at 5 minutes, 1 minute and the start.
It uses the theme's popup colours and corners.

## Games being played online

With the DreamPi add-on connected, openMenu takes the games from the Pi's own `PLY` line (the slots of the card's games that someone plays
online now, with the player count), so the marks work without fetching the player list and with Auto-Refresh off. Without the add-on it falls back to what is below.

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
