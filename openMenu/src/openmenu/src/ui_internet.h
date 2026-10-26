/* ui_internet: the Internet screen, in the Music icon's place while the online features are on. A large panel shows
 * one page at a time; the CD player's buttons along the bottom pick the page (Players, Games, Events, News, Settings), BACK
 * leaves. X connects or hangs up, A refreshes the player list, starts the game or browser chosen, or opens an event or news item. */
#pragma once

#include <bios_menu.h>

#include "input.h"

void uin_open(bmenu* m, int pal);
/* Whether META.DAT is loaded (which games have online play comes from it). */
void uin_set_meta(int loaded);
/* How a game of the card is started from here (the game list's start, which restores the Serial VMU first and returns 1 when it
 * took over). Without it games are launched directly. */
struct gd_item;
void uin_set_launch(int (*start)(const struct gd_item*));
/* A button. Returns 1 when the screen is left. */
int uin_handle(button_t b);
/* Once per frame, before drawing: connection, lists, objects. */
void uin_sync(void);
void uin_draw(void);
/* The mouse over the bottom row selects a button (screen pixels). */
void uin_hover(float x, float y);
