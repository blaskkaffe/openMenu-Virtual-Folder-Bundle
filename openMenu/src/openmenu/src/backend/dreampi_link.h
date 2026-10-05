/*
 * File: dreampi_link.h
 * Project: openmenu
 * Talks to the DreamPi Netswitch add-on over the PPP link: uploads the game
 * list, and starts a game the phone page asked for.
 */

#pragma once

/* Once per frame from the main loop. Starts the link worker when a modem
 * connection is up, and launches a game that the Pi asked for once the menu
 * is idle. */
void dreampi_link_tick(void);

/* Called from the UI draw pass every frame. A requested game starts only on a
 * frame where this was told the plain menu is showing. */
void dreampi_link_ui_idle(int idle);

/* Stops the worker and waits for it. Safe to call at any time. */
void dreampi_link_abort(void);
