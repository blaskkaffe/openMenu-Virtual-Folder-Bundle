/*
 * ui_files: the File screen, the BIOS File screen for the memory cards: pick a card, browse its
 * files, copy files to another card, delete them, reset a card. The screen is bios_filescr; ui_bios.c calls in here.
 */
#pragma once

#include <bios_menu.h>

#include "input.h"

/* Show the screen (replaces the scene of `menu`). */
void uif_open(bmenu* menu);
void uif_set_pal(int pal); /* PAL console: the BACK arrow colour; call after uif_open() */

/* One button (taken by the screen on its next step). Returns 0. Besides the controller's buttons: Y (same game, as X), Page Up / Down
 * (a page of files, a page of icons). */
int uif_handle(button_t b);

/* One 60 Hz step of the screen, after bmenu_update(). Returns 1 once the screen has been left. */
int uif_step(void);

/* Once per frame after the logic steps: one memory card access (reading a card, a file header, or a step of a
 * copy, deletion or memory reset). */
void uif_sync(void);

/* Everything of the screen (the caller has drawn the background). */
void uif_draw(void);

/* The mouse at (x, y) pixels on the 640x480 picture: the cursor goes to what is under it (a card, BACK, a file, a popup choice,
 * an icon, a colour, a button), as a direction key would. Returns 1 if the pointer is over such a thing, so a click on it
 * (BTN_A) can be told from a click on nothing, which does nothing. */
int uif_hover(float x, float y);

/* 1 while the screen has pages the mouse wheel should turn (the file window, the icon picker) instead of moving the cursor. */
int uif_pages(void);
