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

Still open: date/time and language settings, Files (memory card manager), Music (online).

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
the menu disc, by product code. (The games' own `0GDTEX.PVR` labels cannot be read: only the
mounted menu image is visible to the console.)

Starting a game: the rows leave in a circle, the selected disc moves to the CD player's place and
the game starts. Settings > Starting games > Launch animation turns this off (stored in the unused
"scroll art" save variable, default On). Not animated: starting from the recently played popup.
Preview: `bios_preview dc_boot.bin out.ppm 60 <row> -3` (list), `... 52 <row> -4` (launch animation).
Tune on hardware: row layout constants at the top of `bios_list.c`, panel layout in `ui_bios.c`.

Game list details: multi-disc sets (Compact mode) show a small pill with the gold side of a CD and
"disc:total"; Left/Right on such a row pick the disc, A starts it (Left/Right page the list on other
rows). The selected row is lit across the title; the BACK marker sits under the info box.
