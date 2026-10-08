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

## Next

Phase 1: `bios_rom` module reading models, textures, scripts and sounds from the
BIOS ROM (mapped at 0xA0000000), with BIOS revision detection.
