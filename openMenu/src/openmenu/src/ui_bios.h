/* BIOS-style menu on the PVR: the four icons on the cloud background, game list, ... */
#pragma once

#include <bios_rom.h>

/* Runs the menu. Returns -1 right away if the PVR could not be set up (caller falls back
 * to the plain list); otherwise it never returns. */
int ui_bios_run(const bios_rom* rom);
