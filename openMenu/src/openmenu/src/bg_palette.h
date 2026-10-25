/* bg_palette: the background colour choice and dark mode of the Settings menu. Pure arithmetic, host-tested (test/bg_palette_test.c). */
#pragma once

#include <stdint.h>

#define BG_PALETTE_COUNT 6 /* 0 is the BIOS's own gradient, left untouched */
const char* bg_palette_name(int index);

/* Apply the choice to the gradient the BIOS gives (ARGB, top and bottom). index 0 keeps the BIOS colours; dark dims either. */
void bg_palette_apply(int index, int dark, uint32_t* top, uint32_t* bottom);
