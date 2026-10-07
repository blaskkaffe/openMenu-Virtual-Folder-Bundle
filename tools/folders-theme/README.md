# Animated Folders themes

This branch is openMenu Virtual Folder Bundle 1.7.0 with animated 3D backdrops for Folders themes, glass panels over them and
rounded popups. Nothing else is changed, and themes without the new keys look and behave as before.

## THEME.INI keys (Folders themes, all optional)
| Key | |
|---|---|
| `backdrop` | `waves` (the DreamPi web page's wave plane and clouds), `waves_low` (the same with fewer triangles) or `synthwave` |
| `scene_sky`, `scene_horizon`, `scene_sun`, `scene_grid` | r,g,b colors of the synthwave scene |
| `panel_0` .. `panel_3` | x,y,w,h of glass panels drawn over the backdrop, in the border and fill colors of the menu |
| `menu_corner_radius` | rounded corners for the popups and the panels, 0 is square |
| `item_details_scale` | size of the disc count text in percent |

The picture of a theme with a backdrop (`BG_L.PVR`, `BG_R.PVR`) is ARGB4444 and mostly see-through: openMenu draws the logo and the
legend from it over the panels.

## Code (`openMenu/src/openmenu/src/`)
| File | |
|---|---|
| `ui/backdrop.h`, `ui/backdrop_waves.c`, `ui/backdrop_synthwave.c` | the backdrops, one file each. They draw in the opaque pass, since the translucent pass is full of text |
| `ui/draw_kos.c` | rounded rectangles, popup frame and glass panel |
| `ui/theme_manager.c/.h`, `ui/ui_folders.c`, `ui/ui_menu_credits.c` | the keys, and drawing the backdrop, panels and popups |
| `ui/dc/font_bitmap.c` | `font_bmp_set_scale` |
| `main.c` | one extra set of object pointer blocks in `pvr_init`, a tile with too many polygons otherwise drops the rest |

A new backdrop is a new `backdrop_<name>.c`, an entry in the `backdrop` enum and a line in `drawOP` in `ui_folders.c`. The Dreamcast
cannot load code from the SD card, so how a backdrop looks (colors) comes from the theme and what it is comes from the build.

## Themes (`tools/folders-theme/`)
`pip install -r requirements.txt` and `python3 build_theme.py` write four sets, each with `FOLDERS_8` (orange) and `FOLDERS_9` (blue):
`out/` (still picture, also works on stock openMenu), `out_animated/` (waves), `out_lowres/` (fewer triangles) and `out_synthwave/`.
Install one set into the theme folder of the card. The logo and the button icons are cut from the default Folders theme.
`render_backdrop_frame.py [frame] [out.png] [waves|synthwave] [sunset|ice]` renders a backdrop frame for the previews (needs gcc).
The cloud texture is made by `make_backdrop_texture.py`.
