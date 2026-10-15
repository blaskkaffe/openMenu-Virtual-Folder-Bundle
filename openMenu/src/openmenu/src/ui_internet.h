/* ui_internet: the Internet screen, in the Music icon's place while the online features are on. A large panel shows
 * one page at a time; the CD player's buttons along the bottom pick the page (Events, Players, Games, Browsers), BACK
 * leaves. X connects or hangs up, A refreshes (or starts the browser chosen on the Browsers page). */
#pragma once

#include <bios_menu.h>

#include "input.h"

void uin_open(bmenu* m, int pal);
/* A button. Returns 1 when the screen is left. */
int uin_handle(button_t b);
/* Once per frame, before drawing: connection, lists, objects. */
void uin_sync(void);
void uin_draw(void);
/* The mouse over the bottom row selects a button (screen pixels). */
void uin_hover(float x, float y);
