/*
 * ui_list: navigation of the game list (cursor, paging, virtual folders), shared by
 * the BIOS-style front end and the plain fallback screen. It draws nothing.
 */
#pragma once

#include <backend/gd_item.h>

#include "input.h"

#define UIL_VISIBLE 5 /* rows of the game list screen (bios_list) */

typedef enum {
    UIL_NONE,    /* nothing changed */
    UIL_REDRAW,  /* cursor / folder changed */
    UIL_LAUNCH,  /* *launch is the game to start */
    UIL_EXIT     /* B pressed at the top level */
} uil_result;

void uil_reset(void);
uil_result uil_button(button_t btn, const gd_item** launch);

int uil_count(void);
int uil_top(void);
int uil_cursor(void);
const gd_item* uil_item(int index);
int uil_is_folder(const gd_item* item);
int uil_disc_index(void);                /* picked disc (0-based) of the set under the cursor, else 0 */
int uil_disc_total(const gd_item* item); /* discs in the item's set, 1 if not part of one */
void uil_goto_real(int row);        /* cursor onto a row of the underlying list */
void uil_set_cursor(int index);          /* the cursor onto a visible row (the mouse) */
int uil_jump_to_letter(int c);          /* the keyboard: next title starting with the letter */
