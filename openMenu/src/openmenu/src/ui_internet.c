/* ui_internet: see ui_internet.h. The connection, the player list and the online games are the main branch's engine
 * (backend/dcnow_*, dreampi_link, online_games); the events are backend/events_fetch.
 * Sections as the brief has them, one per button: Online (players and games side by side), Play (browsers; the online game
 * catalogue is not there yet), Events, News (needs the DreamPi companion) and Settings (connection, interface, ISP, network). */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <arch/timer.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <bios_audio.h>
#include <bios_list.h>
#include <openmenu_settings.h>

#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/events_fetch.h"
#include "backend/online_games.h"
#include "gfx.h"
#include "launch.h"
#include "sound.h"
#include "ui_internet.h"
#include "ui_settings.h"

/* The bottom row, as the game list has it: the CD player's BACK marker and buttons (scripts 6, 0x17..0x1A) */
#define PRIO 0x1000
#define ID_BACK 0x1110
#define ID_BUTTON(i) ((uint16_t)(0x1301 + (i)))
#define BACK_X (-19.53125f)
#define BACK_Y (-14.84375f)
#define BACK_Z (-351.5625f)
#define PAGES 5
#define CUR_BACK (-1)

/* The panel: from where the rows start to the right edge of the rows' area, down to just above the buttons */
#define PANE_X 52.0f /* the clouds show all round the box: 52 px left and right, 40 above, 36 above the buttons */
#define PANE_Y 40.0f
#define PANE_W 536.0f
#define PANE_H 306.0f
#define PANE_Z 0.0028f /* just in front of the clouds (see ui_bios.c) */
#define TEXT_Z 0.5f
#define PAD 14.0f
#define ROW_H 26.0f
#define LIST_Y (PANE_Y + 76.0f)
#define ROWS 7 /* LIST_Y + 7 rows ends above the footer line */
#define CHAR_W 11 /* the BIOS font's advance */

enum { PAGE_ONLINE, PAGE_PLAY, PAGE_EVENTS, PAGE_NEWS, PAGE_SETTINGS };
static const char* const page_names[PAGES] = {"Online", "Play", "Events", "News", "Settings"};

/* The lists behind the pages (the Online page has two, Y moves between them) */
enum { LIST_PLAYERS, LIST_GAMES, LIST_EVENTS, LIST_PLAY, LIST_SETTINGS, LISTS };
enum { SET_CONNECTION, SET_INTERFACE, SET_ISP, SET_NETWORK, SET_COMPANION, SET_TEST, SET_ROWS }; /* rows from SET_ROWS on are settings of ui_settings.c (uis_net_first) */

/* The Online page's columns */
#define COL_L_X (PANE_X + PAD)
#define COL_L_W 290.0f
#define COL_R_X (PANE_X + PAD + 300.0f)

#define WHITE 0xFFFFFFFFu
#define GREY 0xFFB0B0B0u
#define DIM 0xFF909090u
#define YELLOW 0xFFF0E060u

#define BROWSERS_MAX 16
#define GAMES_MAX 64

static bmenu* menu;
static int console_pal;
static int cursor = PAGE_ONLINE; /* CUR_BACK or the page button */
static int page = PAGE_ONLINE;
static int top[LISTS], sel[LISTS];
static int focus_games; /* the Online page: the games column has the cursor, not the players */
static int (*start_game_fn)(const gd_item*); /* the game list's start (Serial VMU first); NULL: launch directly */
static unsigned anim;
static int probed;
static int adapter_tried; /* the adapter was connected on its own once since the start */
static dcnow_device_t device = DCNOW_DEV_NONE;
static dcnow_isp_t isp;
static dcnow_status_t conn;
static int was_online;
static uint64_t last_players_fetch;
static char notice[48];
static int notice_frames;

static dcnow_fetch_status_t pstat;
static int players_gen = -1;
static dcnow_player_t players[DCNOW_PLAYER_MAX];
static int nplayers;

static events_status_t estat;
static int events_gen = -1;
static event_item_t events[EVENTS_MAX];
static int nevents;

typedef struct {
    char title[DCNOW_TITLE_LEN];
    int players;
    int on_card;
} game_row;
static game_row games[GAMES_MAX];
static int ngames;

static const gd_item* browsers[BROWSERS_MAX];
static int nbrowsers;

static void
say(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
    notice_frames = 180;
}

/* ---- the lists -------------------------------------------------------------------------------------------------------- */

/* Games being played: the player list grouped by title, most players first. */
static void
build_games(void) {
    ngames = 0;
    for (int i = 0; i < nplayers; i++) {
        const char* t = players[i].title;
        if (!t[0]) {
            continue; /* idle in the lobby */
        }
        int k = 0;
        while (k < ngames && strcmp(games[k].title, t) != 0) {
            k++;
        }
        if (k == ngames) {
            if (ngames == GAMES_MAX) {
                continue;
            }
            snprintf(games[k].title, sizeof(games[k].title), "%s", t);
            games[k].players = 0;
            games[k].on_card = online_games_find(t) != NULL;
            ngames++;
        }
        games[k].players++;
    }
    for (int i = 1; i < ngames; i++) { /* most played first */
        game_row g = games[i];
        int j = i - 1;
        while (j >= 0 && games[j].players < g.players) {
            games[j + 1] = games[j];
            j--;
        }
        games[j + 1] = g;
    }
}

/* Browser discs on the card, by their titles. */
static int
is_browser(const char* name) {
    static const char* const keys[] = {"BROWSER", "PASSPORT", "PLANETWEB", "PLANET WEB", "DREAMKEY", "DREAM KEY", "DREAMEXPLORER"};
    char up[128];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(up); n++) {
        up[n] = (char)toupper((unsigned char)name[n]);
    }
    up[n] = '\0';
    for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        if (strstr(up, keys[k])) {
            return 1;
        }
    }
    return 0;
}

static void
find_browsers(void) {
    nbrowsers = 0;
    for (int i = 1; i < list_all_count() && nbrowsers < BROWSERS_MAX; i++) {
        const gd_item* g = list_all_item(i);
        if (g && strncmp(g->disc, "DIR", 3) != 0 && is_browser(g->name)) {
            browsers[nbrowsers++] = g;
        }
    }
}

/* The list the cursor is in on a page, -1 when the page has none. */
static int
page_list(int p) {
    switch (p) {
        case PAGE_ONLINE: return focus_games ? LIST_GAMES : LIST_PLAYERS;
        case PAGE_PLAY: return LIST_PLAY;
        case PAGE_EVENTS: return LIST_EVENTS;
        case PAGE_SETTINGS: return LIST_SETTINGS;
        default: return -1;
    }
}

/* Rows of a list: the Play page has the browser discs and one more (the catalogue to come). */
static int
list_rows(int l) {
    switch (l) {
        case LIST_PLAYERS: return nplayers;
        case LIST_GAMES: return ngames;
        case LIST_EVENTS: return nevents;
        case LIST_PLAY: return nbrowsers + 1;
        default: return SET_ROWS + uis_net_count();
    }
}

static void
clamp_list(int l) {
    int n = list_rows(l);
    if (sel[l] > n - 1) {
        sel[l] = n - 1;
    }
    if (sel[l] < 0) {
        sel[l] = 0;
    }
    if (sel[l] < top[l]) {
        top[l] = sel[l];
    }
    if (sel[l] >= top[l] + ROWS) {
        top[l] = sel[l] - ROWS + 1;
    }
}

/* Starts a game of the card (through the game list's start when it has one). Returns 1 if that took over. */
static int
start_item(const gd_item* g) {
    if (start_game_fn) {
        return start_game_fn(g);
    }
    launch_disc(g); /* only returns if it could not be started */
    return 0;
}

void
uin_set_launch(int (*start)(const gd_item*)) {
    start_game_fn = start;
}

/* ---- connection and fetches ------------------------------------------------------------------------------------------- */

static void
start_fetches(void) {
    last_players_fetch = timer_ms_gettime64();
    dcnow_fetch_start();
    events_fetch_start();
}

static void
connect_or_hang_up(void) {
    dcnow_conn_poll(&conn);
    switch (conn.state) {
        case DCNOW_CONN_ONLINE:
            dcnow_conn_disconnect();
            sound_sfx(BAUDIO_SFX_CANCEL);
            break;
        case DCNOW_CONN_CONNECTING:
            if (conn.can_cancel) {
                dcnow_conn_cancel();
                sound_sfx(BAUDIO_SFX_CANCEL);
            }
            break;
        case DCNOW_CONN_COOLDOWN: sound_sfx(BAUDIO_SFX_ERROR); break; /* the DreamPi needs a moment between calls */
        default:
            /* with no adapter the engine asks the serial port for DreamPi first, then dials the modem if one is fitted */
            /* a flash ROM read runs with interrupts off: the engine's own copy when it has one (as the main branch) */
            if ((device == DCNOW_DEV_BBA || device == DCNOW_DEV_LAN) && !dcnow_conn_isp(&isp)) {
                dcnow_read_isp(&isp);
            }
            if (dcnow_conn_start(device, &isp) == 0) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
            }
            break;
    }
}

void
uin_open(bmenu* m, int pal) {
    menu = m;
    console_pal = pal;
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 6, ID_BACK, PRIO);
    for (int i = 0; i < PAGES; i++) {
        bvm_create(&m->vm, 0x17 + (i == 1 ? 2 : (i == 2 ? 1 : i)), ID_BUTTON(i), PRIO); /* left to right, as bios_list */
    }
    cursor = page;
    anim = 0;
    probed = 0;
    notice_frames = 0;
    find_browsers();
    dcnow_conn_poll(&conn);
    was_online = conn.state == DCNOW_CONN_ONLINE;
    if (was_online) {
        start_fetches();
    }
}

/* ---- input -------------------------------------------------------------------------------------------------------------- */

/* Settings page rows: the label, and whether it does something yet */
static const char*
setting_text(int row, char* out, size_t size) {
    dcnow_device_t used = dcnow_device_hint();
    int online = conn.state == DCNOW_CONN_ONLINE;
    if (row >= SET_ROWS) {
        int r = uis_net_first() + row - SET_ROWS;
        snprintf(out, size, "%s  %s", uis_label(r), uis_value(r));
        return out;
    }
    switch (row) {
        case SET_CONNECTION:
            snprintf(out, size, "Connection  %s", online ? "Online (A: hang up)" : conn.state == DCNOW_CONN_CONNECTING ? "Connecting..." : "Off (A: connect)");
            break;
        case SET_INTERFACE:
            snprintf(out, size, "Interface  %s", used == DCNOW_DEV_NONE && device == DCNOW_DEV_NONE ? "none found (serial cable?)" : dcnow_device_name(used != DCNOW_DEV_NONE ? used : device));
            break;
        case SET_ISP: snprintf(out, size, "ISP settings (coming soon)"); break;
        case SET_NETWORK: snprintf(out, size, "Preferred network (coming soon)"); break;
        case SET_COMPANION: snprintf(out, size, "DreamPi add-on  %s", pstat.from_pi ? "in use" : "not used"); break;
        default: snprintf(out, size, "Network test (coming soon)"); break;
    }
    return out;
}

static int
setting_dim(int row) {
    return row < SET_ROWS && (row == SET_ISP || row == SET_NETWORK || row == SET_TEST);
}

int
uin_handle(button_t b) {
    switch (b) {
        case BTN_LEFT:
            if (cursor > CUR_BACK) {
                cursor--;
                if (cursor >= 0) {
                    page = cursor;
                }
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_RIGHT:
            if (cursor < PAGES - 1) {
                cursor++;
                page = cursor;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_UP:
        case BTN_DOWN:
        case BTN_PAGE_UP:
        case BTN_PAGE_DOWN: {
            int l = page_list(page);
            if (l < 0) {
                break;
            }
            int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -ROWS : ROWS;
            int before = sel[l];
            sel[l] += step;
            clamp_list(l);
            if (sel[l] != before) {
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        }
        case BTN_Y: /* the Online page: the other column */
            if (page == PAGE_ONLINE) {
                focus_games = !focus_games;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_A:
        case BTN_START: {
            if (cursor == CUR_BACK) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                events_fetch_abort();
                return 1;
            }
            bvm_obj* o = bvm_find(&menu->vm, ID_BUTTON(cursor));
            if (o) {
                o->var[1] = 1; /* the button jumps, as in the CD player */
            }
            int online = conn.state == DCNOW_CONN_ONLINE;
            switch (page) {
                case PAGE_ONLINE:
                    if (!online) {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Not connected: X connects");
                    } else if (focus_games && sel[LIST_GAMES] < ngames) { /* join: start the game if it is on the card */
                        const gd_item* g = games[sel[LIST_GAMES]].on_card ? online_games_find(games[sel[LIST_GAMES]].title) : NULL;
                        if (g) {
                            sound_sfx(BAUDIO_SFX_ENTER);
                            if (!start_item(g)) {
                                sound_sfx(BAUDIO_SFX_ERROR);
                                say("Could not start the game");
                            }
                        } else {
                            sound_sfx(BAUDIO_SFX_ERROR);
                            say("That game is not on your card");
                        }
                    } else {
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        start_fetches();
                    }
                    break;
                case PAGE_PLAY:
                    if (sel[LIST_PLAY] < nbrowsers) {
                        sound_sfx(BAUDIO_SFX_ENTER);
                        launch_disc(browsers[sel[LIST_PLAY]]); /* only returns if it could not be started */
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Could not start the browser");
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Online game catalogue: coming soon");
                    }
                    break;
                case PAGE_EVENTS:
                    if (online) {
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        start_fetches();
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Not connected: X connects");
                    }
                    break;
                case PAGE_NEWS:
                    sound_sfx(BAUDIO_SFX_ERROR);
                    say("News needs the DreamPi companion");
                    break;
                default:
                    if (sel[LIST_SETTINGS] == SET_CONNECTION) {
                        connect_or_hang_up();
                    } else if (sel[LIST_SETTINGS] >= SET_ROWS) { /* a choice: the next value, saved at once */
                        int r = uis_net_first() + sel[LIST_SETTINGS] - SET_ROWS;
                        uis_change(r, 1);
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        say(uis_commit() == 0 ? uis_help(r) : "Could not save settings");
                    } else {
                        sound_sfx(setting_dim(sel[LIST_SETTINGS]) ? BAUDIO_SFX_ERROR : BAUDIO_SFX_CURSOR);
                        say(setting_dim(sel[LIST_SETTINGS]) ? "Coming soon" : "Nothing to change here");
                    }
                    break;
            }
            break;
        }
        case BTN_X: connect_or_hang_up(); break;
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            events_fetch_abort();
            return 1;
        default: break;
    }
    return 0;
}

void
uin_hover(float x, float y) {
    if (blist_back_at_px(x, y)) {
        if (cursor != CUR_BACK) sound_sfx(BAUDIO_SFX_CURSOR);
        cursor = CUR_BACK;
        return;
    }
    int bt = blist_button_at_px(x, y);
    if (bt >= 0 && bt < PAGES && bt != cursor) {
        sound_sfx(BAUDIO_SFX_CURSOR);
        cursor = page = bt;
    }
}

/* ---- per frame ------------------------------------------------------------------------------------------------------- */

static void
place(bvm_obj* o, float x, float y, float z) {
    o->flags &= ~(uint32_t)BVM_F_ATTACHED;
    o->pos_tw[0].cur = o->pos[0] = x;
    o->pos_tw[1].cur = o->pos[1] = y;
    o->pos_tw[2].cur = o->pos[2] = z;
    o->pos_tw[0].step = o->pos_tw[1].step = o->pos_tw[2].step = 0;
}

void
uin_sync(void) {
    anim++;
    if (notice_frames > 0) {
        notice_frames--;
    }
    if (!probed && anim > 1) {
        /* the expansion port is probed once the screen shows (it can take a moment), as the main branch's window */
        probed = 1;
        device = dcnow_detect_device();
        /* An adapter needs no Connect step (the main branch's window connects it at once); a modem or the serial cable waits
         * for X, since a dial takes the phone line. Only once per start: after a hang-up X connects again. */
        dcnow_conn_poll(&conn);
        if (!adapter_tried && conn.state == DCNOW_CONN_IDLE && (device == DCNOW_DEV_BBA || device == DCNOW_DEV_LAN)) {
            adapter_tried = 1;
            connect_or_hang_up();
        }
    }
    dcnow_conn_poll(&conn);
    int online = conn.state == DCNOW_CONN_ONLINE;
    if (online && !was_online) {
        start_fetches(); /* just connected */
    }
    if (!online && was_online) {
        nplayers = ngames = 0; /* the lists go with the connection, as in the main branch */
        players_gen = -1;
    }
    was_online = online;

    dcnow_fetch_poll(&pstat);
    if (pstat.generation != players_gen && pstat.list_valid) {
        players_gen = pstat.generation;
        nplayers = dcnow_fetch_copy(players, DCNOW_PLAYER_MAX);
        build_games();
    }
    events_fetch_poll(&estat);
    if (estat.generation != events_gen) {
        events_gen = estat.generation;
        nevents = events_fetch_copy(events, EVENTS_MAX);
    }
    /* Auto-Refresh of the player list (the VMU screen refreshes on its own while it is on), as the main branch */
    if (online && sf_dcnow_vmu[0] != DCNOW_VMU_ON && pstat.state != DCNOW_FETCH_RUNNING && dcnow_refresh_seconds(sf_dcnow_refresh[0]) > 0
        && timer_ms_gettime64() - last_players_fetch >= (uint64_t)dcnow_refresh_seconds(sf_dcnow_refresh[0]) * 1000u) {
        last_players_fetch = timer_ms_gettime64();
        dcnow_fetch_start();
    }
    for (int l = 0; l < LISTS; l++) {
        clamp_list(l);
    }

    bvm_obj* back = bvm_find(&menu->vm, ID_BACK);
    if (back) {
        place(back, BACK_X, BACK_Y, BACK_Z);
    }
    for (int i = 0; i < PAGES; i++) {
        bvm_obj* o = bvm_find(&menu->vm, ID_BUTTON(i));
        if (o) {
            o->var[0] = i == cursor; /* lit, as the CD player's selected button */
        }
    }
}

/* ---- drawing --------------------------------------------------------------------------------------------------------- */

/* `text` cut to `chars` characters ("..." when cut) */
static void
text_cut(const char* text, int chars, float x, float y, uint32_t argb) {
    char line[96];
    int n = (int)strlen(text);
    if (chars > (int)sizeof(line) - 1) {
        chars = (int)sizeof(line) - 1;
    }
    if (n <= chars) {
        gfx_text(text, x, y, TEXT_Z, argb, 1);
        return;
    }
    snprintf(line, sizeof(line), "%.*s..", chars > 2 ? chars - 2 : 0, text);
    gfx_text(line, x, y, TEXT_Z, argb, 1);
}

static void
draw_status(void) {
    char line[64];
    dcnow_device_t used = dcnow_device_hint();
    const char* how = used == DCNOW_DEV_MODEM    ? "modem"
                      : used == DCNOW_DEV_SERIAL ? "serial cable"
                      : used == DCNOW_DEV_NONE   ? ""
                                                 : "network adapter";
    switch (conn.state) {
        case DCNOW_CONN_ONLINE:
            snprintf(line, sizeof(line), "Online%s%s  X: hang up", how[0] ? " via " : "", how);
            break;
        case DCNOW_CONN_CONNECTING:
            snprintf(line, sizeof(line), "%s", conn.count > 0 ? conn.lines[conn.count - 1] : "Connecting...");
            break;
        case DCNOW_CONN_COOLDOWN: snprintf(line, sizeof(line), "Wait %d s before calling again", conn.cooldown_seconds); break;
        case DCNOW_CONN_FAILED:
            /* the engine's failure lines are "Connection failed.", the reason, and advice when there is any: show the reason */
            snprintf(line, sizeof(line), "%s  X: retry", conn.count > 1 ? conn.lines[1] : conn.count > 0 ? conn.lines[0] : "Connection failed");
            break;
        default:
            snprintf(line, sizeof(line), "%s", !probed ? "Looking for a modem..." : device == DCNOW_DEV_NONE ? "Not connected  X: connect by serial cable" : "Not connected  X: connect");
            break;
    }
    text_cut(line, 44, PANE_X + PAD, PANE_Y + 8.0f, conn.state == DCNOW_CONN_ONLINE ? GREY : YELLOW);
}

/* What the player fetch is doing or why it failed (its last status line), for the empty lists: "Loading..." alone hides where
 * it stops (the DreamPi add-on, DNS, the connection to the server, the answer). */
static const char*
fetch_note(char* out, size_t size) {
    if (pstat.state == DCNOW_FETCH_FAILED && pstat.count > 0) {
        snprintf(out, size, "%s  A: retry", pstat.lines[pstat.count - 1]);
        return out;
    }
    if (pstat.state == DCNOW_FETCH_RUNNING && pstat.count > 0) {
        return pstat.lines[pstat.count - 1];
    }
    return "Loading...";
}

/* One row's highlight bar over `w` pixels from x */
static void
row_bar(float x, float w, float y) {
    gfx_rect(x - 8.0f, y - 2.0f, w + 8.0f, ROW_H, 0.45f, 0x40FFFFFFu);
}

/* The Online page: the players on the left, the games being played on the right; Y moves the cursor between them. */
static void
draw_online(void) {
    char line[64];
    int online = conn.state == DCNOW_CONN_ONLINE;
    if (pstat.list_valid) {
        snprintf(line, sizeof(line), "Players: %d", pstat.online_total);
    } else {
        snprintf(line, sizeof(line), "Players%s", pstat.state == DCNOW_FETCH_RUNNING ? ": loading" : pstat.state == DCNOW_FETCH_FAILED ? ": failed" : "");
    }
    gfx_text(line, COL_L_X, PANE_Y + 40.0f, TEXT_Z, !focus_games ? YELLOW : WHITE, 1);
    snprintf(line, sizeof(line), "Games: %d", ngames);
    gfx_text(line, COL_R_X, PANE_Y + 40.0f, TEXT_Z, focus_games ? YELLOW : WHITE, 1);

    if (nplayers == 0) {
        char note[DCNOW_STATUS_WIDTH + 16];
        text_cut(!online ? "Connect to see who is online" : pstat.list_valid ? "Nobody is online" : fetch_note(note, sizeof(note)), 26,
                 COL_L_X, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_PLAYERS] + r < nplayers; r++) {
        int i = top[LIST_PLAYERS] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = !focus_games && i == sel[LIST_PLAYERS];
        if (is_sel) {
            row_bar(COL_L_X, COL_L_W - 12.0f, y);
        }
        text_cut(players[i].name, 13, COL_L_X, y, is_sel ? YELLOW : WHITE);
        gfx_text(players[i].network, COL_L_X + 150.0f, y, TEXT_Z, DIM, 1);
    }
    if (ngames == 0 && online && pstat.list_valid) {
        text_cut("Nobody is in a game", 19, COL_R_X, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_GAMES] + r < ngames; r++) {
        int i = top[LIST_GAMES] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = focus_games && i == sel[LIST_GAMES];
        if (is_sel) {
            row_bar(COL_R_X, 196.0f, y);
        }
        snprintf(line, sizeof(line), "%2d", games[i].players);
        gfx_text(line, COL_R_X, y, TEXT_Z, GREY, 1);
        text_cut(games[i].title, 14, COL_R_X + 30.0f, y, is_sel ? YELLOW : WHITE);
        if (games[i].on_card) {
            gfx_text("*", COL_R_X + 188.0f, y, TEXT_Z, YELLOW, 1);
        }
    }
    if (top[LIST_PLAYERS] > 0) {
        gfx_text("^", COL_L_X + COL_L_W - 14.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (top[LIST_PLAYERS] + ROWS < nplayers) {
        gfx_text("v", COL_L_X + COL_L_W - 14.0f, LIST_Y + (ROWS - 1) * ROW_H, TEXT_Z, GREY, 1);
    }
}

/* A plain list page: heading, the rows of `l` drawn by `row_text`, the arrows. */
typedef const char* (*row_text_fn)(int index, char* out, size_t size, uint32_t* colour);

static void
draw_list(int l, row_text_fn row_text) {
    int n = list_rows(l);
    char line[96];
    for (int r = 0; r < ROWS && top[l] + r < n; r++) {
        int i = top[l] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[l];
        uint32_t col = is_sel ? YELLOW : WHITE;
        const char* text = row_text(i, line, sizeof(line), &col);
        if (is_sel) {
            row_bar(PANE_X + PAD, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        text_cut(text, 44, PANE_X + PAD, y, is_sel && col == WHITE ? YELLOW : col);
    }
    if (top[l] > 0) {
        gfx_text("^", PANE_X + PANE_W - PAD - 11.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (top[l] + ROWS < n) {
        gfx_text("v", PANE_X + PANE_W - PAD - 11.0f, LIST_Y + (ROWS - 1) * ROW_H, TEXT_Z, GREY, 1);
    }
}

static const char*
play_row(int i, char* out, size_t size, uint32_t* colour) {
    (void)size;
    (void)out;
    if (i < nbrowsers) {
        return browsers[i]->name;
    }
    *colour = DIM;
    return "Online game catalogue (coming soon)";
}

static const char*
event_row(int i, char* out, size_t size, uint32_t* colour) {
    (void)colour;
    snprintf(out, size, "%-17.17s %s", events[i].when, events[i].title);
    return out;
}

static const char*
settings_row(int i, char* out, size_t size, uint32_t* colour) {
    if (setting_dim(i)) {
        *colour = DIM;
    }
    return setting_text(i, out, size);
}

static void
draw_page(void) {
    int online = conn.state == DCNOW_CONN_ONLINE;
    char line[64];
    switch (page) {
        case PAGE_ONLINE: draw_online(); break;
        case PAGE_PLAY:
            gfx_text("Browsers on this card", PANE_X + PAD, PANE_Y + 40.0f, TEXT_Z, WHITE, 1);
            draw_list(LIST_PLAY, play_row);
            break;
        case PAGE_EVENTS:
            snprintf(line, sizeof(line), "Upcoming events%s", nevents ? "" : estat.state == EVENTS_RUNNING ? ": loading" : "");
            gfx_text(line, PANE_X + PAD, PANE_Y + 40.0f, TEXT_Z, WHITE, 1);
            if (nevents == 0) {
                text_cut(!online ? "Connect to see the events" : estat.state == EVENTS_FAILED ? estat.line : "No events coming", 44, PANE_X + PAD, LIST_Y, GREY);
            } else {
                draw_list(LIST_EVENTS, event_row);
            }
            break;
        case PAGE_NEWS:
            gfx_text("Community news", PANE_X + PAD, PANE_Y + 40.0f, TEXT_Z, WHITE, 1);
            gfx_text("The DreamPi companion will collect the news", PANE_X + PAD, LIST_Y, TEXT_Z, GREY, 1);
            gfx_text("(DC99, DreamcastLive, ...) and send it here.", PANE_X + PAD, LIST_Y + ROW_H, TEXT_Z, GREY, 1);
            gfx_text("Not available yet.", PANE_X + PAD, LIST_Y + 3.0f * ROW_H, TEXT_Z, DIM, 1);
            break;
        default:
            gfx_text("Network and DreamPi", PANE_X + PAD, PANE_Y + 40.0f, TEXT_Z, WHITE, 1);
            draw_list(LIST_SETTINGS, settings_row);
            break;
    }
}

static void
draw_footer(void) {
    const char* text = NULL;
    char line[96];
    if (notice_frames > 0) {
        text = notice;
    } else if (conn.state == DCNOW_CONN_FAILED && conn.count > 2) {
        text = conn.lines[2]; /* the advice that goes with the reason */
    } else if (page == PAGE_ONLINE) {
        if (focus_games && sel[LIST_GAMES] < ngames) {
            text = games[sel[LIST_GAMES]].on_card ? "A: start this game (it is on your card)  Y: players" : "Not on your card  Y: players";
        } else if (!focus_games && sel[LIST_PLAYERS] < nplayers) {
            snprintf(line, sizeof(line), "%s: %s  Y: games", players[sel[LIST_PLAYERS]].name,
                     players[sel[LIST_PLAYERS]].title[0] ? players[sel[LIST_PLAYERS]].title : "in the lobby");
            text = line;
        } else {
            text = "Y: switch between players and games";
        }
    } else if (page == PAGE_EVENTS && nevents > 0) {
        snprintf(line, sizeof(line), "%s", events[sel[LIST_EVENTS]].source);
        text = line;
    } else if (page == PAGE_PLAY) {
        text = sel[LIST_PLAY] < nbrowsers ? "A: start the browser" : "Games with online features, from the DreamPi";
    }
    if (text) {
        text_cut(text, 44, PANE_X + PAD, PANE_Y + PANE_H - 34.0f, DIM);
    }
}

void
uin_draw(void) {
    const bscene_sink* sink = gfx_sink();
    gfx_pane(PANE_X, PANE_Y, PANE_W, PANE_H, PANE_Z, BSCENE_PANE_ARGB);
    draw_status();
    draw_page();
    draw_footer();

    /* BACK and the buttons: BACK lit with its frame blinking while it is selected, the buttons without their pictures */
    bmenu_back_style(menu, cursor == CUR_BACK, (int)anim, console_pal);
    menu->scene.parts = BSCENE_PART_ALL;
    for (int i = 0; i < menu->vm.count; i++) {
        const bvm_obj* o = &menu->vm.objs[menu->vm.order[i]];
        menu->scene.no_decals = o->id >= ID_BUTTON(0) && o->id < ID_BUTTON(PAGES);
        bscene_draw_object(&menu->scene, o, sink);
    }
    menu->scene.no_decals = 0;
    menu->scene.ovr_n = 0;
    for (int i = 0; i < PAGES; i++) {
        float x, y;
        blist_button_center_px(i, &x, &y);
        gfx_text(page_names[i], x - (float)gfx_text_width(page_names[i]) / 2.0f, y - (float)GFX_LINE_H / 2.0f, TEXT_Z,
                 i == page ? WHITE : GREY, 1);
    }
}
