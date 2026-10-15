/*
 * File: online_games.h
 * Project: openmenu
 * Which games on the card somebody is playing online right now, so the game list can mark them.
 */

#pragma once

struct gd_item;

/* Once per frame from the main loop. While a connection is up and Auto-Refresh is on, keeps the player list fresh in the background and
 * matches the games being played to the games on the card whenever a new list arrives. */
void online_games_tick(void);

/* How many online players are in this game, 0 when none or when the item is not a game on the card. Cheap enough to call per row. */
int online_games_players(const struct gd_item* item);

/* The game on the card that an online title means (the same name as the DC Now! window shows, matched as the marks are), or NULL. */
const struct gd_item* online_games_find(const char* title);
