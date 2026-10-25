/* ui_internet: see ui_internet.h. The connection, the player list and the online games are the main branch's engine
 * (backend/dcnow_*, dreampi_link, online_games); the events are backend/events_fetch, the news backend/news_fetch.
 * One page per button: Players (who is online), Games (the card's online games, the ones being played first, then the browsers),
 * Events and News (four to a page, A opens one), Settings (connection, interface, ISP, network). The connection status is
 * drawn above the panel. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <arch/timer.h>

#include <backend/gd_item.h>
#include <backend/db_list.h>
#include <backend/gd_list.h>
#include <bios_audio.h>
#include <bios_list.h>
#include <openmenu_settings.h>

#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/events_fetch.h"
#include "backend/news_fetch.h"
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
#define HEAD_Y (PANE_Y + 10.0f)
#define LIST_Y (PANE_Y + 44.0f)
#define ROWS 8 /* LIST_Y + 8 rows ends above the footer line */
#define ITEM_H 46.0f /* events and news: two lines per item */
#define ITEM_ROWS 4
#define STATUS_Y 10.0f /* the connection status, above the panel */
#define CHAR_W 11 /* the BIOS font's advance */

enum { PAGE_PLAYERS, PAGE_GAMES, PAGE_EVENTS, PAGE_NEWS, PAGE_SETTINGS };
static const char* const page_names[PAGES] = {"Players", "Games", "Events", "News", "Settings"};

/* The lists behind the pages */
enum { LIST_PLAYERS, LIST_GAMES, LIST_EVENTS, LIST_NEWS, LIST_SETTINGS, LISTS };
enum { SET_CONNECTION, SET_INTERFACE, SET_ISP, SET_NETWORK, SET_COMPANION, SET_TEST, SET_ROWS }; /* rows from SET_ROWS on are settings of ui_settings.c (uis_net_first) */

#define WHITE 0xFFFFFFFFu
#define GREY 0xFFB0B0B0u
#define DIM 0xFF909090u
#define YELLOW 0xFFF0E060u

#define GAMES_MAX 256

static bmenu* menu;
static int console_pal;
static int cursor = PAGE_PLAYERS; /* CUR_BACK or the page button */
static int page = PAGE_PLAYERS;
static int top[LISTS], sel[LISTS];
static int detail;     /* an event or a news item is open (on the Events or News page) */
static int detail_top; /* the first line of it shown */
static int have_meta;  /* META.DAT is loaded: db_get_meta may be asked */
static unsigned games_built; /* frame of the last rebuild of the games list */
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

static news_status_t nstat;
static int news_gen = -1;
static news_item_t news[NEWS_MAX];
static int nnews;

/* A row of the Games page: a game of the card (with the players in it now) or a browser disc. */
typedef struct {
    const gd_item* item;
    int players;
    int browser;
} game_row;
static game_row games[GAMES_MAX];
static int ngames;

static void
say(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
    notice_frames = 180;
}

/* ---- the lists -------------------------------------------------------------------------------------------------------- */

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

static int
name_cmp(const char* a, const char* b) {
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Games first by players online, then by name; the browsers last. */
static int
game_cmp(const void* x, const void* y) {
    const game_row* a = x;
    const game_row* b = y;
    if (a->browser != b->browser) {
        return a->browser - b->browser;
    }
    if (a->players != b->players) {
        return b->players - a->players;
    }
    return name_cmp(a->item->name, b->item->name);
}

/* The Games page: every game of the card that has online play (META.DAT) or is being played online now, once per game (not every
 * disc of a set), and the browser discs. The cursor stays on the same game when the order changes. */
static void
build_games(void) {
    const gd_item* keep = sel[LIST_GAMES] < ngames ? games[sel[LIST_GAMES]].item : NULL;
    ngames = 0;
    for (int i = 1; i < list_all_count() && ngames < GAMES_MAX; i++) {
        const gd_item* g = list_all_item(i);
        if (!g || strncmp(g->disc, "DIR", 3) == 0) {
            continue;
        }
        if (is_browser(g->name)) {
            games[ngames].item = g;
            games[ngames].players = 0;
            games[ngames++].browser = 1;
            continue;
        }
        if (!g->product[0] || (gd_item_disc_total(g->disc) > 1 && gd_item_disc_num(g->disc) > 1)) {
            continue;
        }
        int n = online_games_players(g);
        struct db_item* meta = NULL;
        if (n > 0 || (have_meta && !db_get_meta(g->product, &meta) && meta && (meta->network & 3))) {
            games[ngames].item = g;
            games[ngames].players = n;
            games[ngames++].browser = 0;
        }
    }
    qsort(games, (size_t)ngames, sizeof(games[0]), game_cmp);
    for (int i = 0; keep && i < ngames; i++) {
        if (games[i].item == keep) {
            sel[LIST_GAMES] = i;
        }
    }
}

/* The list the cursor is in on a page, -1 when the page has none. */
static int
page_list(int p) {
    switch (p) {
        case PAGE_PLAYERS: return LIST_PLAYERS;
        case PAGE_GAMES: return LIST_GAMES;
        case PAGE_EVENTS: return LIST_EVENTS;
        case PAGE_NEWS: return LIST_NEWS;
        case PAGE_SETTINGS: return LIST_SETTINGS;
        default: return -1;
    }
}

static int
list_rows(int l) {
    switch (l) {
        case LIST_PLAYERS: return nplayers;
        case LIST_GAMES: return ngames;
        case LIST_EVENTS: return nevents;
        case LIST_NEWS: return nnews;
        default: return SET_ROWS;
    }
}

/* How many rows of a list fit on its page */
static int
list_visible(int l) {
    return l == LIST_EVENTS || l == LIST_NEWS ? ITEM_ROWS : ROWS;
}

static void
clamp_list(int l) {
    int n = list_rows(l);
    int vis = list_visible(l);
    if (sel[l] > n - 1) {
        sel[l] = n - 1;
    }
    if (sel[l] < 0) {
        sel[l] = 0;
    }
    if (sel[l] < top[l]) {
        top[l] = sel[l];
    }
    if (sel[l] >= top[l] + vis) {
        top[l] = sel[l] - vis + 1;
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
    news_fetch_start();
}

void
uin_set_meta(int loaded) {
    have_meta = loaded;
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
    detail = 0;
    build_games();
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
                detail = 0;
                cursor--;
                if (cursor >= 0) {
                    page = cursor;
                }
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_RIGHT:
            if (cursor < PAGES - 1) {
                detail = 0;
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
            if (detail) { /* an open item: scroll its text */
                int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -4 : 4;
                detail_top = detail_top + step < 0 ? 0 : detail_top + step;
                break;
            }
            int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -list_visible(l) : list_visible(l);
            int before = sel[l];
            sel[l] += step;
            clamp_list(l);
            if (sel[l] != before) {
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        }
        case BTN_A:
        case BTN_START: {
            if (cursor == CUR_BACK) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                events_fetch_abort();
                news_fetch_abort();
                return 1;
            }
            bvm_obj* o = bvm_find(&menu->vm, ID_BUTTON(cursor));
            if (o) {
                o->var[1] = 1; /* the button jumps, as in the CD player */
            }
            int online = conn.state == DCNOW_CONN_ONLINE;
            switch (page) {
                case PAGE_PLAYERS:
                    if (online) {
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        start_fetches();
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Not connected: X connects");
                    }
                    break;
                case PAGE_GAMES:
                    if (sel[LIST_GAMES] < ngames) { /* start the game or browser (through the game list's start when it has one) */
                        sound_sfx(BAUDIO_SFX_ENTER);
                        if (!start_item(games[sel[LIST_GAMES]].item)) {
                            sound_sfx(BAUDIO_SFX_ERROR);
                            say("Could not start it");
                        }
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("No online games on this card");
                    }
                    break;
                case PAGE_EVENTS:
                case PAGE_NEWS: {
                    int n = page == PAGE_EVENTS ? nevents : nnews;
                    if (detail) {
                        detail = 0;
                    } else if (n > 0) {
                        detail = 1;
                        detail_top = 0;
                        sound_sfx(BAUDIO_SFX_ENTER);
                    } else if (online) {
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        start_fetches();
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Not connected: X connects");
                    }
                    break;
                }
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
            if (detail) {
                detail = 0;
                break;
            }
            events_fetch_abort();
            news_fetch_abort();
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
        nplayers = 0; /* the lists go with the connection, as in the main branch */
        players_gen = -1;
        build_games();
    }
    was_online = online;

    dcnow_fetch_poll(&pstat);
    if (pstat.generation != players_gen && pstat.list_valid) {
        players_gen = pstat.generation;
        nplayers = dcnow_fetch_copy(players, DCNOW_PLAYER_MAX);
        build_games();
        games_built = anim;
    }
    if (page == PAGE_GAMES && anim - games_built >= 300) { /* the Pi's own account of the games changes without a new list */
        build_games();
        games_built = anim;
    }
    news_fetch_poll(&nstat);
    if (nstat.generation != news_gen) {
        news_gen = nstat.generation;
        nnews = news_fetch_copy(news, NEWS_MAX);
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
    int chars = (int)strlen(line) < 46 ? (int)strlen(line) : 46;
    text_cut(line, 46, PANE_X + PANE_W - PAD - (float)(chars * CHAR_W), STATUS_Y, conn.state == DCNOW_CONN_ONLINE ? GREY : YELLOW);
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

/* The Players page: who is online, the game they are in, the network. */
static void
draw_players(void) {
    char line[64];
    int online = conn.state == DCNOW_CONN_ONLINE;
    float x = PANE_X + PAD;
    if (pstat.list_valid) {
        snprintf(line, sizeof(line), "Players online: %d", pstat.online_total);
    } else {
        snprintf(line, sizeof(line), "Players online%s", pstat.state == DCNOW_FETCH_RUNNING ? ": loading" : pstat.state == DCNOW_FETCH_FAILED ? ": failed" : "");
    }
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);

    if (nplayers == 0) {
        char note[DCNOW_STATUS_WIDTH + 16];
        text_cut(!online ? "Connect to see who is online" : pstat.list_valid ? "Nobody is online" : fetch_note(note, sizeof(note)), 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_PLAYERS] + r < nplayers; r++) {
        int i = top[LIST_PLAYERS] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_PLAYERS];
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        text_cut(players[i].name, 12, x, y, is_sel ? YELLOW : WHITE);
        text_cut(players[i].title[0] ? players[i].title : "in the lobby", 23, x + 145.0f, y, players[i].title[0] ? GREY : DIM);
        text_cut(players[i].network, 6, x + 410.0f, y, DIM);
    }
}

/* A plain list page: the rows of `l` drawn by `row_text`, the arrows. */
typedef const char* (*row_text_fn)(int index, char* out, size_t size, uint32_t* colour);

static void
draw_arrows(int l) {
    int n = list_rows(l);
    int vis = list_visible(l);
    float last = l == LIST_EVENTS || l == LIST_NEWS ? (float)(vis - 1) * ITEM_H : (float)(vis - 1) * ROW_H;
    if (top[l] > 0) {
        gfx_text("^", PANE_X + PANE_W - PAD - 11.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (top[l] + vis < n) {
        gfx_text("v", PANE_X + PANE_W - PAD - 11.0f, LIST_Y + last, TEXT_Z, GREY, 1);
    }
}

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
    draw_arrows(l);
}

/* The Games page: players in it now (highlighted rows), the title, the browsers last. */
static void
draw_games(void) {
    float x = PANE_X + PAD;
    char line[64];
    int playing = 0;
    for (int i = 0; i < ngames; i++) {
        playing += games[i].players > 0;
    }
    snprintf(line, sizeof(line), "Online games: %d  being played: %d", ngames, playing);
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);
    if (ngames == 0) {
        text_cut(have_meta ? "No online games on this card" : "No game information (META.DAT)", 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_GAMES] + r < ngames; r++) {
        int i = top[LIST_GAMES] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_GAMES];
        const game_row* g = &games[i];
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        if (g->players > 0) {
            gfx_rect(x - 8.0f, y - 2.0f, PANE_W - PAD * 2.0f - 4.0f, ROW_H, 0.44f, 0x3040C040u); /* green tint: somebody plays it now */
            snprintf(line, sizeof(line), "%2d", g->players);
            gfx_text(line, x, y, TEXT_Z, YELLOW, 1);
        }
        text_cut(g->item->name, 38, x + 34.0f, y, is_sel ? YELLOW : g->players > 0 ? WHITE : g->browser ? GREY : 0xFFD0D0D0u);
        if (g->browser) {
            gfx_text("browser", x + PANE_W - PAD * 2.0f - 7.0f * CHAR_W - 16.0f, y, TEXT_Z, DIM, 1);
        }
    }
    draw_arrows(LIST_GAMES);
}

/* Event and news items: two lines each, four to a page. */
static void
draw_items(int l, int n, const char* (*first)(int), const char* (*second)(int)) {
    float x = PANE_X + PAD;
    for (int r = 0; r < ITEM_ROWS && top[l] + r < n; r++) {
        int i = top[l] + r;
        float y = LIST_Y + (float)r * ITEM_H;
        int is_sel = i == sel[l];
        if (is_sel) {
            gfx_rect(x - 8.0f, y - 2.0f, PANE_W - PAD * 2.0f - 4.0f, ITEM_H - 4.0f, 0.45f, 0x40FFFFFFu);
        }
        text_cut(first(i), 44, x, y, is_sel ? YELLOW : WHITE);
        text_cut(second(i), 44, x, y + 22.0f, DIM);
    }
    draw_arrows(l);
}

static const char*
event_first(int i) {
    return events[i].title;
}

static const char*
event_second(int i) {
    static char line[64];
    snprintf(line, sizeof(line), "%s  %s", events[i].when, events[i].source);
    return line;
}

static const char*
news_first(int i) {
    return news[i].title;
}

static const char*
news_second(int i) {
    return news[i].when[0] ? news[i].when : news[i].body[0] ? news[i].body : news[i].source;
}

/* Text cut into lines of `width` characters at spaces. Returns the number of lines, writes the start/length of line `want`. */
static int
wrap_lines(const char* text, int width, int want, const char** start, int* len) {
    int lines = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        int n = (int)strlen(p);
        int cut = n <= width ? n : width;
        if (n > width) {
            while (cut > 0 && p[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = width;
            }
        }
        if (lines == want) {
            *start = p;
            *len = cut;
        }
        lines++;
        p += cut;
    }
    return lines;
}

/* An open event or news item: the heading in yellow, then the details; Up/Down scroll, B closes. */
static void
draw_detail(void) {
    char text[NEWS_BODY_LEN + NEWS_TITLE_LEN + 8];
    const char *title, *facts, *body;
    char fact_line[64];
    if (page == PAGE_EVENTS) {
        const event_item_t* e = &events[sel[LIST_EVENTS]];
        snprintf(fact_line, sizeof(fact_line), "%s  %s", e->when, e->source);
        title = e->title;
        facts = fact_line;
        body = "";
    } else {
        const news_item_t* n = &news[sel[LIST_NEWS]];
        snprintf(fact_line, sizeof(fact_line), "%s%s%s", n->when, n->when[0] ? "  " : "", n->source);
        title = n->title;
        facts = fact_line;
        body = n->body;
    }
    snprintf(text, sizeof(text), "%s", title);
    int tl = wrap_lines(text, 44, -1, NULL, NULL);
    /* lines: the title (wrapped), the facts, a gap, the body (wrapped) */
    int bl = wrap_lines(body, 44, -1, NULL, NULL);
    int total = tl + 2 + bl;
    int vis = ROWS;
    if (detail_top > total - vis) {
        detail_top = total - vis > 0 ? total - vis : 0;
    }
    for (int r = 0; r < vis; r++) {
        int li = detail_top + r;
        float y = LIST_Y + (float)r * ROW_H - 4.0f;
        const char* st = NULL;
        int len = 0;
        uint32_t col = WHITE;
        if (li < tl) {
            wrap_lines(text, 44, li, &st, &len);
            col = YELLOW;
        } else if (li == tl) {
            st = facts;
            len = (int)strlen(facts);
            col = DIM;
        } else if (li > tl + 1 && li - tl - 2 < bl) {
            wrap_lines(body, 44, li - tl - 2, &st, &len);
        }
        if (st && len > 0) {
            char line[48];
            snprintf(line, sizeof(line), "%.*s", len < 46 ? len : 46, st);
            gfx_text(line, PANE_X + PAD, y, TEXT_Z, col, 1);
        }
    }
    if (detail_top > 0) {
        gfx_text("^", PANE_X + PANE_W - PAD - 11.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (detail_top + vis < total) {
        gfx_text("v", PANE_X + PANE_W - PAD - 11.0f, LIST_Y + (float)(vis - 1) * ROW_H, TEXT_Z, GREY, 1);
    }
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
    if (detail && ((page == PAGE_EVENTS && nevents > 0) || (page == PAGE_NEWS && nnews > 0))) {
        draw_detail();
        return;
    }
    detail = 0;
    switch (page) {
        case PAGE_PLAYERS: draw_players(); break;
        case PAGE_GAMES: draw_games(); break;
        case PAGE_EVENTS:
            snprintf(line, sizeof(line), "Upcoming events%s", nevents ? "" : estat.state == EVENTS_RUNNING ? ": loading" : "");
            gfx_text(line, PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
            if (nevents == 0) {
                text_cut(!online ? "Connect to see the events" : estat.state == EVENTS_FAILED ? estat.line : "No events coming", 44, PANE_X + PAD, LIST_Y, GREY);
            } else {
                draw_items(LIST_EVENTS, nevents, event_first, event_second);
            }
            break;
        case PAGE_NEWS:
            snprintf(line, sizeof(line), "News%s", nnews ? "" : nstat.state == NEWS_RUNNING ? ": loading" : "");
            gfx_text(line, PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
            if (nnews == 0) {
                text_cut(!online ? "Connect to read the news" : nstat.state == NEWS_FAILED ? nstat.line : nstat.state == NEWS_RUNNING ? nstat.line : "No news yet  A: retry",
                         44, PANE_X + PAD, LIST_Y, GREY);
            } else {
                draw_items(LIST_NEWS, nnews, news_first, news_second);
            }
            break;
        default:
            gfx_text("Network and DreamPi", PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
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
    } else if (detail) {
        text = "Up/Down: scroll  B: back";
    } else if (page == PAGE_PLAYERS && sel[LIST_PLAYERS] < nplayers) {
        snprintf(line, sizeof(line), "A: refresh the list  X: %s", conn.state == DCNOW_CONN_ONLINE ? "hang up" : "connect");
        text = line;
    } else if (page == PAGE_GAMES && sel[LIST_GAMES] < ngames) {
        text = games[sel[LIST_GAMES]].browser ? "A: start the browser" : "A: start this game";
    } else if (page == PAGE_EVENTS && nevents > 0) {
        text = "A: open the event";
    } else if (page == PAGE_NEWS && nnews > 0) {
        text = "A: read the item";
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
