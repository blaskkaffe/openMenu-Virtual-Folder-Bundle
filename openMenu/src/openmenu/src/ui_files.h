/*
 * ui_files: the Files screen, the BIOS File screen for the memory cards: pick a card, browse its
 * files, copy a file to another card or delete it. The state lives here; ui_bios.c calls into it.
 */
#pragma once

#include <bios_menu.h>

#include "input.h"

/* Show the screen (replaces the scene of `menu`). */
void uif_open(bmenu* menu);
void uif_set_pal(int pal); /* PAL console: the BACK arrow colour; call after uif_open() */

/* One button. Returns 1 when the screen is left (back to the main menu). */
int uif_handle(button_t b);

/* Once per frame after the logic steps: lights the cards, runs a pending copy or delete. */
void uif_sync(void);

/* Everything of the screen (the caller has drawn the background). */
void uif_draw(void);

/* The mouse at (x, y) pixels: select what is under it. */
void uif_hover(float x, float y);
