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
