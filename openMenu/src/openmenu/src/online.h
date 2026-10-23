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

/* For the main menu's top bar: whether the console is online and how many players are. */
typedef struct online_header {
    int shown;   /* Online features is on: the bar has something to say */
    int state;   /* 0 not connected, 1 connecting, 2 online, 3 the last connection failed */
    int players; /* online players in the newest list, -1 when there is none yet */
    int dcnow;   /* of them on DCNow! */
    int dcnet;   /* and on DCNET */
} online_header;
void online_header_get(online_header* out);
