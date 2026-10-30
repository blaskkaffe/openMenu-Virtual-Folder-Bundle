/* ui_iblue: see ui_iblue.h. */
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "iblue_map.h"
#include "ui_iblue.h"

typedef enum { SC_NOBOARD, SC_PORT, SC_REMAP, SC_PICK, SC_LEARN, SC_LEAVE } sc_t;

enum { ROW_PORT, ROW_TYPE, ROW_ACC, ROW_REMAP, ROW_RESET, ROW_SAVE, ROW_DISCARD, ROWS };

#define LEFT_X 64.0f
#define VALUE_X 330.0f
#define LIST_Y 128.0f
#define VISIBLE 7
#define LEARN_POLL_FRAMES 5
#define LEARN_TIMEOUT_FRAMES 600
#define SWALLOW_FRAMES 20

static const char* const dev_names[] = {"Controller", "Controller (alt)", "Keyboard", "Mouse"};
static const char* const acc_names[] = {"None", "VMU", "Rumble pack", "VMU + rumble"};

static iblue_link lk;
static sc_t screen;
static int done;
static int port;
static int cursor;
static iblue_port_info info[IBLUE_PORTS];
static iblue_map_entry map[IBLUE_MAP_MAX];
static int map_size;
static int map_loaded;   /* the table of `port` is in map[] */
static int out_cursor;
static int out_top;
static int pick_cursor;
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

void
uib_open(const iblue_io* io) {
    int r;
    done = 0;
    port = 0;
    cursor = 0;
    map_loaded = 0;
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
    for (int p = 0; p < IBLUE_PORTS; p++) {
        if (!check(iblue_link_port_info(&lk, p, &info[p]))) {
            return;
        }
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
step_acc(int delta) {
    int v = info[port].acc_mode;
    for (int i = 0; i < 4; i++) {
        v = (v + delta + 4) % 4;
        if (port != 0 || (v & 1)) { /* port A keeps the VMU: the settings channel lives in it */
            break;
        }
    }
    if (!check(iblue_link_set_type(&lk, port, info[port].dev_mode, v))) {
        return UIB_SND_ERROR;
    }
    refresh_port();
    return UIB_SND_CONFIRM;
}

static uib_sound
leave(int save) {
    int r = save ? iblue_link_commit(&lk) : (lk.unsaved ? iblue_link_revert(&lk) : 0);
    if (!check(r)) {
        return UIB_SND_ERROR;
    }
    say(save ? "Saved" : "");
    done = 1;
    return save ? UIB_SND_CONFIRM : UIB_SND_CANCEL;
}

static uib_sound
port_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (cursor > 0) { cursor--; return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (cursor < ROWS - 1) { cursor++; return UIB_SND_CURSOR; }
            break;
        case BTN_LEFT:
        case BTN_RIGHT: {
            int d = b == BTN_LEFT ? -1 : 1;
            if (cursor == ROW_PORT) { change_port(d); return screen == SC_NOBOARD ? UIB_SND_ERROR : UIB_SND_CURSOR; }
            if (cursor == ROW_TYPE) return step_type(d);
            if (cursor == ROW_ACC) return step_acc(d);
            break;
        }
        case BTN_A:
        case BTN_START:
            switch (cursor) {
                case ROW_PORT: change_port(1); return UIB_SND_CURSOR;
                case ROW_TYPE: return step_type(1);
                case ROW_ACC: return step_acc(1);
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
remap_button(button_t b) {
    switch (b) {
        case BTN_UP:
            if (out_cursor > 0) { out_cursor--; keep_visible(); return UIB_SND_CURSOR; }
            break;
        case BTN_DOWN:
            if (out_cursor < IBLUE_OUTPUTS - 1) { out_cursor++; keep_visible(); return UIB_SND_CURSOR; }
            break;
        case BTN_A:
        case BTN_START:
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
draw_port(void) {
    char line[64];
    char value[40];
    gfx_text("Controllers", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    for (int p = 0; p < IBLUE_PORTS; p++) {
        float x = LEFT_X + 150.0f + (float)p * 56.0f;
        gfx_rrect(x, 62.0f, 48.0f, 28.0f, 6.0f, 0.5f, p == port ? 0xC0FFFFFFu : 0x60000000u);
        snprintf(line, sizeof(line), "%c", 'A' + p);
        gfx_text(line, x + 18.0f, 62.0f, 0.6f, p == port ? 0xFF102050u : (info[p].connected ? WHITE : GREY), 0);
    }
    snprintf(line, sizeof(line), "%s", info[port].connected ? info[port].name : "no controller connected");
    gfx_text(line, LEFT_X, 94.0f, 0.6f, info[port].connected ? WHITE : GREY, 0);

    snprintf(value, sizeof(value), "< %c >", 'A' + port);
    row_line(ROW_PORT, cursor == ROW_PORT, "Port", value);
    snprintf(value, sizeof(value), "%s", dev_names[info[port].dev_mode & 3]);
    row_line(ROW_TYPE, cursor == ROW_TYPE, "Type", value);
    snprintf(value, sizeof(value), "%s", acc_names[info[port].acc_mode & 3]);
    row_line(ROW_ACC, cursor == ROW_ACC, "Accessories", value);
    row_line(ROW_REMAP, cursor == ROW_REMAP, "Remap buttons", "");
    row_line(ROW_RESET, cursor == ROW_RESET, "Reset this controller", "");
    row_line(ROW_SAVE, cursor == ROW_SAVE, "Save and exit", lk.unsaved ? "(unsaved changes)" : "");
    row_line(ROW_DISCARD, cursor == ROW_DISCARD, "Exit without saving", "");
}

static void
draw_remap(void) {
    char line[64], names[48], tmp[20];
    int ids[8];
    gfx_text("Remap buttons", LEFT_X, 60.0f, 0.6f, WHITE, 1);
    snprintf(line, sizeof(line), "Port %c   A: press a button   X: pick from list   Y: clear", 'A' + port);
    gfx_text(line, LEFT_X, 94.0f, 0.6f, GREY, 0);
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
