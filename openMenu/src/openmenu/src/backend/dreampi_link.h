/*
 * File: dreampi_link.h
 * Project: openmenu
 * Talks to the DreamPi Netswitch add-on over the PPP link: uploads the game
 * list, and starts a game the phone page asked for.
 */

#pragma once

#include <stddef.h>
#include <netinet/in.h>

/* Once per frame from the main loop. Starts the link worker when a modem
 * connection is up, and launches a game that the Pi asked for once the menu
 * is idle. */
void dreampi_link_tick(void);

/* Called from the UI draw pass every frame. A requested game starts only on a
 * frame where this was told the plain menu is showing. */
void dreampi_link_ui_idle(int idle);

/* Stops the worker and waits for it. Safe to call at any time. */
void dreampi_link_abort(void);

/* The Pi's address, once openMenu has heard the add-on answer on this connection. While the first poll is
 * still under way it waits (up to a few seconds, or until *abort_flag is set) so the first player list can
 * already come from the Pi. Returns 1 with the address and its text, 0 when there is no add-on to ask. */
int dreampi_link_pi(struct sockaddr_in* addr, char* host, size_t host_len, const volatile int* abort_flag);

struct gd_item;

/* The live info the Pi adds to every answer. All of it is gone again when the connection is. */
/* The network the DreamPi has selected: 0 not known, 1 DCNow!, 2 DCNET. */
int dreampi_link_network(void);

/* 1 once the Pi has answered on this connection, so dreampi_link_game_playing() means something (nobody playing is an answer too). */
int dreampi_link_playing_known(void);

/* How many players are in this game online right now, by the Pi's account (0 when none): its PLY line names the game's slot (its SD
 * card folder number) or its product code, with the player count. */
int dreampi_link_game_playing(const struct gd_item* item);

/* The DC99 event banner. Returns 1 while it should be on screen (20 seconds when a reminder first comes due, and again at 5 minutes, 1 minute
 * and the start), with two lines: the source and the time ("Sega Discord: in 12 min"), and the title. */
int dreampi_link_event_banner(char* line1, size_t line1_len, char* line2, size_t line2_len);
