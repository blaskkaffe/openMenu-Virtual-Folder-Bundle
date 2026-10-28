/* PS2-style menu on the PVR: the same launcher as the BIOS-style one, drawn like the PS2's browser. */
#pragma once

#include <bios_rom.h>

/* Looks for PS2.PAK (see tools/ps2-assets/ps2pack.py) next to OPENMENU.INI and runs the menu.
 * Returns -1 at once if the pack is missing or unusable or the PVR cannot be set up (the caller
 * then uses the BIOS-style menu); otherwise it never returns. `rom` may be NULL: it is only
 * needed for the sound effects. */
int ui_ps2_run(const bios_rom* rom);
