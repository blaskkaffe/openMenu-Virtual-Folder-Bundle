# NewOpenMenu

A stripped-down openMenu that will become a 3D GDEMU launcher styled after the
Dreamcast BIOS menu, using the console's own BIOS ROM assets at runtime (no BIOS
data is shipped or committed).

## Phase 0 (this state)

Minimal openMenu: boot, read `OPENMENU.INI`, list games (with virtual folders),
launch the selected one. Nothing else.

- `src/openmenu/src/main.c`: init, placeholder list UI (BIOS font drawn straight
  into the framebuffer, no PVR), controller input.
- `src/openmenu/src/launch.c`: mounts the GDEMU image and starts the game or exits
  through the BIOS for non-game discs.
- `src/openmenu_shared`, `src/openmenu_settings`: game list and settings storage,
  unchanged.

Removed versus full openMenu: themes, artwork/DAT textures, grid/scroll/folder
UIs, DC Now, DreamPi link, online time/games, BGM, VM2/VMU integration, mouse,
keyboard, credits, launch history, cheats/bloom launch paths.

Controls: D-pad move, left/right page, A open/launch, B back out of a folder.

Build: unchanged, see `BUILD_INSTRUCTIONS.md`.

Not yet verified on a real build: this was written without a KOS toolchain
available, only syntax-checked against the KOS API signatures.

## Phase 1 (bios_rom)

`src/bios_rom`: portable C module that reads textures (GBIX/PVRT), the script
bytecode bank, message tables and the sound directory straight from the boot ROM
(`0xA0000000` on the console). Revision is detected from the ROM header; only
1.01d is accepted for now. Host test: `test/bios_rom_test.c` builds a synthetic ROM
(no Sega data), and also checks a real dump when `BIOS_ROM_FILE` is set.
The app shows the result on the bottom line of the screen.

## Phase 2 (BIOS-style visuals)

- `src/bios_engine/`: portable engine. `nj_model` parses the ROM's Ninja chunk models and
  motions, `bios_vm` runs the menu's bytecode scripts and tweens, `dcbg` is the animated cloud
  background, `bios_scene` turns all of it into screen triangles for any backend, `bios_menu`
  creates the four icons and handles the 2x2 cursor, `tex_decode` is for host tools.
- `src/openmenu/src/gfx.c`: PVR backend (ROM textures uploaded on demand, BIOS-font text
  rendered into small textures). `ui_bios.c` is the menu, `ui_list.c` the game list logic.
- `src/bios_engine/tools/bios_preview.c`: renders the menu on a PC from a ROM dump
  (`bios_preview dc_boot.bin out.ppm [frames] [selected] [script]`) with a software
  rasteriser, to check models/scripts/projection without hardware.
- Unknown or unreadable ROM: falls back to the plain text list (`ui_fallback.c`).

## Phase 3 (sound)

`bios_audio` (portable, host tested) parses the sound bank container and builds the data map and
command bytes; `sound.c` loads the ARM7 driver and banks from the ROM into the sound chip and
plays the UI effects (cursor, confirm, back, enter, error). Sound is silently disabled if the
container does not look as expected. The status line at the bottom shows the result.

## Phase 4 (menu wired to the launcher)

Game opens the game browser (with a detail line), Settings edits boot animation and the exit
mode and saves them. Files and Music show "Not available yet".

Still open: language settings, Music (online).

## Custom header logo

The "Dreamcast" logo in the header bar comes from the console's ROM. To use your own, put a
`LOGO.PVR` next to `OPENMENU.INI` (root of the openMenu disc image); if it is not there the BIOS
logo is used. `tools/bios-logo/biologo.py` makes the file: `convert logo.png LOGO.PVR`
(any PNG, scaled to 128x32, transparency kept) and `extract dc_boot.bin logo.png` saves the
BIOS logo as a starting point. `BIOS_PREVIEW_LOGO=LOGO.PVR bios_preview ...` shows it on a PC.
A fully transparent picture hides the logo.

## Tested against real ROMs

The host tests and the preview tool were run against the retail boot ROMs 1.01c, 1.01d, 1.022,
1.032 (region-free build) and the 1.011 dev ROM: all 62 models, 18 motions, 18 textures, 88 scripts
and the sound container load, and every script runs for 120 frames without error. Their menu image
is byte-identical, so `bios_rom_init` accepts a ROM by structure, not version. 1.004 and the early
dev ROMs (0.976, 1.001) have a different layout and fall back to the plain list.

## Not verified on hardware

Phases 0 and 1 build in CI. Everything from Phase 2 on has only been syntax checked against the
KOS API and tested on a PC with a synthetic ROM; texture placement, camera constants, the text
anchor of the icon captions, and the sound driver start-up are the likely places to need tuning.

## Todo (after the essential functions are back)

- Genre filter: the genre list is incomplete and sometimes wrong (the genre data comes from
  the bundled game database). Review and correct it, then bring the filter back.
- Browsing by letter or region: decide between the old category rows (A-Z, region, genre) and
  an L/R jump through the list. Undecided.
- Most played sort (needs a play counter in the save file).

## Game browser settings (phase 5)

All set in Settings: Game order (A-Z, SD card order), Multi-disc games, Disc sets from,
Recently played (Off, 5, 10, 15, 20, 25, 50), Remember last game. In the game list, X opens the
recently played popup. Remember last game restores folder and cursor the first time the
browser is opened after start-up. Later: genre filter (needs better genre data), most played.

## Settings screen (sketch)

Looks like the BIOS Settings screen (`bios_engine/bios_page`): four rows with a 3D icon, a pill and a
text line, and a help box below showing the group and a help line. The list scrolls, so it can hold
any number of rows. Icons are the digit models 0-9 until real ones exist. Rows with more than two
values open a popup list; two-value rows toggle. Rows are defined in `ui_settings.c`
(label, group, help, save-file variable, choice names). Text column positions
(`PAGE_TEXT_PAD`, `PAGE_VALUE_X` in `gfx.c`) are guesses to be tuned on hardware.
Preview without hardware: `bios_preview dc_boot.bin out.ppm 90 <cursor row> -2`.

## Game browser (sketch)

Seven rows in the dense BIOS-settings look (`bios_engine/bios_list`), about 60% of the screen
width. Each row has the BIOS GD-ROM disc model with the game's icon on its label; the selected
disc spins like the CD player's. Box art and info (name, product, region, discs, players) are
shown to the right. Art comes from `ICON.DAT` / `BOX.DAT` (+ `_EX`) and info from `META.DAT` on
the menu disc, by product code. The disc labels are the games' own `0GDTEX.PVR`: the console only
sees the menu image, so GD MENU Card Manager reads each game's label from its image when it builds
the menu and stores them in `DISC.DAT` (same format as ICON.DAT, 128x128, keyed by serial). A game
without one (compressed images not yet saved to the card, discs without the file) keeps the
ICON.DAT picture, then the BIOS disc of its region.

Starting a game: the rows leave in a circle, the selected disc moves to the CD player's place and
the game starts. Settings > Starting games > Launch animation turns this off (stored in the unused
"scroll art" save variable, default On). Not animated: starting from the recently played popup.
Preview: `bios_preview dc_boot.bin out.ppm 60 <row> -3` (list), `... 52 <row> -4` (launch animation).
Tune on hardware: row layout constants at the top of `bios_list.c`, panel layout in `ui_bios.c`.

Game list details: multi-disc sets (Compact mode) show a small pill with the gold side of a CD and
"disc:total"; Left/Right on such a row pick the disc, A starts it (Left/Right page the list on other
rows). The selected row is lit across the title; the BACK marker sits under the info box.

Game list bottom row: the BACK marker and the CD player's five buttons (without their pictures)
sit where they do in the CD player. For now the buttons show product code, region, players, VMU
blocks and disc count (`draw_button_text` in `ui_bios.c`); they are free for other info.

Disc colour: a game without icon art shows the BIOS disc of its region, chosen from the serial number
(`serial_region.c`): PAL (Sega MK + 7 digits, third party ...D, ...D50, ...N50) gets the blue disc,
everything else the red one - the same pairing the BIOS uses (blue on European consoles).

## Date/time editor, aspect, About, mouse and keyboard (sketch)

- Settings > Date and time opens the BIOS editor (`bios_engine/bios_datetime`): left/right pick a
  field, up/down change it, the green arrows (model 35) sit above and below the field, Select sets
  the console clock, Cancel or B leaves. Years 1950-2085, field order by console region (Japan
  Y/M/D, America M/D/Y, Europe D/M/Y). The window panel is drawn as a rounded rectangle.
- Settings > Aspect ratio: 16:9 squeezes everything except the background gradient to 3/4 width
  (`gfx_set_aspect`) so a wide TV shows it in proportion.
- Settings > About (last row, question mark icon): version, BIOS revision, sound status.
- Keyboard: arrows, Enter = A, Escape/Backspace = B, Tab = X, Page Up/Down, and typing a letter or
  digit jumps to the next game starting with it. Mouse: the pointer is model 35 turned 225 degrees
  so its tip points to the top left; moving over a row selects it, left click = A, right click = B,
  the wheel scrolls. Both are written against the KOS maple API and untested on hardware.

Window panels: the BIOS draws its windows with model 39 (four corners moved apart to the wanted size,
dark translucent body, rim in the screen's accent colour: Game orange, Settings magenta). The date
editor, the About box and the popups use it now (`bscene_draw_panel`). In the editor the green ovals
are drawn under the names of the buttons, which start over the right half of the oval.

## Files screen: the BIOS File screen

Main menu > Files is the BIOS's own File screen, ported from the boot ROM (`bios_engine/bios_filescr`,
`file_screen_update` 0x8C017A60 and what it calls). Same objects, scripts, positions, cursor tables, state
machines and messages (read from the ROM's message tables) as the original:

- card grid: each card in its own colour with its picture (ICONDATA_VMS, else the root block's icon shape),
  empty sockets, unformatted cards (orange picture), the VMU busy animation while a card is read,
  controllers only in the ports that have one, the plate with "A-1", free blocks and the used-space gauge,
  the selected card animating, BACK; in destination mode the copy arrow from the source card
- the card flies between the grid and the file window (script 7)
- file window: 24 tiles a page with the files' animated icons, block counts in the tile digits, copy
  protected files red, VMU games green, the selected tiles blinking; the card ("ALL") selects every file,
  X selects all files of the same game (first 9 characters of the name); file information (description,
  name, VMU description, date, blocks, GAME/DATA, eyecatch) or the total of the selected files
- popups and messages: Copy / Delete, Copy all / Delete all (memory reset), Start copying / View contents
  of the destination, overwrite question, "not ready", "full", "one VMU game per card", "card removed"
- copy box with its progress bar (grid mesh, deformer 5), "Deleting all..." box
- memory reset: confirm, icon picker (124 BIOS icons, 4x4 a page), colour picker (the BIOS's 32 colours
  and "Transparent"), "Please confirm your settings", then the card is rebuilt (also never formatted cards)
- screen fly-in / fly-out fades (object flag 17)

Text is printed into the BIOS's text surfaces (`bios_surface`: message strings with colour codes and
button symbols, tile digits, the clock font); `gfx.c` uploads only the rows that change. The card data
and the commands are the caller's (`ui_files.c` with `vmu_files.c`, KOS vmufs): one memory card access a
frame (a card, a file header, or one file of a copy / delete), so the screen keeps running.
Preview: `bios_preview dc_boot.bin out.ppm <frames> 0 -14` with `BIOS_PREVIEW_KEYS="40:A,90:R,..."` (keys
UDLRABXY at frame numbers) on a demo set of cards.
Not yet: mouse hover. Untested on hardware: copying and resetting real cards; back up first.

## Todo: Files, devices, DreamPi (from the plan, later)

- Browse and load Serial VMU saves (the saves kept on the SD card by the serial VMU emulation) in the
  Files menu, next to the real cards.
- Show an SD card model or icon on the VMU slot that is allocated for the serial SD.
- Serial SD settings in the Files menu: on/off, slot/controller number, back up now, load backup.
- Integrate with VM2, VMU Pro, USB4Maple and Pico2Maple: sense which of them is plugged in, and
  control their own features from the Files menu: switch between VMU slots (and know which is
  connected), enable/disable GameID and show its status, battery level of VM2 and VMU Pro, and more.
- Much later: integrate with DreamPi so that the DreamPi script can load, download and upload saves
  and VMU backups.

## Todo: Online menu item (blue, takes the place of Music)

- Online players: from DreamcastLive (as openMenu 1.7 does) and from dc99.net. Favourite players.
- Games being played online, matched with the games on the SD card, so a game somebody is playing
  now can be launched from the list. Favourite online games.
- Games and applications (browsers) on the card with known, currently working online support, with
  which network to use for each (lists from DreamcastLive and the Flycast DCNet list). Show each
  game's own settings, such as DNS, where there are any.
- The online event schedule of dc99.net.
- Switch between DCNET and DCNow! (the default) through the DreamPi companion script.
- Show which networks a game is patched for (serial PPP, WIZnet), and group several patched
  versions of a game so that, for example, the regular and the WIZnet one sit together.
- Change the console's ISP network settings without starting a browser, if that is possible, with 2-3
  presets to switch between (with a clear warning, as this writes to the flash memory).
- In-menu notifications when a favourite player or game comes online.

## Extra models in the BIOS style (sketch2)

`src/bios_engine/src/bios_models.c` builds four models in code (no ROM data), ids from `BMODEL_BASE` (0x100):
`BMODEL_PHONE` (classic telephone in the music note colour, plain white dial disc), `BMODEL_GLOBE` (low poly
earth, green land raised over blue sea, small stand), `BMODEL_CASE_WHITE` and `BMODEL_CASE_PAL` (CD jewel
cases, white spine / blue PAL style). Use the id as `bvm_obj.model` and also as its `texlist`; the scene
draws them like ROM models.

The jewel cases have two texture slots: slot 0 (`BMODEL_TEX_FRONT`) is the front picture (box art, UV 0..1,
top-left origin), slot 1 (`BMODEL_TEX_BACK`) the back. In the app `gfx_model_bind_front(product)` puts the box
art of a game on the front; the back is plain white until something is placed there.
Triangle counts (budget about 330, the ROM icons have 208 to 486): phone 228, globe 320 (icosphere, 1280 with -DGLOBE_SUBDIV=3), cases 88 and 80.
`models/*.obj` are the same models exported with `tools/bmodel_obj` (materials `front_art`, `back_art`).
Preview: `BIOS_PREVIEW_ROT="22,-25,0" bios_preview dc_boot.bin out.ppm 5 <0..3> -10`.

## CD case in the game browser (sketch2)

The flat box art of the right panel is replaced by the 3D jewel case of the selected game (blue PAL case for
PAL serials, white-spine case otherwise; the box art is the front picture). `bios_case.c`: when the selection
moves down the new case flies in from the top while the old one leaves at the bottom (up: the other way round);
motion is an ease-out of 26% of the remaining way per frame (about 15 frames), the case leans into the motion.
At rest it tilts slowly about all axes (periods roughly 9, 12 and 15 s), each game with its own phase and
direction. Folders show no case. Preview: `bios_preview dc_boot.bin out.ppm 5 <frames> -11`.

## Lighting of the models

Taken from the reference in `menu3d.zip` (traced through the ROM, checked against a Flycast capture of the real BIOS):
one directional light (0, 0, -1) in view space, intensities spc 1.4, dif 0.3, amb 0, global ambient 0.5; `njControl3D(0x20)` (offset
material): diffuse += the object's constant colour (script colour op) and the ambient colour := that diffuse, so the files'
ambient chunks are never used. Per vertex: `M = diffuse + const`, `d = max(0, nz)` of the transformed, unnormalised normal,
`rgb = clamp(0.5 M + 0.3 d M)` (no 0.5 term with strip flag 0x04), `alpha = clamp(M.a)`; flag 0x01 = unlit, material as is.
Specular `1.4 * spec.rgb * (d*d)^P[n]` (n = specular alpha byte, P = 0, .5, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128) only
reaches textured strips, as the PVR offset colour (`bscene_vtx.oargb`, `cxt.gen.specular`); untextured strips never show it. Nodes with
eval flag 0x08 are hidden. Code: `bios_lit()`, `bios_offset()` in `bios_scene.c`. Main screen: every icon is drawn twice (0x200.. and 0x300..,
alpha -90/256 each), pills alpha -50/256, header rgb -30/256.
Colours picked by scripts that are not a constant material (panel accent, BACK overrides) are shown unlit.

Sorting: the BIOS lets the PVR sort translucent polygons per pixel (autosort). Where an icon's parts and its two
copies overlap (d-pad on the body, the copy 0.04 behind), any per-triangle CPU order leaves artefacts, so the main
screen switches the PVR to autosort for its frames (`gfx_set_autosort`, CMake `GFX_MAIN_AUTOSORT`, default 1) and
sends the objects unsorted (`bmenu.hw_autosort`). The switch rewrites the presort bit of the tile control words of the
buffer the TA fills next (`set_tile_presort` in `gfx.c`, the fix later KOS versions have); KOS 2.1.1's own
`pvr_set_presort_mode()` must not be called per frame: it moves the tile matrix 0x48 bytes on each call, which broke
the lower right of the picture into stripes and hung the PVR on the first frame. It needs KOS's `pvr_internal.h`,
which CMake takes from `$KOS_BASE`; without it the build keeps one mode and the CPU sorter. Other screens keep the build default (`GFX_PRESORT`). The PC
preview composites the main menu per pixel the same way (`BIOS_PREVIEW_PRESORT=1` for submission order). Checked:
`BIOS_PREVIEW_FLAT=1 bios_preview dc_boot.bin out.ppm 120 -1` matches the reference render of `menu3d_ref.py`
within one level on every icon and pill pixel.
BACK marker: selectable on the game list, Settings (BIOS cursor table 0x8C037D70) and the File card grid (0x8C03884C).

## Frame time

Measured as SH-4 instructions per frame of the engine (scene building only, `qemu-sh4` with one instruction per
block; the console runs about 3.3 million cycles per 60 Hz frame):

| Screen | 4be3853 (60 fps) | before this change | now |
|---|---|---|---|
| Main menu, an icon animating | 1.29 M | 3.34 M | 1.04 M |
| Settings | 0.85 M | 1.18 M | 0.45 M |
| Game list | | 3.41 M | 0.71 M |
| File card grid | | 3.83 M | 0.39 M |

What changed: the lit colour of a vertex is worked out once per strip instead of once per triangle corner; the
sphere-map u, v only for meshes that use it; the round disc faces use a sin/cos table; and objects whose drawing
state (model, motion frame, position, rotation, scale, constant material, scene settings) is the same as in an
earlier frame send their kept triangles again instead of being transformed, lit and projected
(`bscene_cache_*`, checked against uncached drawing frame by frame in `bios_engine_test`). Doubling the icons and
the per-vertex lighting had tripled the main menu's cost; that is what dropped it to 20 fps.

Game list: the box art of the games next to the cursor is read ahead, and a new case waits up to 10 frames for
its picture before flying in, so the front no longer pops in.

## Music: the BIOS CD player

`bios_engine/bios_cdplayer`: the Music screen as `music_screen_update` (0x8C018BC8) builds it: BACK (0x1110 at
-19.53, -14.84), the five buttons 0x1301..0x1305 (scripts 0x17..0x1B), the disc 0x1200 (script 0x1C, shown and
spinning as after the drive's "disc ready" event), the repeat indicator 0x1340 (script 0x47) and the readout
0x1310..0x1319 (scripts 0x2A..0x33: TRACK, two digits, TIME, colon, (h)mm:ss as digit models 21..30, the hundreds
of minutes hidden at 0). Cursor: the BIOS table 0x8C038610 (left / right wrap, start on Play / Pause); the selected
button gets var0 = 1 (its script lights it and plays its motion), BACK var0 = 1 while selected. A on a button makes
it jump (var1 = 1) with the confirm sound, as the BIOS buttons do; nothing is played. B or A on BACK leaves.
Preview: `bios_preview dc_boot.bin out.ppm 120 <cursor> -13` (`BIOS_PREVIEW_NODISC=1`: empty drive).
