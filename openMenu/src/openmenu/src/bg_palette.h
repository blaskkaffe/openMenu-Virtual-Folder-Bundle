/* bg_palette: the background colour choice and dark mode of the Settings menu. Pure arithmetic, host-tested (test/bg_palette_test.c). */
#pragma once

#include <stdint.h>

#define BG_PALETTE_COUNT 6 /* 0 is the BIOS's own gradient, left untouched */
const char* bg_palette_name(int index);

/* Apply the choice to the gradient the BIOS gives (ARGB, top and bottom). index 0 keeps the BIOS colours; dark dims either. */
/* The multiplier for the background's waves (bscene_set_background_tint): the clouds picture is blue, so it is moved the way the background's
 * bottom colour moves from the BIOS blue, and dimmed in dark mode. 0xFFFFFF when nothing changes. */
uint32_t bg_palette_wave_tint(int index, int dark);
/* The header bar's colour in dark mode (0xAARRGGBB). */
uint32_t bg_palette_bar_dark(void);
void bg_palette_apply(int index, int dark, uint32_t* top, uint32_t* bottom);
