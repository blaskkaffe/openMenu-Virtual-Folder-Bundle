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

## Next

Phase 2: PVR texture upload, ROM background, Ninja chunk-model renderer, script VM.
