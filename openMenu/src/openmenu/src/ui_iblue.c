/* ui_iblue: see ui_iblue.h. */
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "iblue_map.h"
#include "ui_iblue.h"

typedef enum { SC_NOBOARD, SC_PORT, SC_REMAP, SC_PICK, SC_LEARN, SC_LEAVE, SC_OPTS, SC_PROF } sc_t;

enum {
    ROW_PORT, ROW_TYPE, ROW_VMU, ROW_RUMBLE, ROW_REMAP, ROW_PROFILES, ROW_PAIR, ROW_RESET, ROW_GAMEDEL, ROW_SAVE,
    ROW_DISCARD, ROW_KINDS
};
enum { OPT_SCALE, OPT_THRESHOLD, OPT_DEADZONE, OPT_TURBO, OPT_ALGO, OPTS };

#define LEFT_X 64.0f
#define VALUE_X 330.0f
#define LIST_Y 128.0f
#define VISIBLE 7
#define LEARN_POLL_FRAMES 5
#define LEARN_TIMEOUT_FRAMES 600
#define SWALLOW_FRAMES 20

/* "Dual stick" is the alternative controller description: the console is told about the
 * extra buttons and the second stick. */
static const char* const dev_names[] = {"Controller", "Dual stick", "Keyboard", "Mouse"};

/* Rapid fire: the board's turbo byte is (frame mask << 1) | phase; a bigger mask is slower. */
static const uint8_t turbo_values[] = {0, 2, 6, 14, 30};
static const char* const turbo_names[] = {"Off", "Fast", "Medium", "Slow", "Very slow"};
#define TURBO_COUNT 5
/* Stick scaling: 0 stretches the controller's range over the Dreamcast's, 5 keeps the raw value. */
static const uint8_t algo_values[] = {0, 5};
static const char* const algo_names[] = {"Linear", "Raw"};

static iblue_link lk;
static sc_t screen;
static int done;
static int port;
static int cursor;
static int rows[ROW_KINDS]; /* the rows of the port screen, in order */
static int row_count;
static int game_mode;       /* editing the settings of one game */
static int game_has;        /* ... which has settings of its own */
static char game_id[IBLUE_GAME_ID_MAX + 1];
static char game_name[40];
static iblue_profile prof[IBLUE_PROFILES];
static int prof_cursor;
static iblue_port_info info[IBLUE_PORTS];
static iblue_map_entry map[IBLUE_MAP_MAX];
static int map_size;
static int map_loaded;   /* the table of `port` is in map[] */
static int out_cursor;
static int out_top;
static int pick_cursor;
static int port_top;
static int pair_mode;
static int opt_entry;  /* index in map[] of the button being set */
static int opt_cursor;
static int learn_frames;
static int learn_armed;
static int swallow;
static char notice[56];
static char nobody_text[56];

static void
say(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
}

static const char*
error_text(int r) {
    switch (r) {
        case IBLUE_ERR_IO: return "The memory card did not answer";
        case IBLUE_ERR_NO_BOARD: return "No iBlue found";
        case IBLUE_ERR_TIMEOUT: return "The iBlue did not answer";
        case IBLUE_ERR_PROTO: return "Bad answer from the iBlue";
        case IBLUE_RES_LOCKOUT: return "Port A needs its VMU";
        case IBLUE_RES_BAD_ARG: return "The iBlue refused that";
        case IBLUE_RES_FAILED: return "Could not save in the iBlue";
        default: return "iBlue error";
    }
}

/* Returns 1 if r is fine. A broken link ends in the "not found" screen. */
static int
check(int r) {
    if (r == 0) {
        return 1;
    }
    say(error_text(r));
    if (r < 0) {
        snprintf(nobody_text, sizeof(nobody_text), "%s", error_text(r));
        screen = SC_NOBOARD;
    }
    return 0;
}

static int
refresh_port(void) {
    return check(iblue_link_port_info(&lk, port, &info[port]));
}

static int
load_map(void) {
    if (!map_loaded) {
        if (!check(iblue_link_get_map(&lk, port, map, &map_size))) {
            return 0;
        }
        map_loaded = 1;
    }
    return 1;
}

static void
build_rows(void) {
    row_count = 0;
    rows[row_count++] = ROW_PORT;
    rows[row_count++] = ROW_TYPE;
    rows[row_count++] = ROW_VMU;
    rows[row_count++] = ROW_RUMBLE;
    rows[row_count++] = ROW_REMAP;
    rows[row_count++] = ROW_PROFILES;
    if (!game_mode) {
        rows[row_count++] = ROW_PAIR; /* not a per game setting */
    }
    rows[row_count++] = ROW_RESET;
    if (game_mode) {
        rows[row_count++] = ROW_GAMEDEL;
    }
    rows[row_count++] = ROW_SAVE;
    rows[row_count++] = ROW_DISCARD;
}

/* id NULL or "": the normal settings. Otherwise the settings of that game. */
static void
open_common(const iblue_io* io, const char* id, const char* name) {
    int r;
    done = 0;
    port = 0;
    cursor = 0;
    port_top = 0;
    pair_mode = 0;
    map_loaded = 0;
    game_mode = id && id[0];
    game_has = 0;
    snprintf(game_id, sizeof(game_id), "%s", game_mode ? id : "");
    snprintf(game_name, sizeof(game_name), "%s", game_mode && name ? name : "");
    build_rows();
    notice[0] = '\0';
    screen = SC_PORT;
    r = iblue_link_open(&lk, io);
    if (r) {
        check(r);
        if (r == IBLUE_ERR_NO_BOARD) {
            snprintf(nobody_text, sizeof(nobody_text), "No iBlueControlMod found");
        }
        screen = SC_NOBOARD;
        return;
    }
    /* Edits go to the normal settings, unless it is a game's: a game that was started earlier
     * is still set in the iBlue. Older firmware does not know the command: ignored. */
    iblue_link_game_set(&lk, game_id, &game_has);
    for (int p = 0; p < IBLUE_PORTS; p++) {
        if (!check(iblue_link_port_info(&lk, p, &info[p]))) {
            return;
        }
    }
    check(iblue_link_get_pairing(&lk, &pair_mode));
}

void
uib_open(const iblue_io* io) {
    open_common(io, NULL, NULL);
}

void
uib_open_game(const iblue_io* io, const char* product, const char* name) {
    open_common(io, product, name);
}

void
uib_prepare_launch(const iblue_io* io, const char* product) {
    iblue_link l;
    if (product && product[0] && iblue_link_open(&l, io) == 0) {
        iblue_link_game_set(&l, product, NULL); /* its own settings if it has some, else the normal ones */
    }
}

int
uib_done(void) {
    return done;
}

const char*
uib_notice(void) {
    return notice;
}

/* ---- Port screen ------------------------------------------------------------------------- */

static void
change_port(int delta) {
    port = (port + delta + IBLUE_PORTS) % IBLUE_PORTS;
    map_loaded = 0;
    refresh_port();
}

static uib_sound
step_type(int delta) {
    int v = (info[port].dev_mode + delta + 4) % 4;
    if (!check(iblue_link_set_type(&lk, port, v, info[port].acc_mode))) {
        return UIB_SND_ERROR;
    }
    map_loaded = 0;
    refresh_port();
    return UIB_SND_CONFIRM;
}

static uib_sound
toggle_acc(int bit) {
    int v = info[port].acc_mode ^ bit;
    if (port == 0 && !(v & 1)) { /* port A keeps the VMU: the settings channel lives in it */
        say("Port A needs its VMU");
        return UIB_SND_ERROR;
    }
    if (!check(iblue_link_set_type(&lk, port, info[port].dev_mode, v))) {
        return UIB_SND_ERROR;
    }
    refresh_port();
    return UIB_SND_CONFIRM;
}

static uib_sound
toggle_pairing(void) {
    int v = !pair_mode;
    if (!check(iblue_link_set_pairing(&lk, v))) {
        return UIB_SND_ERROR;
    }
    pair_mode = v;
    return UIB_SND_CONFIRM;
}

static uib_sound
leave(int save) {
    int r;
    if (game_mode) {
        r = save ? iblue_link_game_save(&lk) : 0;
        if (r == 0) {
            r = iblue_link_game_set(&lk, "", NULL); /* back to the normal settings (drops unsaved edits) */
        }
    } else {
        r = save ? iblue_link_commit(&lk) : (lk.unsaved ? iblue_link_revert(&lk) : 0);
    }
    if (!check(r)) {
        return UIB_SND_ERROR;
    }
    say(save ? "Saved" : "");
    done = 1;
    return save ? UIB_SND_CONFIRM : UIB_SND_CANCEL;
}

static uib_sound
open_profiles(void) {
    if (!check(iblue_link_profiles(&lk, prof))) {
        return UIB_SND_ERROR;
    }
    prof_cursor = 0;
    screen = SC_PROF;
    return UIB_SND_ENTER;
}

static uib_sound
profile_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (prof_cursor > 0) { prof_cursor--; return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (prof_cursor < IBLUE_PROFILES - 1) { prof_cursor++; return UIB_SND_CURSOR; }
            break;
        case BTN_A:
        case BTN_START: {
            int r = iblue_link_profile_load(&lk, prof_cursor, port);
            if (r == IBLUE_RES_EMPTY) {
                say("That profile is empty");
                return UIB_SND_ERROR;
            }
            if (!check(r)) return UIB_SND_ERROR;
            map_loaded = 0;
            refresh_port();
            say("Loaded (not saved yet)");
            return UIB_SND_CONFIRM;
        }
        case BTN_X:
            if (!check(iblue_link_profile_save(&lk, prof_cursor, port))) return UIB_SND_ERROR;
            if (!check(iblue_link_profiles(&lk, prof))) return UIB_SND_ERROR;
            say("Profile saved");
            return UIB_SND_CONFIRM;
        case BTN_Y:
            if (!prof[prof_cursor].used) return UIB_SND_NONE;
            if (!check(iblue_link_profile_delete(&lk, prof_cursor))) return UIB_SND_ERROR;
            if (!check(iblue_link_profiles(&lk, prof))) return UIB_SND_ERROR;
            say("Profile deleted");
            return UIB_SND_CONFIRM;
        case BTN_B:
            screen = SC_PORT;
            return UIB_SND_CANCEL;
        default: break;
    }
    return UIB_SND_NONE;
}

static void
refresh_port_n(int p) {
    check(iblue_link_port_info(&lk, p, &info[p]));
}

static void
port_scroll(void) {
    if (cursor < port_top) port_top = cursor;
    if (cursor >= port_top + VISIBLE) port_top = cursor - VISIBLE + 1;
}

static uib_sound
port_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (cursor > 0) { cursor--; port_scroll(); return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (cursor < row_count - 1) { cursor++; port_scroll(); return UIB_SND_CURSOR; }
            break;
        case BTN_LEFT:
        case BTN_RIGHT: {
            int d = b == BTN_LEFT ? -1 : 1;
            int kind = rows[cursor];
            if (kind == ROW_PORT) { change_port(d); return screen == SC_NOBOARD ? UIB_SND_ERROR : UIB_SND_CURSOR; }
            if (kind == ROW_TYPE) return step_type(d);
            if (kind == ROW_VMU) return toggle_acc(1);
            if (kind == ROW_RUMBLE) return toggle_acc(2);
            if (kind == ROW_PAIR) return toggle_pairing();
            break;
        }
        case BTN_A:
        case BTN_START:
            switch (rows[cursor]) {
                case ROW_PORT: change_port(1); return UIB_SND_CURSOR;
                case ROW_TYPE: return step_type(1);
                case ROW_VMU: return toggle_acc(1);
                case ROW_RUMBLE: return toggle_acc(2);
                case ROW_PAIR: return toggle_pairing();
                case ROW_PROFILES: return open_profiles();
                case ROW_GAMEDEL:
                    if (!check(iblue_link_game_delete(&lk))) return UIB_SND_ERROR;
                    game_has = 0;
                    map_loaded = 0;
                    for (int p = 0; p < IBLUE_PORTS; p++) refresh_port_n(p);
                    say("Game settings removed");
                    return UIB_SND_CONFIRM;
                case ROW_REMAP:
                    if (info[port].dev_mode > 1) {
                        say("Remap is for controllers");
                        return UIB_SND_ERROR;
                    }
                    if (!load_map()) return UIB_SND_ERROR;
                    screen = SC_REMAP;
                    return UIB_SND_ENTER;
                case ROW_RESET:
                    if (!check(iblue_link_reset_map(&lk, port))) return UIB_SND_ERROR;
                    map_loaded = 0;
                    refresh_port();
                    say("Buttons reset (not saved yet)");
                    return UIB_SND_CONFIRM;
                case ROW_SAVE: return leave(1);
                case ROW_DISCARD: return leave(0);
            }
            break;
        case BTN_B:
            if (lk.unsaved) {
                screen = SC_LEAVE;
                return UIB_SND_ENTER;
            }
            return leave(0);
        default: break;
    }
    return UIB_SND_NONE;
}

/* ---- Remap screen ------------------------------------------------------------------------- */

static void
keep_visible(void) {
    if (out_cursor < out_top) out_top = out_cursor;
    if (out_cursor >= out_top + VISIBLE) out_top = out_cursor - VISIBLE + 1;
}

static int
send_range(int first, int count) {
    if (count <= 0) {
        return 1;
    }
    return check(iblue_link_set_map(&lk, port, first, count, &map[first], map_size));
}

static uib_sound
assign_source(int src) {
    int first, count;
    if (iblue_assign(map, &map_size, port, src, out_cursor, &first, &count) != 0) {
        say("The button table is full");
        return UIB_SND_ERROR;
    }
    if (!send_range(first, count)) {
        map_loaded = 0;
        return UIB_SND_ERROR;
    }
    return UIB_SND_CONFIRM;
}

static uib_sound
open_pick(void) {
    int ids[4];
    pick_cursor = iblue_sources_of(map, map_size, port, out_cursor, ids, 4) > 0 && ids[0] < 32 ? ids[0] : 0;
    screen = SC_PICK;
    return UIB_SND_ENTER;
}

static uib_sound
open_opts(void) {
    int ids[1];
    if (iblue_sources_of(map, map_size, port, out_cursor, ids, 1) == 0) {
        say("No button does this yet");
        return UIB_SND_ERROR;
    }
    for (int i = 0; i < map_size; i++) {
        if (map[i].src_btn == ids[0] && map[i].dst_id == port) {
            opt_entry = i;
        }
    }
    opt_cursor = 0;
    screen = SC_OPTS;
    return UIB_SND_ENTER;
}

static int
index_of(const uint8_t* values, int n, int v) {
    for (int i = 0; i < n; i++) {
        if (values[i] == v) return i;
    }
    return -1;
}

static int
step_clamped(int v, int delta, int step, int max) {
    v += delta * step;
    return v < 0 ? 0 : (v > max ? max : v);
}

static uib_sound
opts_button(button_t b) {
    iblue_map_entry* e = &map[opt_entry];
    int d = 0;
    switch (b) {
        case BTN_UP:
            if (opt_cursor > 0) { opt_cursor--; return UIB_SND_CURSOR; }
            return UIB_SND_NONE;
        case BTN_DOWN:
            if (opt_cursor < OPTS - 1) { opt_cursor++; return UIB_SND_CURSOR; }
            return UIB_SND_NONE;
        case BTN_B:
            screen = SC_REMAP;
            return UIB_SND_CANCEL;
        case BTN_LEFT: d = -1; break;
        case BTN_RIGHT:
        case BTN_A: d = 1; break;
        default: return UIB_SND_NONE;
    }
    switch (opt_cursor) {
        case OPT_SCALE: e->perc_max = (uint8_t)step_clamped(e->perc_max, d, 5, 100); break;
        case OPT_THRESHOLD: e->perc_threshold = (uint8_t)step_clamped(e->perc_threshold, d, 5, 100); break;
        case OPT_DEADZONE: e->perc_deadzone = (uint8_t)step_clamped(e->perc_deadzone, d, 5, 255); break;
        case OPT_TURBO: {
            int i = index_of(turbo_values, TURBO_COUNT, e->turbo);
            e->turbo = turbo_values[(i + d + TURBO_COUNT) % TURBO_COUNT]; /* i -1 (custom) goes to Off/Very slow */
            break;
        }
        case OPT_ALGO: {
            int i = index_of(algo_values, 2, e->algo & 0xF);
            e->algo = algo_values[(i + d + 2) % 2];
            break;
        }
    }
    if (!send_range(opt_entry, 1)) {
        map_loaded = 0;
        return UIB_SND_ERROR;
    }
    return UIB_SND_CONFIRM;
}

static uib_sound
remap_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (out_cursor > 0) { out_cursor--; keep_visible(); return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (out_cursor < IBLUE_OUTPUTS - 1) { out_cursor++; keep_visible(); return UIB_SND_CURSOR; }
            break;
        case BTN_START:
            return open_opts();
        case BTN_A:
            if (!info[port].connected) {
                return open_pick(); /* no controller to press a button on: choose from the list */
            }
            screen = SC_LEARN;
            learn_frames = 0;
            learn_armed = 0;
            return UIB_SND_ENTER;
        case BTN_X:
            return open_pick();
        case BTN_Y: {
            int first, count;
            iblue_clear_output(map, map_size, port, out_cursor, &first, &count);
            if (count == 0) return UIB_SND_NONE;
            return send_range(first, count) ? UIB_SND_CONFIRM : UIB_SND_ERROR;
        }
        case BTN_B:
            screen = SC_PORT;
            return UIB_SND_CANCEL;
        default: break;
    }
    return UIB_SND_NONE;
}

static uib_sound
pick_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (pick_cursor > 0) { pick_cursor--; return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (pick_cursor < 31) { pick_cursor++; return UIB_SND_CURSOR; }
            break;
        case BTN_A:
        case BTN_START: {
            uib_sound s = assign_source(pick_cursor);
            if (screen == SC_PICK) screen = SC_REMAP;
            return s;
        }
        case BTN_B:
            screen = SC_REMAP;
            return UIB_SND_CANCEL;
        default: break;
    }
    return UIB_SND_NONE;
}

/* ---- Buttons / tick ----------------------------------------------------------------------- */

uib_sound
uib_button(button_t b) {
    if (b == BTN_NONE) {
        return UIB_SND_NONE;
    }
    if (swallow > 0) {
        return UIB_SND_NONE;
    }
    notice[0] = '\0';
    switch (screen) {
        case SC_NOBOARD:
            if (b == BTN_B || b == BTN_A) {
                done = 1;
                return UIB_SND_CANCEL;
            }
            return UIB_SND_NONE;
        case SC_PORT: return port_button(b);
        case SC_REMAP: return remap_button(b);
        case SC_PICK: return pick_button(b);
        case SC_OPTS: return opts_button(b);
        case SC_PROF: return profile_button(b);
        case SC_LEARN: return UIB_SND_NONE; /* the pressed buttons are what is being learned */
        case SC_LEAVE:
            if (b == BTN_A || b == BTN_START) {
                screen = SC_PORT;
                return leave(1);
            }
            if (b == BTN_X) {
                screen = SC_PORT;
                return leave(0);
            }
            if (b == BTN_B) {
                screen = SC_PORT;
                return UIB_SND_CANCEL;
            }
            return UIB_SND_NONE;
    }
    return UIB_SND_NONE;
}

void
uib_tick(void) {
    if (swallow > 0) {
        swallow--;
    }
    if (screen != SC_LEARN) {
        return;
    }
    learn_frames++;
    if (learn_frames > LEARN_TIMEOUT_FRAMES) {
        screen = SC_REMAP;
        say("Nothing pressed");
        swallow = SWALLOW_FRAMES;
        return;
    }
    if (learn_frames % LEARN_POLL_FRAMES) {
        return;
    }
    {
        uint32_t w[4];
        int connected = 0;
        if (!check(iblue_link_live(&lk, port, w, &connected))) {
            return;
        }
        if (!connected) {
            screen = SC_REMAP;
            say("The controller went away");
            return;
        }
        if (!w[0]) {
            learn_armed = 1; /* ignore what was held when the screen opened */
            return;
        }
        if (learn_armed) {
            int src = 0;
            while (!(w[0] & (1u << src))) src++;
            screen = SC_REMAP;
            assign_source(src);
            swallow = SWALLOW_FRAMES;
        }
    }
}

/* ---- Drawing ------------------------------------------------------------------------------ */

#define WHITE 0xFFFFFFFFu
#define GREY 0xFFA0A0A0u

static void
row_line(int i, int selected, const char* label, const char* value) {
    float y = LIST_Y + (float)i * GFX_LINE_H;
    if (selected) {
        gfx_rect(LEFT_X - 8.0f, y, 520.0f, (float)GFX_LINE_H, 0.55f, 0x60FFFFFFu);
    }
    gfx_text(label, LEFT_X, y, 0.6f, selected ? WHITE : 0xFFC0C0C0u, selected);
    if (value && value[0]) {
        gfx_text(value, VALUE_X, y, 0.6f, selected ? WHITE : 0xFFC0C0C0u, selected);
    }
}

static void
draw_hint(const char* text) {
    gfx_text(text, LEFT_X, 376.0f, 0.6f, GREY, 0);
}

static void
draw_port(void) {
    char line[64];
    char value[40];
    char label[28];
    const char* hint = "";
    const char* shown = "";
    gfx_text(game_mode ? "Game settings" : "Controllers", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    for (int p = 0; p < IBLUE_PORTS; p++) {
        float x = LEFT_X + 150.0f + (float)p * 56.0f;
        gfx_rrect(x, 62.0f, 48.0f, 28.0f, 6.0f, 0.5f, p == port ? 0xC0FFFFFFu : 0x60000000u);
        snprintf(line, sizeof(line), "%c", 'A' + p);
        gfx_text(line, x + 18.0f, 62.0f, 0.6f, p == port ? 0xFF102050u : (info[p].connected ? WHITE : GREY), 0);
    }
    if (game_mode) {
        snprintf(line, sizeof(line), "%.24s%s", game_name, game_has ? " *" : "");
    } else {
        snprintf(line, sizeof(line), "%s", info[port].connected ? info[port].name : "no Bluetooth controller");
    }
    gfx_text(line, LEFT_X, 94.0f, 0.6f, (game_mode || info[port].connected) ? WHITE : GREY, 0);

    for (int i = port_top; i < row_count && i < port_top + VISIBLE; i++) {
        int sel = cursor == i, row = i - port_top;
        value[0] = '\0';
        snprintf(label, sizeof(label), "?");
        switch (rows[i]) {
            case ROW_PORT:
                snprintf(label, sizeof(label), "Port");
                snprintf(value, sizeof(value), "< %c >%s", 'A' + port, info[port].plugged ? "  pad plugged in" : "");
                break;
            case ROW_TYPE:
                snprintf(label, sizeof(label), "Type");
                snprintf(value, sizeof(value), "%s", dev_names[info[port].dev_mode & 3]);
                hint = "Dual stick: second stick shown to games";
                break;
            case ROW_VMU:
                snprintf(label, sizeof(label), "Memory card (VMU)");
                snprintf(value, sizeof(value), "%s", (info[port].acc_mode & 1) ? "On" : "Off");
                hint = port == 0 ? "Port A needs its VMU" : "Memory card in this port";
                break;
            case ROW_RUMBLE:
                snprintf(label, sizeof(label), "Rumble pack");
                snprintf(value, sizeof(value), "%s", (info[port].acc_mode & 2) ? "On" : "Off");
                hint = "Vibration on this port";
                break;
            case ROW_REMAP:
                snprintf(label, sizeof(label), "Remap buttons");
                hint = "Which controller button does what";
                break;
            case ROW_PROFILES:
                snprintf(label, sizeof(label), "Profiles");
                hint = "Home + face button on the controller";
                break;
            case ROW_GAMEDEL:
                snprintf(label, sizeof(label), "Remove game settings");
                snprintf(value, sizeof(value), "%s", game_has ? "" : "(none)");
                hint = "This game uses the normal settings";
                break;
            case ROW_PAIR:
                snprintf(label, sizeof(label), "Bluetooth pairing");
                snprintf(value, sizeof(value), "%s", pair_mode ? "Manual" : "Auto");
                hint = "Auto: search by itself. Manual: button";
                break;
            case ROW_RESET:
                snprintf(label, sizeof(label), "Reset this controller");
                hint = "Back to the default buttons";
                break;
            case ROW_SAVE:
                snprintf(label, sizeof(label), "Save and exit");
                snprintf(value, sizeof(value), "%s", lk.unsaved ? "(unsaved changes)" : "");
                hint = "Keep the changes in the iBlue";
                break;
            default:
                snprintf(label, sizeof(label), "Exit without saving");
                hint = "Throw the changes away";
                break;
        }
        row_line(row, sel, label, value);
        if (sel) {
            shown = hint;
        }
    }
    draw_hint(shown);
    if (port_top > 0) gfx_text("^", 590.0f, 100.0f, 0.6f, WHITE, 1);
    if (port_top + VISIBLE < row_count) gfx_text("v", 590.0f, 352.0f, 0.6f, WHITE, 1);
}

static void
draw_remap(void) {
    char line[64], names[48], tmp[20];
    int ids[8];
    gfx_text("Remap buttons", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    snprintf(line, sizeof(line), "Port %c", 'A' + port);
    gfx_text(line, LEFT_X, 94.0f, 0.6f, GREY, 0);
    draw_hint("A:press X:list Y:clear Start:options");
    for (int i = 0; i < VISIBLE; i++) {
        int o = out_top + i;
        if (o >= IBLUE_OUTPUTS) break;
        int n = iblue_sources_of(map, map_size, port, o, ids, 8);
        if (n == 0) {
            snprintf(names, sizeof(names), "-");
        } else if (n == 1) {
            snprintf(names, sizeof(names), "%s", iblue_source_name(ids[0], tmp, sizeof(tmp)));
        } else {
            snprintf(names, sizeof(names), "%s +%d", iblue_source_name(ids[0], tmp, sizeof(tmp)), n - 1);
        }
        row_line(i, o == out_cursor, iblue_output_name(o), names);
    }
    if (out_top > 0) gfx_text("^", 590.0f, 100.0f, 0.6f, WHITE, 1);
    if (out_top + VISIBLE < IBLUE_OUTPUTS) gfx_text("v", 590.0f, 352.0f, 0.6f, WHITE, 1);
}

static void
draw_profiles(void) {
    static const char* const keys[IBLUE_PROFILES] = {"1  Home + down", "2  Home + right", "3  Home + left", "4  Home + up"};
    char line[48], tmp[40];
    snprintf(line, sizeof(line), "Port %c", 'A' + port);
    gfx_text("Profiles", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    gfx_text(line, LEFT_X, 94.0f, 0.6f, GREY, 0);
    for (int i = 0; i < IBLUE_PROFILES; i++) {
        if (prof[i].used) {
            snprintf(tmp, sizeof(tmp), "%s%s%s", dev_names[prof[i].dev_mode & 3], (prof[i].acc_mode & 2) ? ", rumble" : "",
                     (prof[i].acc_mode & 1) ? ", VMU" : "");
        } else {
            snprintf(tmp, sizeof(tmp), "empty");
        }
        row_line(i, i == prof_cursor, keys[i], tmp);
    }
    draw_hint("A:load X:save here Y:delete");
}

static void
draw_opts(void) {
    char line[64], value[24], tmp[20];
    const iblue_map_entry* e = &map[opt_entry];
    int ti = -1, ai = -1;
    const char* hint = "";

    for (int i = 0; i < TURBO_COUNT; i++) if (turbo_values[i] == e->turbo) ti = i;
    for (int i = 0; i < 2; i++) if (algo_values[i] == (e->algo & 0xF)) ai = i;
    snprintf(line, sizeof(line), "%s: %s", iblue_output_name(out_cursor), iblue_source_name(e->src_btn, tmp, sizeof(tmp)));
    gfx_text("Button settings", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    gfx_text(line, LEFT_X, 94.0f, 0.6f, GREY, 0);
    for (int i = 0; i < OPTS; i++) {
        const char* label = "";
        value[0] = '\0';
        switch (i) {
            case OPT_SCALE:
                label = "Scale";
                snprintf(value, sizeof(value), "%d%%", e->perc_max);
                if (i == opt_cursor) hint = "Strength of a stick or trigger";
                break;
            case OPT_THRESHOLD:
                label = "Press level";
                snprintf(value, sizeof(value), "%d%%", e->perc_threshold);
                if (i == opt_cursor) hint = "How far a stick presses a button";
                break;
            case OPT_DEADZONE:
                label = "Dead zone";
                snprintf(value, sizeof(value), "%d.%02d%%", e->perc_deadzone / 100, e->perc_deadzone % 100);
                if (i == opt_cursor) hint = "Stick travel around the centre ignored";
                break;
            case OPT_TURBO:
                label = "Rapid fire";
                snprintf(value, sizeof(value), "%s", ti >= 0 ? turbo_names[ti] : "Custom");
                if (i == opt_cursor) hint = "Presses again and again while held";
                break;
            default:
                label = "Stick scaling";
                snprintf(value, sizeof(value), "%s", ai >= 0 ? algo_names[ai] : "Other");
                if (i == opt_cursor) hint = "Linear fills the range, Raw keeps it";
                break;
        }
        row_line(i, i == opt_cursor, label, value);
    }
    draw_hint(hint);
}

static void
draw_box(const char* title, const char* l1, const char* l2) {
    gfx_rect(120.0f, 140.0f, 400.0f, 130.0f, 0.7f, 0xE0102050u);
    gfx_text(title, 136.0f, 150.0f, 0.75f, WHITE, 1);
    gfx_text(l1, 136.0f, 188.0f, 0.75f, WHITE, 0);
    gfx_text(l2, 136.0f, 220.0f, 0.75f, GREY, 0);
}

void
uib_draw(void) {
    char a[48], b[48];
    switch (screen) {
        case SC_NOBOARD:
            gfx_text("Controllers", LEFT_X, 60.0f, 0.6f, WHITE, 1);
            gfx_text(nobody_text, LEFT_X, 130.0f, 0.6f, WHITE, 0);
            gfx_text("Needs the iBlue firmware with the", LEFT_X, 180.0f, 0.6f, GREY, 0);
            gfx_text("settings channel and the VMU on port A.", LEFT_X, 212.0f, 0.6f, GREY, 0);
            gfx_text("B: back", LEFT_X, 270.0f, 0.6f, GREY, 0);
            break;
        case SC_PORT:
        case SC_LEAVE:
            draw_port();
            if (screen == SC_LEAVE) draw_box("Unsaved changes", "A: save   X: discard", "B: keep editing");
            break;
        case SC_REMAP: draw_remap(); break;
        case SC_OPTS: draw_opts(); break;
        case SC_PROF: draw_profiles(); break;
        case SC_PICK: {
            int first = pick_cursor >= VISIBLE ? pick_cursor - VISIBLE + 1 : 0;
            draw_remap();
            gfx_rect(100.0f, 100.0f, 440.0f, 270.0f, 0.7f, 0xF0102050u);
            snprintf(a, sizeof(a), "Button for %s", iblue_output_name(out_cursor));
            gfx_text(a, 116.0f, 106.0f, 0.75f, WHITE, 1);
            for (int i = 0; i < VISIBLE; i++) {
                int s = first + i;
                float y = 140.0f + (float)i * GFX_LINE_H;
                if (s == pick_cursor) gfx_rect(108.0f, y, 424.0f, (float)GFX_LINE_H, 0.78f, 0x60FFFFFFu);
                gfx_text(iblue_source_name(s, b, sizeof(b)), 120.0f, y, 0.8f, s == pick_cursor ? WHITE : 0xFFC0C0C0u, 0);
            }
            break;
        }
        case SC_LEARN:
            draw_remap();
            snprintf(a, sizeof(a), "Press the button for %s", iblue_output_name(out_cursor));
            snprintf(b, sizeof(b), "%d s left", (LEARN_TIMEOUT_FRAMES - learn_frames) / 60 + 1);
            draw_box("Remap", a, b);
            break;
    }
    if (notice[0]) {
        gfx_text(notice, LEFT_X, 404.0f, 0.6f, WHITE, 1);
    }
}
