/* online: the main branch's online features (Dreamcast Now!, DCNET through the DreamPi add-on, the VMU status screen,
 * Online Time Sync), run from the BIOS-style front end. The engine (backend/dcnow_*, dreampi_link, online_games,
 * online_time_sync) is the main branch's, so the saved settings and the connection behave the same. */
#pragma once

/* At boot, after the settings are loaded: Auto-Connect starts dialing on its own thread, as the main branch does. */
void online_boot(void);

/* `hangup` is called (on the menu thread) when a modem connection is torn down in a blocking way, to show it. */
void online_set_hangup_hook(void (*hangup)(void));

/* Once per frame. `menu_idle`: nothing else is going on, so a game the phone page asked for may start. */
void online_tick(int menu_idle);

/* Before another program starts: hang up and give the VMU screens back (the next program must not inherit a live
 * modem or adapter). */
void online_shutdown(void);
