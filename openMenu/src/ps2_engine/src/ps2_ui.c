/* ps2_ui: the PS2-style menu, see ps2_ui.h. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ps2_ui.h"

#define PI_F 3.14159265f

/* Timings (seconds). The PS2 BIOS counts video fields; its browser waits 30 NTSC / 25 PAL fields
 * for a transition, which is half a second, and the logo fades in over 33 / 28 fields (0.55 s). */
#define SCREEN_TRANSITION 1.1f /* measured in PCSX2: main menu <-> System Configuration fades take 1.1 s */
#define BOOT_FADE 0.55f
#define LAUNCH_TIME 0.7f
#define NOTICE_TIME 2.5f
#define EASE_RATE 12.0f

#define GRID_ROWS 4

#define COL_WHITE 0xFFFFFFFFu
#define COL_GREY 0xFFB4B4B4u
#define COL_BLUE 0xFF3CB9F0u   /* selected entry (measured: 60,185,240) */
#define COL_CYAN 0xFF3CB9F0u
#define COL_YELLOW 0xFFE8E23Cu /* screen titles (232,226,60) */
#define COL_ITEM_GREY 0xFF808080u
#define COL_VALUE 0xFFE8E8E8u

/* The reference frames are 640x512 (PAL); this screen is 640x480. */
#define Y(v) ((v) * 0.9375f)
#define TEXT_TOP 21.0f /* distance from the top of a font cell to the middle of the capitals, at scale 1 */

typedef enum { MAIN_BROWSER, MAIN_SETTINGS, MAIN_COUNT } main_item;
typedef enum { DEV_CARD1, DEV_CARD2, DEV_DISC, DEV_COUNT } dev_item;

static float
clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float
ease_to(float cur, float target, float dt) {
    float d = target - cur;
    if (fabsf(d) < 0.0015f) {
        return target;
    }
    return cur + d * (1.0f - expf(-dt * EASE_RATE));
}

static float
smooth(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void
sfx(ps2_ui* ui, int s) {
    if (ui->host.sfx) {
        ui->host.sfx(ui->host.user, s);
    }
}

/* ---- Set-up ----------------------------------------------------------------------------- */

int
ps2_ui_init(ps2_ui* ui, const ps2_pack* pack, const ps2_host* host, const ps2_sink* sink) {
    memset(ui, 0, sizeof(*ui));
    ui->pack = pack;
    ui->host = *host;
    ps2d_init(&ui->draw, pack, sink);
    static const char* const fog[5] = {"OFOG0", "OFOG1", "OFOG2", "OFOG3", "OFOG4"};
    for (int i = 0; i < 5; i++) {
        ui->id.fog[i] = ps2_pack_find_tex(pack, fog[i]);
    }
    ui->id.flare = ps2_pack_find_tex(pack, "OFLAR");
    ui->id.ref = ps2_pack_find_tex(pack, "OREF");
    ui->id.blur = ps2_pack_find_tex(pack, "CBLUR");
    ui->id.smoke = ps2_pack_find_tex(pack, "CSMOK");
    ui->id.glow1 = ps2_pack_find_tex(pack, "BNAV1");
    ui->id.glow2 = ps2_pack_find_tex(pack, "BNAV2");
    ui->id.ring = ps2_pack_find_tex(pack, "OCRLE");
    ui->id.btn = ps2_pack_find_tex(pack, "BBTTN");
    ui->id.arrow = ps2_pack_find_tex(pack, "BARRW");
    ui->id.kabe = ps2_pack_find_tex(pack, "CKABE");
    ui->id.wall = ps2_pack_find_tex(pack, "OWAL0");
    ui->id.m_disc = ps2_pack_find_model(pack, "MBPS2D");
    ui->id.m_card = ps2_pack_find_model(pack, "MBPS2M");
    ui->id.m_cube = ps2_pack_find_model(pack, "MBFNOR");
    ui->id.m_system = ps2_pack_find_model(pack, "MBYSYS");
    ui->id.t_disc = ps2_pack_find_tex(pack, "TBPS2D");
    ui->id.t_card = ps2_pack_find_tex(pack, "TBPS2M");
    ui->id.t_cube = ps2_pack_find_tex(pack, "TBFNOR");
    ui->id.t_folder = ps2_pack_find_tex(pack, "TBFSCE");
    ui->id.t_system = ps2_pack_find_tex(pack, "TBYSYS");
    if (ui->id.m_disc < 0 || ui->id.m_card < 0 || ui->id.m_cube < 0 || ui->id.m_system < 0) {
        return -1;
    }
    ui->screen = PS2_SCREEN_MAIN;
    ui->dev_sel = DEV_DISC;
    ui->dev_pos = (float)DEV_DISC;
    return 0;
}

/* The main menu is on black; the other screens paint their own backgrounds. */
void
ps2_ui_background(uint32_t* top, uint32_t* bottom) {
    *top = 0xFF000000;
    *bottom = 0xFF000000;
}

/* ---- Screen changes --------------------------------------------------------------------- */

static void
go(ps2_ui* ui, int screen) {
    if (ui->transitioning) {
        return;
    }
    ui->pending_screen = screen;
    ui->transitioning = 1;
    ui->trans = SCREEN_TRANSITION / 2.0f;
}

static void
arrive(ps2_ui* ui) {
    ui->screen = ui->pending_screen;
    if (ui->screen == PS2_SCREEN_GAMES) {
        if (ui->host.game_opened) {
            ui->host.game_opened(ui->host.user);
        }
        int cursor = ui->host.game_cursor ? ui->host.game_cursor(ui->host.user) : 0;
        ui->grid_target = cursor / PS2_GRID_COLS > GRID_ROWS - 1 ? cursor / PS2_GRID_COLS - (GRID_ROWS - 1) : 0;
        ui->grid_top = (float)ui->grid_target;
        ui->game_count_seen = -1;
    } else if (ui->screen == PS2_SCREEN_SETTINGS) {
        ui->settings_cursor = 0;
        ui->settings_pos = 0.0f;
        ui->cfg_prev = 0;
        ui->cfg_fade = 1.0f;
        ui->cfg_hide = 0;
        ui->cfg_hidden = 0.0f;
    }
}

void
ps2_ui_show(ps2_ui* ui, int screen) {
    ui->pending_screen = screen;
    ui->transitioning = 0;
    ui->boot_t = BOOT_FADE;
    arrive(ui);
}

/* ---- Input ------------------------------------------------------------------------------ */

static void
notice(ps2_ui* ui, const char* text) {
    snprintf(ui->notice, sizeof(ui->notice), "%s", text);
    ui->notice_t = NOTICE_TIME;
}

static void
button_main(ps2_ui* ui, ps2_btn b) {
    if (b == PS2_BTN_UP && ui->main_sel > 0) {
        ui->main_sel--;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_DOWN && ui->main_sel < MAIN_COUNT - 1) {
        ui->main_sel++;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_A || b == PS2_BTN_START) {
        sfx(ui, PS2_SFX_ENTER);
        go(ui, ui->main_sel == MAIN_BROWSER ? PS2_SCREEN_DEVICES : PS2_SCREEN_SETTINGS);
    } else if (b == PS2_BTN_Y) {
        sfx(ui, PS2_SFX_ENTER);
        go(ui, PS2_SCREEN_VERSION);
    }
}

static void
button_version(ps2_ui* ui, ps2_btn b) {
    if (b == PS2_BTN_B) {
        sfx(ui, PS2_SFX_CANCEL);
        go(ui, PS2_SCREEN_MAIN);
    }
}

static void
button_devices(ps2_ui* ui, ps2_btn b) {
    if (b == PS2_BTN_LEFT && ui->dev_sel > 0) {
        ui->dev_sel--;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_RIGHT && ui->dev_sel < DEV_COUNT - 1) {
        ui->dev_sel++;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_A || b == PS2_BTN_START) {
        sfx(ui, PS2_SFX_ENTER);
        if (ui->dev_sel == DEV_DISC) {
            go(ui, PS2_SCREEN_GAMES);
        } else {
            ui->card_slot = ui->dev_sel == DEV_CARD1 ? 0 : 1;
            go(ui, PS2_SCREEN_CARD);
        }
    } else if (b == PS2_BTN_B) {
        sfx(ui, PS2_SFX_CANCEL);
        go(ui, PS2_SCREEN_MAIN);
    }
}

static void
button_card(ps2_ui* ui, ps2_btn b) {
    if (b == PS2_BTN_B) {
        sfx(ui, PS2_SFX_CANCEL);
        go(ui, PS2_SCREEN_DEVICES);
    }
}

static void
button_recent(ps2_ui* ui, ps2_btn b) {
    int n = ui->host.recent_count ? ui->host.recent_count(ui->host.user) : 0;
    if (b == PS2_BTN_UP && ui->recent_sel > 0) {
        ui->recent_sel--;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_DOWN && ui->recent_sel < n - 1) {
        ui->recent_sel++;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_A || b == PS2_BTN_START) {
        sfx(ui, PS2_SFX_ENTER);
        ui->recent_open = 0;
        if (ui->host.recent_launch) {
            ui->host.recent_launch(ui->host.user, ui->recent_sel); /* only returns if it failed */
            sfx(ui, PS2_SFX_ERROR);
            notice(ui, "Could not start the game");
        }
    } else if (b == PS2_BTN_B || b == PS2_BTN_X) {
        sfx(ui, PS2_SFX_CANCEL);
        ui->recent_open = 0;
    }
}

static void
button_games(ps2_ui* ui, ps2_btn b) {
    if (ui->launching) {
        return;
    }
    if (ui->recent_open) {
        button_recent(ui, b);
        return;
    }
    if (b == PS2_BTN_X) {
        if (ui->host.recent_count && ui->host.recent_count(ui->host.user) > 0) {
            ui->recent_open = 1;
            ui->recent_sel = 0;
            sfx(ui, PS2_SFX_ENTER);
        } else {
            sfx(ui, PS2_SFX_ERROR);
            notice(ui, "No recently played games");
        }
        return;
    }
    int nav;
    switch (b) {
        case PS2_BTN_LEFT: nav = PS2_NAV_PREV; break;
        case PS2_BTN_RIGHT: nav = PS2_NAV_NEXT; break;
        case PS2_BTN_UP: nav = PS2_NAV_ROW_PREV; break;
        case PS2_BTN_DOWN: nav = PS2_NAV_ROW_NEXT; break;
        case PS2_BTN_Y: nav = PS2_NAV_DISC; break;
        case PS2_BTN_A:
        case PS2_BTN_START: nav = PS2_NAV_ENTER; break;
        case PS2_BTN_B: nav = PS2_NAV_BACK; break;
        default: return;
    }
    int r = ui->host.game_nav ? ui->host.game_nav(ui->host.user, nav) : PS2_NAVR_NONE;
    if (r == PS2_NAVR_LAUNCH) {
        sfx(ui, PS2_SFX_ENTER);
        ui->launching = 1;
        ui->launch_t = ui->host.launch_anim && !ui->host.launch_anim(ui->host.user) ? LAUNCH_TIME : 0.0f;
    } else if (r == PS2_NAVR_EXIT) {
        sfx(ui, PS2_SFX_CANCEL);
        go(ui, PS2_SCREEN_DEVICES);
    } else if (r == PS2_NAVR_CHANGED) {
        sfx(ui, nav == PS2_NAV_ENTER ? PS2_SFX_CONFIRM : (nav == PS2_NAV_BACK ? PS2_SFX_CANCEL : PS2_SFX_CURSOR));
    }
}

/* The settings are a ring of cubes: left/right turn it, up/down (and A) change the value. */
static void
button_settings(ps2_ui* ui, ps2_btn b) {
    int n = ui->host.set_count ? ui->host.set_count(ui->host.user) : 0;
    int row = ui->settings_cursor;
    if (b == PS2_BTN_LEFT || b == PS2_BTN_RIGHT) {
        int next = row + (b == PS2_BTN_LEFT ? -1 : 1);
        if (next >= 0 && next < n) {
            ui->cfg_prev = row;
            ui->cfg_fade = 0.0f;
            ui->settings_cursor = next;
            sfx(ui, PS2_SFX_CURSOR);
        }
    } else if (n > 0 && (b == PS2_BTN_UP || b == PS2_BTN_DOWN || b == PS2_BTN_A)) {
        ui->host.set_change(ui->host.user, row, b == PS2_BTN_UP ? -1 : 1);
        sfx(ui, PS2_SFX_CONFIRM);
    } else if (b == PS2_BTN_X) { /* square: Display, hides the text and shows the bare scene */
        ui->cfg_hide = !ui->cfg_hide;
        sfx(ui, PS2_SFX_CONFIRM);
    } else if (b == PS2_BTN_B || b == PS2_BTN_START) {
        if (ui->host.set_leave && ui->host.set_leave(ui->host.user) != 0) {
            sfx(ui, PS2_SFX_ERROR);
            notice(ui, "Could not save settings");
        } else {
            sfx(ui, PS2_SFX_CANCEL);
        }
        go(ui, PS2_SCREEN_MAIN);
    }
}

void
ps2_ui_button(ps2_ui* ui, ps2_btn b) {
    if (b == PS2_BTN_NONE || ui->transitioning || ui->boot_t < BOOT_FADE) {
        return;
    }
    switch (ui->screen) {
        case PS2_SCREEN_MAIN: button_main(ui, b); break;
        case PS2_SCREEN_DEVICES: button_devices(ui, b); break;
        case PS2_SCREEN_GAMES: button_games(ui, b); break;
        case PS2_SCREEN_CARD: button_card(ui, b); break;
        case PS2_SCREEN_SETTINGS: button_settings(ui, b); break;
        case PS2_SCREEN_VERSION: button_version(ui, b); break;
        default: break;
    }
}

/* ---- Update ----------------------------------------------------------------------------- */

void
ps2_ui_update(ps2_ui* ui, float dt) {
    if (dt > 0.1f) {
        dt = 0.1f; /* after a stall do not fast-forward */
    }
    ui->time += dt;
    if (ui->boot_t < BOOT_FADE) {
        ui->boot_t += dt;
    }
    if (ui->transitioning) {
        ui->trans -= dt;
        if (ui->trans <= 0.0f) {
            if (ui->transitioning == 1) {
                arrive(ui);
                ui->transitioning = 2;
                ui->trans = SCREEN_TRANSITION / 2.0f;
            } else {
                ui->transitioning = 0;
                ui->trans = 0.0f;
            }
        }
    }
    ui->main_pos = ease_to(ui->main_pos, (float)ui->main_sel, dt);
    ui->dev_pos = ease_to(ui->dev_pos, (float)ui->dev_sel, dt);
    ui->settings_pos = ease_to(ui->settings_pos, (float)ui->settings_cursor, dt);
    ui->cfg_fade = clampf(ui->cfg_fade + dt / 0.4f, 0.0f, 1.0f);
    ui->cfg_hidden = clampf(ui->cfg_hidden + (ui->cfg_hide ? dt : -dt), 0.0f, 1.0f); /* about 1 s, measured */
    if (ui->screen == PS2_SCREEN_GAMES && ui->host.game_cursor) {
        int count = ui->host.game_count(ui->host.user);
        char folder[64] = "";
        if (ui->host.game_folder) {
            ui->host.game_folder(ui->host.user, folder, sizeof(folder));
        }
        int cursor = ui->host.game_cursor(ui->host.user);
        int row = cursor / PS2_GRID_COLS;
        if (row < ui->grid_target) {
            ui->grid_target = row;
        } else if (row > ui->grid_target + GRID_ROWS - 1) {
            ui->grid_target = row - (GRID_ROWS - 1);
        }
        if (count != ui->game_count_seen || strcmp(folder, ui->game_folder_seen) != 0) {
            /* another list (a folder was entered or left): no sweep */
            ui->grid_target = row > GRID_ROWS - 1 ? row - (GRID_ROWS - 1) : 0;
            ui->grid_top = (float)ui->grid_target;
            ui->game_count_seen = count;
            snprintf(ui->game_folder_seen, sizeof(ui->game_folder_seen), "%s", folder);
        }
        ui->grid_top = ease_to(ui->grid_top, (float)ui->grid_target, dt);
    }
    if (ui->launching) {
        ui->launch_t += dt;
        if (ui->launch_t >= LAUNCH_TIME) {
            ui->launching = 0;
            ui->launch_t = 0.0f;
            if (ui->host.game_launch) {
                ui->host.game_launch(ui->host.user); /* only returns if the launch failed */
                sfx(ui, PS2_SFX_ERROR);
                notice(ui, "Could not start the game");
            }
        }
    }
    if (ui->notice_t > 0.0f) {
        ui->notice_t -= dt;
    }
    ui->clock_t -= dt;
    if (ui->clock_t <= 0.0f && ui->host.clock_text) {
        ui->host.clock_text(ui->host.user, ui->clock, sizeof(ui->clock));
        ui->clock_t = 1.0f;
    }
}

/* ---- Drawing: pieces used by several screens -------------------------------------------- */

/* A soft glowing ball: a small bright core inside a blue halo */
static void
draw_orb(ps2_ui* ui, float x, float y, float size, float bright) {
    ps2_draw* d = &ui->draw;
    ps2d_sprite(d, ui->id.glow2, x, y, size * 5.0f, size * 5.0f, 0.0f, ps2_argb((int)(110.0f * bright), 60, 100, 255), PS2_BLEND_ADD);
    ps2d_sprite(d, ui->id.glow2, x, y, size * 2.6f, size * 2.6f, 0.0f, ps2_argb((int)(230.0f * bright), 170, 200, 255), PS2_BLEND_ADD);
    ps2d_sprite(d, ui->id.glow2, x, y, size * 1.1f, size * 1.1f, 0.0f, ps2_argb((int)(255.0f * bright), 255, 255, 255), PS2_BLEND_ADD);
}

/* The pad symbols of the PS2; the sheet holds square, triangle (top row), cross, circle. */
enum { SYM_CROSS, SYM_CIRCLE, SYM_SQUARE, SYM_TRIANGLE };

static void
draw_symbol(ps2_ui* ui, int which, float x, float y, float size) {
    static const float qu[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    static const float qv[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    ps2d_quad(&ui->draw, ui->id.btn, x, y, x + size, y + size, qu[which], qv[which], qu[which] + 0.5f, qv[which] + 0.5f, COL_WHITE,
              PS2_BLEND_ALPHA);
}

typedef struct {
    int glyph;
    const char* text;
    float x; /* left edge of the symbol; 0 = place it centred with the others */
} hint_t;

/* Text whose capitals are centred on y (the reference measurements are centres) */
static void
text_at(ps2_ui* ui, const char* str, float x, float cy, float scale, uint32_t argb, int align) {
    ps2d_text(&ui->draw, str, x, cy - TEXT_TOP * scale, scale, argb, align);
}

#define HINT_Y Y(474.0f)
#define HINT_SCALE 0.8f

/* Button hints along the bottom, each a symbol and a word; hints with an x go where the PS2 puts
 * them (measured), the others are spread around the centre. */
static void
draw_hints(ps2_ui* ui, const hint_t* hints, int n) {
    ps2_draw* d = &ui->draw;
    const float sym = 22.0f, gap = 40.0f;
    int free_n = 0;
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        if (hints[i].x <= 0.0f) {
            total += sym + 6.0f + ps2d_text_width(d, hints[i].text, HINT_SCALE) + (free_n++ ? gap : 0.0f);
        }
    }
    float x = 320.0f - total / 2.0f;
    for (int i = 0; i < n; i++) {
        float hx = hints[i].x > 0.0f ? hints[i].x : x;
        draw_symbol(ui, hints[i].glyph, hx, HINT_Y - sym / 2.0f, sym);
        text_at(ui, hints[i].text, hx + sym + 4.0f, HINT_Y, HINT_SCALE, COL_WHITE, PS2_TEXT_LEFT);
        if (hints[i].x <= 0.0f) {
            x += sym + 6.0f + ps2d_text_width(d, hints[i].text, HINT_SCALE) + gap;
        }
    }
}

/* Arrow of the sheet: 0 left, 1 right, 2 up, 3 down */
static void
draw_arrow(ps2_ui* ui, int which, float cx, float cy, float size, uint32_t argb) {
    static const float qu[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    static const float qv[4] = {0.0f, 0.0f, 0.5f, 0.5f};
    ps2d_quad(&ui->draw, ui->id.arrow, cx - size / 2.0f, cy - size / 2.0f, cx + size / 2.0f, cy + size / 2.0f, qu[which], qv[which],
              qu[which] + 0.5f, qv[which] + 0.5f, argb, PS2_BLEND_ALPHA);
}

/* ---- Drawing: main menu ----------------------------------------------------------------- */

#define RING_X 262.0f
#define RING_Y Y(262.0f)
#define RING_R 58.0f
#define MENU_ITEM_X 442.0f
#define MENU_ITEM_SCALE 1.12f

/* The ring of seven glowing balls that tumbles in space: a circle turned about two axes, the
 * balls bunch up and spread on it, and a faint trail marks it (main menu and version screen). */
static void
draw_orb_ring(ps2_ui* ui) {
    float t = ui->time;
    float tilt_x = 0.5f + 0.4f * sinf(t * 0.45f), tilt_z = t * 0.28f;
    float cx_ = cosf(tilt_x), sx_ = sinf(tilt_x), cz = cosf(tilt_z), sz = sinf(tilt_z);
    for (int i = 0; i < 48; i++) { /* trail */
        float a = (float)i * (2.0f * PI_F / 48.0f);
        float x = RING_R * cosf(a), y = RING_R * sinf(a) * cx_;
        float px = x * cz - y * sz, py = x * sz + y * cz;
        ps2d_sprite(&ui->draw, ui->id.glow2, RING_X + px, RING_Y + py, 16.0f, 16.0f, 0.0f, ps2_argb(26, 40, 90, 255), PS2_BLEND_ADD);
    }
    for (int i = 0; i < 7; i++) {
        float a = t * 0.7f + (float)i * (2.0f * PI_F / 7.0f) + 0.5f * sinf(t * 0.8f + (float)i * 0.9f);
        float x = RING_R * cosf(a), y = RING_R * sinf(a) * cx_, depth = RING_R * sinf(a) * sx_;
        float px = x * cz - y * sz, py = x * sz + y * cz;
        float size = 11.0f + 3.0f * (depth / RING_R);
        draw_orb(ui, RING_X + px, RING_Y + py, size, 0.8f + 0.2f * (depth / RING_R));
    }
}

static void
draw_main(ps2_ui* ui) {
    static const char* const names[MAIN_COUNT] = {"Browser", "Settings"};
    static const float ys[MAIN_COUNT] = {247.0f, 279.0f};
    draw_orb_ring(ui);
    /* the two entries never move, the pick only changes their colour (a cross-fade) */
    for (int i = 0; i < MAIN_COUNT; i++) {
        float focus = 1.0f - clampf(fabsf((float)i - ui->main_pos), 0.0f, 1.0f);
        text_at(ui, names[i], MENU_ITEM_X + (i == MAIN_SETTINGS ? 1.0f : 0.0f), Y(ys[i]), MENU_ITEM_SCALE, ps2_mix(COL_ITEM_GREY, COL_BLUE, focus),
                PS2_TEXT_CENTER);
    }
    hint_t hints[] = {{SYM_CROSS, "Enter", 235.0f}, {SYM_TRIANGLE, "Version", 523.0f}};
    draw_hints(ui, hints, 2);
}

static void
draw_version(ps2_ui* ui) {
    char games[24];
    int count = ui->host.game_count ? ui->host.game_count(ui->host.user) : 0;
    snprintf(games, sizeof(games), "%d", count);
    const char* ver = ui->host.version ? ui->host.version(ui->host.user) : "OpenMenu";
    const char* label[3] = {"Console", "Menu", "Games"};
    const char* value[3] = {"Dreamcast", ver, games};
    draw_orb_ring(ui);
    text_at(ui, "Version Information", 454.0f, Y(191.0f), 1.0f, COL_YELLOW, PS2_TEXT_CENTER);
    for (int i = 0; i < 3; i++) {
        float y = Y(296.0f + 26.0f * (float)(i - 1) + 26.0f);
        int sel = i == 2;
        text_at(ui, label[i], 440.0f, y, 0.9f, sel ? COL_BLUE : COL_ITEM_GREY, PS2_TEXT_RIGHT);
        text_at(ui, value[i], 467.0f, y, 0.9f, sel ? COL_VALUE : COL_ITEM_GREY, PS2_TEXT_LEFT);
    }
    hint_t hints[] = {{SYM_CIRCLE, "Back", 357.0f}};
    draw_hints(ui, hints, 1);
}

/* ---- Drawing: the grey panel of the browser --------------------------------------------- */

#define PANEL_X 79.0f
#define PANEL_Y 62.0f
#define PANEL_W 480.0f
#define PANEL_H 358.0f

/* Where a row of the cube grid lies on the screen: the rows are in perspective, the lower ones
 * nearer, wider apart and larger. s is the row (0 = top of the panel), may be fractional. */
static float
grid_y(float s) {
    static const float ys[4] = {163.0f, 217.0f, 275.0f, 342.0f};
    if (s <= 0.0f) return ys[0] + s * 54.0f;
    if (s >= 3.0f) return ys[3] + (s - 3.0f) * 67.0f;
    int i = (int)s;
    return ys[i] + (ys[i + 1] - ys[i]) * (s - (float)i);
}

static float
grid_spacing(float s) {
    return 60.0f + 5.0f * clampf(s, -1.0f, 4.0f);
}

static float
grid_scale(float s) {
    return 14.0f + 1.8f * clampf(s, -1.0f, 4.0f);
}

static void
draw_panel(ps2_ui* ui, const char* line1, const char* line2, int icon_model, int icon_tex) {
    ps2_draw* d = &ui->draw;
    /* dark at the top left, lighter to the lower right */
    ps2d_gradient_h(d, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0xFF2E2E2Au, 0xFF7C7C6Cu, PS2_BLEND_ALPHA);
    ps2d_gradient(d, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0x00FFFFFFu, 0x48FFFFFFu, PS2_BLEND_ADD);
    ps2_model_view mv = {.cx = 116.0f, .cy = 84.0f, .scale = 8.0f, .yaw = 0.0f, .pitch = 0.9f, .tint = 0xFF707070u, .tex = icon_tex,
                         .y_offset = icon_model == ui->id.m_cube ? -1.04f : 0.0f};
    ps2d_model(d, icon_model, &mv);
    ps2d_text_fit(d, line1, 137.0f, 66.0f, 0.5f, 0xFFE0E0E0u, PS2_TEXT_LEFT, 380.0f);
    ps2d_text_fit(d, line2, 137.0f, 90.0f, 0.42f, 0xFFC0C0C0u, PS2_TEXT_LEFT, 380.0f);
}

/* One icon of the grid: dark when idle, lit and turning when it is the picked one */
static void
draw_grid_icon(ps2_ui* ui, int model, int tex, int planar, float x, float y, float scale, int selected, float fade, float y_off) {
    ps2_draw* d = &ui->draw;
    if (selected) {
        ps2d_sprite(d, ui->id.glow2, x, y, scale * 6.5f, scale * 5.5f, 0.0f, ps2_fade(0xFFFFFFFFu, fade), PS2_BLEND_ADD);
        ps2d_sprite(d, ui->id.glow2, x, y, scale * 3.0f, scale * 2.6f, 0.0f, ps2_fade(0xFFFFFFFFu, fade), PS2_BLEND_ADD);
    }
    ps2_model_view mv = {.cx = x, .cy = y, .scale = scale, .yaw = selected ? ui->time * 1.4f : 0.5f, .pitch = 0.62f,
                         .y_offset = y_off, .tint = ps2_fade(selected ? 0xFFFFFFFFu : 0xFF787878u, fade), .tex = tex, .planar_uv = planar};
    ps2d_model(d, model, &mv);
}

/* ---- Drawing: device screen ------------------------------------------------------------- */

#define VIEW_X 10.0f
#define VIEW_W 620.0f

/* The picker's backdrop: a diagonal grey gradient, light at the top left, dark olive at the lower
 * right, inside the viewport (10,64)-(630,448) of the 640x512 reference, black around it. */
static void
draw_picker_backdrop(ps2_ui* ui, const char* tag, const char* name, const char* sub) {
    ps2_draw* d = &ui->draw;
    float y0 = Y(64.0f), h = Y(384.0f);
    ps2d_gradient_h(d, VIEW_X, y0, VIEW_W, h, 0xFF969696u, 0xFF4A4A38u, PS2_BLEND_ALPHA);
    ps2d_gradient(d, VIEW_X, y0, VIEW_W, h, 0x00000000u, 0x80101008u, PS2_BLEND_ALPHA);
    text_at(ui, tag, 35.0f, Y(81.0f), 0.82f, 0xFFD8D8D8u, PS2_TEXT_LEFT);
    /* title in yellow at the right; the part in brackets is smaller */
    const char* open = strchr(name, '(');
    const char* close = open ? strchr(open, ')') : NULL;
    if (open && close) {
        char head[40], mid[16];
        snprintf(head, sizeof(head), "%.*s", (int)(open - name), name);
        snprintf(mid, sizeof(mid), "%.*s", (int)(close - open + 1), open);
        float x = 627.0f;
        float tail_w = ps2d_text_width(d, close + 1, 0.9f);
        text_at(ui, close + 1, x, Y(81.0f), 0.9f, COL_YELLOW, PS2_TEXT_RIGHT);
        x -= tail_w;
        float mid_w = ps2d_text_width(d, mid, 0.62f);
        text_at(ui, mid, x, Y(84.0f), 0.62f, COL_YELLOW, PS2_TEXT_RIGHT);
        x -= mid_w + 3.0f;
        text_at(ui, head, x, Y(81.0f), 0.9f, COL_YELLOW, PS2_TEXT_RIGHT);
    } else {
        text_at(ui, name, 627.0f, Y(81.0f), 0.9f, COL_YELLOW, PS2_TEXT_RIGHT);
    }
    if (sub && sub[0]) {
        text_at(ui, sub, 627.0f, Y(112.0f), 0.62f, 0xFFE0E0D0u, PS2_TEXT_RIGHT);
    }
}

/* An icon seen face on, as in the picker: dark, or lit with a white glow on its label when picked */
static void
draw_picker_icon(ps2_ui* ui, int model, int tex, float x, float y, float scale, int selected, float pitch) {
    ps2_model_view mv = {.cx = x, .cy = y, .scale = scale, .yaw = 0.0f, .pitch = pitch, .roll = model == ui->id.m_card ? PI_F : 0.0f,
                         .dist = 16.0f, .tint = selected ? 0xFF5A5A5Au : 0xFF383838u, .tex = tex};
    ps2d_model(&ui->draw, model, &mv);
    if (selected) {
        float pulse = 0.85f + 0.15f * sinf(ui->time * 3.0f);
        ps2d_sprite(&ui->draw, ui->id.glow2, x, y + scale * 0.45f, scale * 2.6f, scale * 2.0f, 0.0f, ps2_argb((int)(255.0f * pulse), 255, 255, 255), PS2_BLEND_ADD);
    }
}

static void
draw_devices(ps2_ui* ui) {
    static const char* const names[DEV_COUNT] = {"Memory Card (PS2)/1", "Memory Card (PS2)/2", "Disc"};
    static const char* const tags[DEV_COUNT] = {"PS2", "PS2", "SD"};
    char info[48] = "";
    if (ui->dev_sel == DEV_DISC) {
        snprintf(info, sizeof(info), "Games on the SD card");
    }
    draw_picker_backdrop(ui, tags[ui->dev_sel], names[ui->dev_sel], info);
    for (int i = 0; i < DEV_COUNT; i++) {
        float x = 320.0f + ((float)i - 1.0f) * 156.0f; /* the two cards sit at 252 and 408 */
        if (i == DEV_DISC) {
            draw_picker_icon(ui, ui->id.m_disc, ui->id.t_disc, x, Y(276.0f), 25.0f, i == ui->dev_sel, 1.2f);
        } else {
            draw_picker_icon(ui, ui->id.m_card, ui->id.t_card, x, Y(276.0f), 27.0f, i == ui->dev_sel, 1.0f);
        }
    }
    hint_t hints[] = {{SYM_CROSS, "Enter", 235.0f}, {SYM_CIRCLE, "Back", 357.0f}};
    draw_hints(ui, hints, 2);
}

static void
draw_card_screen(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    char title[32];
    snprintf(title, sizeof(title), "Memory Card (PS2)/%d", ui->card_slot + 1);
    int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, ui->card_slot) : 0;
    draw_picker_backdrop(ui, "PS2", title, "");
    (void)d;
    text_at(ui, present ? "No data" : "No memory card", 320.0f, Y(250.0f), 1.0f, COL_WHITE, PS2_TEXT_CENTER);
    if (present) {
        text_at(ui, "Browsing memory cards is not available yet", 320.0f, Y(300.0f), 0.62f, 0xFFE0E0D0u, PS2_TEXT_CENTER);
    }
    hint_t hints[] = {{SYM_CIRCLE, "Back", 357.0f}};
    draw_hints(ui, hints, 1);
}

/* ---- Drawing: game list ----------------------------------------------------------------- */

static void
draw_games(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    int count = ui->host.game_count ? ui->host.game_count(ui->host.user) : 0;
    int cursor = ui->host.game_cursor ? ui->host.game_cursor(ui->host.user) : 0;
    char folder[64] = "";
    if (ui->host.game_folder) {
        ui->host.game_folder(ui->host.user, folder, sizeof(folder));
    }
    ps2_game_entry sel;
    memset(&sel, 0, sizeof(sel));
    if (count > 0) {
        ui->host.game_get(ui->host.user, cursor, &sel);
    }
    char line1[100], line2[112];
    snprintf(line1, sizeof(line1), "%s%s%s", "Disc", folder[0] ? " / " : "", folder);
    if (count <= 0) {
        snprintf(line2, sizeof(line2), "No games found");
    } else if (sel.disc_total > 1) {
        snprintf(line2, sizeof(line2), "%s   Disc %d of %d", sel.name, sel.disc_index + 1, sel.disc_total);
    } else {
        snprintf(line2, sizeof(line2), "%s", sel.name);
    }
    draw_panel(ui, line1, line2, ui->id.m_disc, ui->id.t_disc);

    float launch = ui->launching ? smooth(ui->launch_t / LAUNCH_TIME) : 0.0f;
    if (count > 0) {
        char num[32];
        snprintf(num, sizeof(num), "%d / %d", cursor + 1, count);
        ps2d_text(d, num, PANEL_X + PANEL_W - 14.0f, 66.0f, 0.42f, 0xFFC0C0C0u, PS2_TEXT_RIGHT);
        int first = (int)floorf(ui->grid_top) - 1;
        if (first < 0) first = 0;
        int last = (int)ceilf(ui->grid_top) + GRID_ROWS;
        int art_slot = 0;
        int sel_art = -1;
        int order[2] = {0, 1}; /* the picked icon is drawn after the others */
        for (int pass = 0; pass < 2; pass++) {
            for (int row = first; row <= last; row++) {
                float s = (float)row - ui->grid_top;
                if (s < -0.7f || s > (float)GRID_ROWS - 0.3f) {
                    continue;
                }
                float edge = clampf(1.0f + s * 1.4f, 0.0f, 1.0f) * clampf(((float)GRID_ROWS - 0.3f - s) * 1.4f, 0.0f, 1.0f);
                for (int col = 0; col < PS2_GRID_COLS; col++) {
                    int i = row * PS2_GRID_COLS + col;
                    if (i >= count || (i == cursor) != (order[pass] == 1)) {
                        continue;
                    }
                    ps2_game_entry e;
                    memset(&e, 0, sizeof(e));
                    ui->host.game_get(ui->host.user, i, &e);
                    float x = 320.0f + (float)(col - 2) * grid_spacing(s);
                    float y = grid_y(s);
                    float scale = grid_scale(s);
                    float fade = edge * (i == cursor ? 1.0f : 1.0f - launch);
                    int tex = e.is_folder ? ui->id.t_folder : ui->id.t_cube;
                    int planar = 0;
                    if (!e.is_folder && e.product[0] && art_slot < PS2_ART_SLOTS) {
                        if (ui->draw.sink.bind_art) {
                            ui->draw.sink.bind_art(ui->draw.sink.user, art_slot, e.product);
                        }
                        tex = PS2_TEX_ART + art_slot;
                        planar = 1;
                        if (i == cursor) sel_art = art_slot;
                        art_slot++;
                    }
                    if (i == cursor && ui->launching) {
                        scale *= 1.0f + 5.0f * launch * launch;
                        y -= 60.0f * launch;
                    }
                    draw_grid_icon(ui, ui->id.m_cube, tex, planar, x, y, scale, i == cursor, fade, -1.04f);
                }
            }
        }
        (void)sel_art;
        float up = ui->grid_top > 0.05f ? 1.0f : 0.0f;
        float down = (floorf(ui->grid_top + 0.5f) + GRID_ROWS) * PS2_GRID_COLS < (float)count ? 1.0f : 0.0f;
        if (up > 0.0f) draw_arrow(ui, 2, 320.0f, 114.0f, 16.0f, 0xFF2878FFu);
        if (down > 0.0f) draw_arrow(ui, 3, 320.0f, 392.0f, 16.0f, 0xFF2878FFu);
    } else {
        ps2d_text(d, "No data", 320.0f, 230.0f, 0.7f, COL_WHITE, PS2_TEXT_CENTER);
    }
    if (!ui->launching) {
        hint_t hints[4] = {{SYM_CROSS, "Enter", 0.0f}, {SYM_CIRCLE, "Back", 0.0f}, {SYM_SQUARE, "Recent", 0.0f}, {SYM_TRIANGLE, "Disc", 0.0f}};
        int n = sel.disc_total > 1 ? 4 : 3;
        draw_hints(ui, hints, n);
    }
    if (launch > 0.55f) {
        ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, ps2_argb((int)(255.0f * smooth((launch - 0.55f) / 0.45f)), 255, 255, 255));
    }
}

/* ---- Drawing: recently played ----------------------------------------------------------- */

static void
draw_recent(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    int n = ui->host.recent_count ? ui->host.recent_count(ui->host.user) : 0;
    int rows = n < 7 ? n : 7;
    int first = ui->recent_sel >= 7 ? ui->recent_sel - 6 : 0;
    float w = 400.0f, h = 60.0f + (float)rows * 36.0f;
    float x = 320.0f - w / 2.0f, y = 240.0f - h / 2.0f;
    ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, 0xB0000000u);
    ps2d_gradient(d, x, y, w, h, 0xFF40403Au, 0xFF1C1C18u, PS2_BLEND_ALPHA);
    ps2d_text(d, "Recently played", 320.0f, y + 8.0f, 0.55f, COL_WHITE, PS2_TEXT_CENTER);
    for (int i = 0; i < rows; i++) {
        float ry = y + 50.0f + (float)i * 36.0f;
        int on = first + i == ui->recent_sel;
        if (on) {
            ps2d_gradient_h(d, x + 10.0f, ry, w - 20.0f, 32.0f, 0x602878FFu, 0xB02878FFu, PS2_BLEND_ADD);
        }
        const char* name = ui->host.recent_name ? ui->host.recent_name(ui->host.user, first + i) : NULL;
        ps2d_text_fit(d, name ? name : "?", x + 24.0f, ry + 2.0f, 0.5f, on ? COL_WHITE : COL_GREY, PS2_TEXT_LEFT, w - 48.0f);
    }
}

/* ---- Drawing: settings ------------------------------------------------------------------ */

#define CFG_CX 277.0f /* middle of the ring of prisms and of the wire sphere, reference pixels (measured) */
#define CFG_CY 235.0f
#define CFG_SLOTS 5

/* The cubes sit still at these places (measured, reference pixels) and only tumble; the picked
 * setting is the one whose cube is lit blue. */
static const float cube_slot_x[CFG_SLOTS] = {205.0f, 85.0f, 212.0f, 90.0f, 208.0f};
static const float cube_slot_y[CFG_SLOTS] = {110.0f, 190.0f, 255.0f, 320.0f, 392.0f};

/* One prism of blue glass lying on the ring, pointing at the middle: three bands (lit top face,
 * side, dark underside) with a bright outline and a lighter end. `lit` is the pale cyan one. */
static void
draw_prism(ps2_ui* ui, float x, float y, float len, float width, float angle, int lit) {
    ps2_draw* d = &ui->draw;
    float c = cosf(angle), s = sinf(angle);
    float hx = c * len * 0.5f, hy = s * len * 0.5f, wx = -s * width * 0.5f, wy = c * width * 0.5f;
    static const float band_lo[3] = {-1.0f, -0.33f, 0.33f}, band_hi[3] = {-0.33f, 0.33f, 1.0f};
    static const uint32_t blue[3] = {0xA07AB4F0u, 0x98386CC8u, 0x982A4C9Cu};
    static const uint32_t cyan[3] = {0xE0DCFAFFu, 0xE0A8E8FFu, 0xE07CC8F0u};
    for (int k = 0; k < 3; k++) {
        float lo = band_lo[k], hi = band_hi[k];
        float xs[4] = {x - hx + wx * lo, x + hx + wx * lo, x - hx + wx * hi, x + hx + wx * hi};
        float ys[4] = {y - hy + wy * lo, y + hy + wy * lo, y - hy + wy * hi, y + hy + wy * hi};
        float z[4] = {0, 0, 0, 0};
        uint32_t col = lit ? cyan[k] : blue[k];
        uint32_t far = ps2_fade(col, 0.75f);
        uint32_t cs[4] = {far, col, far, col};
        ps2d_quad4(d, PS2_TEX_NONE, xs, ys, z, z, cs, PS2_BLEND_ALPHA);
    }
    uint32_t edge = lit ? 0xFFE8FAFFu : 0xB0A0D0FFu;
    ps2d_line(d, x - hx + wx, y - hy + wy, x + hx + wx, y + hy + wy, 1.3f, edge, PS2_BLEND_ALPHA);
    ps2d_line(d, x - hx - wx, y - hy - wy, x + hx - wx, y + hy - wy, 1.3f, edge, PS2_BLEND_ALPHA);
    ps2d_line(d, x + hx - wx, y + hy - wy, x + hx + wx, y + hy + wy, 1.3f, edge, PS2_BLEND_ALPHA);
    ps2d_line(d, x - hx - wx, y - hy - wy, x - hx + wx, y - hy + wy, 1.3f, ps2_fade(edge, 0.6f), PS2_BLEND_ALPHA);
}

/* The sphere of thin blue lines in the middle with its bright balls, turning in space */
static void
draw_wire_sphere(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    float t = ui->time;
    const float R = 66.0f;
    float cx = CFG_CX, cy = Y(CFG_CY);
    for (int m = 0; m < 4; m++) { /* the outline, then three meridians turning about the vertical axis */
        float phi = t * 0.4f + (float)m * (PI_F / 3.0f);
        float px = 0.0f, py = 0.0f;
        for (int i = 0; i <= 40; i++) {
            float a = (float)i * (2.0f * PI_F / 40.0f);
            float x, y, z;
            if (m == 0) {
                x = cosf(a);
                y = sinf(a);
                z = 0.0f;
            } else {
                x = sinf(a) * cosf(phi);
                y = cosf(a);
                z = sinf(a) * sinf(phi);
            }
            float sx = cx + R * x, sy = cy + R * 0.95f * y;
            if (i > 0) {
                ps2d_line(d, px, py, sx, sy, 1.2f, ps2_argb(m == 0 ? 190 : (z > 0.0f ? 170 : 80), 70, 130, 255), PS2_BLEND_ADD);
            }
            px = sx;
            py = sy;
        }
    }
    for (int i = 0; i < 9; i++) {
        float a = t * 0.8f + (float)i * 0.7f, b = 0.9f * sinf(t * 0.3f + (float)i);
        float x = cosf(b) * cosf(a), y = sinf(b), z = cosf(b) * sinf(a);
        draw_orb(ui, cx + R * x, cy + R * 0.95f * y, 7.0f + 2.0f * z, 0.85f + 0.15f * z);
    }
}

static void
draw_settings_background(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    float t = ui->time;
    float y0 = Y(62.0f), h = Y(388.0f);
    /* noisy purple, lighter towards the right, dark around the middle */
    ps2d_gradient_h(d, VIEW_X, y0, VIEW_W, h, 0xFF3E3068u, 0xFF6A5496u, PS2_BLEND_ALPHA);
    ps2d_gradient(d, VIEW_X, y0, VIEW_W, h, 0x30000000u, 0x70000010u, PS2_BLEND_ALPHA);
    ps2d_sprite(d, ui->id.glow2, CFG_CX, Y(CFG_CY), 420.0f, 360.0f, 0.0f, 0xC0100820u, PS2_BLEND_ALPHA);
    /* twelve prisms on an ellipse around the middle, pointing at it; the one at the upper right is pale */
    for (int i = 0; i < 12; i++) {
        float th = (float)i * (PI_F / 6.0f) + 0.015f * sinf(t * 0.6f + (float)i);
        float ex = 167.0f * cosf(th), ey = 138.0f * sinf(th);
        float ang = atan2f(ey, ex);
        float len = 100.0f + 52.0f * fabsf(cosf(th));
        draw_prism(ui, CFG_CX + ex, Y(CFG_CY) + ey * 0.9375f, len, 31.0f, ang, i == 10);
    }
    draw_wire_sphere(ui);
}

/* One cube of glass; the picked one is lit blue */
static void
draw_glass_cube(ps2_ui* ui, float x, float y, float phase, int selected, float fade) {
    ps2_glass g = {.cx = x, .cy = y, .scale = selected ? 28.0f : 26.0f,
                   .yaw = ui->time * 0.5f + phase, .pitch = 0.5f + 0.35f * sinf(ui->time * 0.37f + phase),
                   .roll = 0.45f * sinf(ui->time * 0.29f + phase * 2.0f), .dist = 9.0f,
                   .front = selected ? ps2_argb(185, 30, 120, 255) : ps2_argb(120, 150, 140, 190),
                   .back = selected ? ps2_argb(200, 10, 50, 200) : ps2_argb(160, 18, 10, 48),
                   .edge = selected ? ps2_argb(255, 130, 215, 255) : ps2_argb(225, 215, 205, 250), .edge_w = selected ? 2.4f : 2.0f};
    g.front = ps2_fade(g.front, fade);
    g.back = ps2_fade(g.back, fade);
    g.edge = ps2_fade(g.edge, fade);
    ps2d_glass_cube(&ui->draw, &g);
}

static void
draw_cfg_text(ps2_ui* ui, int row, float alpha) {
    ps2_draw* d = &ui->draw;
    float keep = d->alpha;
    d->alpha = keep * alpha;
    const float fit = 330.0f;
    const char* label = ui->host.set_label(ui->host.user, row);
    const char* value = ui->host.set_value(ui->host.user, row);
    float w = ps2d_text_width(d, label, 1.1f);
    float sc = w > fit ? 1.1f * fit / w : 1.1f;
    text_at(ui, label, 443.0f, Y(274.0f), sc, COL_CYAN, PS2_TEXT_CENTER);
    w = ps2d_text_width(d, value, 1.1f);
    sc = w > fit ? 1.1f * fit / w : 1.1f;
    text_at(ui, value, 443.0f, Y(316.0f), sc, COL_VALUE, PS2_TEXT_CENTER);
    text_at(ui, ui->host.set_help(ui->host.user, row), 443.0f, Y(356.0f), 0.55f, 0xFFC8C0E0u, PS2_TEXT_CENTER);
    d->alpha = keep;
}

static void
draw_settings(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    int n = ui->host.set_count ? ui->host.set_count(ui->host.user) : 0;
    float keep = ui->draw.alpha;
    float ui_fade = 1.0f - ui->cfg_hidden; /* Display hides the text, date and clock stay */

    draw_settings_background(ui);
    if (n > 0) {
        /* five cubes at fixed places; with more settings the five stand for a window of them */
        int top = n > CFG_SLOTS ? (int)clampf((float)(ui->settings_cursor - CFG_SLOTS / 2), 0.0f, (float)(n - CFG_SLOTS)) : 0;
        int slots = n < CFG_SLOTS ? n : CFG_SLOTS;
        for (int pass = 0; pass < 2; pass++) { /* the picked cube last */
            for (int k = 0; k < slots; k++) {
                int sel = top + k == ui->settings_cursor;
                if (sel != (pass == 1)) {
                    continue;
                }
                draw_glass_cube(ui, cube_slot_x[k], Y(cube_slot_y[k]), (float)k * 1.7f, sel, 1.0f);
            }
        }
    }
    /* the scene lives in the viewport; everything above and below it is black */
    ps2d_rect(d, 0.0f, 0.0f, 640.0f, Y(62.0f), 0xFF000000u);
    ps2d_rect(d, 0.0f, Y(450.0f), 640.0f, 480.0f - Y(450.0f), 0xFF000000u);
    if (ui->clock[0]) { /* "YYYY/MM/DD HH:MM:SS": the date at the left, the time at the right */
        char date[16], time_s[16];
        const char* sp = strchr(ui->clock, ' ');
        size_t dl = sp ? (size_t)(sp - ui->clock) : strlen(ui->clock);
        if (dl > sizeof(date) - 1) dl = sizeof(date) - 1;
        memcpy(date, ui->clock, dl);
        date[dl] = '\0';
        snprintf(time_s, sizeof(time_s), "%s", sp ? sp + 1 : "");
        text_at(ui, date, 35.0f, Y(47.0f), 1.0f, 0xFFE0E0E0u, PS2_TEXT_LEFT);
        text_at(ui, time_s, 628.0f, Y(47.0f), 1.0f, 0xFFE0E0E0u, PS2_TEXT_RIGHT);
    }
    d->alpha = keep * ui_fade;
    text_at(ui, "Settings", 443.0f, Y(221.0f), MENU_ITEM_SCALE, COL_YELLOW, PS2_TEXT_CENTER);
    if (n > 0) {
        /* the old text fades out while the new one fades in */
        if (ui->cfg_fade < 1.0f && ui->cfg_prev != ui->settings_cursor && ui->cfg_prev < n) {
            draw_cfg_text(ui, ui->cfg_prev, 1.0f - ui->cfg_fade);
        }
        draw_cfg_text(ui, ui->settings_cursor, ui->cfg_fade < 1.0f && ui->cfg_prev != ui->settings_cursor ? ui->cfg_fade : 1.0f);
        draw_arrow(ui, 2, 616.0f, Y(264.0f), 22.0f, 0xFFB8B8C8u); /* up/down change the value */
        draw_arrow(ui, 3, 616.0f, Y(286.0f), 22.0f, 0xFFB8B8C8u);
    }
    hint_t hints[] = {{SYM_SQUARE, "Display", 45.0f}, {SYM_CROSS, "Enter", 235.0f}, {SYM_CIRCLE, "Back", 357.0f}};
    d->alpha = keep * (ui->cfg_hide ? 1.0f : ui_fade);
    draw_hints(ui, hints, ui->cfg_hide ? 1 : 3);
    d->alpha = keep;
}

/* ---- Frame ------------------------------------------------------------------------------ */

void
ps2_ui_draw(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    d->alpha = 1.0f;
    d->ox = d->oy = 0.0f;

    /* leaving / arriving: fade */
    float k = 1.0f;
    if (ui->transitioning) {
        float half = SCREEN_TRANSITION / 2.0f;
        k = clampf(ui->transitioning == 1 ? ui->trans / half : 1.0f - ui->trans / half, 0.0f, 1.0f); /* linear, as on the PS2 */
    }
    d->alpha = k;
    switch (ui->screen) {
        case PS2_SCREEN_MAIN: draw_main(ui); break;
        case PS2_SCREEN_DEVICES: draw_devices(ui); break;
        case PS2_SCREEN_GAMES: draw_games(ui); break;
        case PS2_SCREEN_CARD: draw_card_screen(ui); break;
        case PS2_SCREEN_SETTINGS: draw_settings(ui); break;
        case PS2_SCREEN_VERSION: draw_version(ui); break;
        default: break;
    }
    if (ui->screen == PS2_SCREEN_GAMES && ui->recent_open) {
        draw_recent(ui);
    }
    d->alpha = 1.0f;
    if (ui->notice_t > 0.0f) {
        ps2d_text(d, ui->notice, 320.0f, 396.0f, 0.5f, ps2_fade(COL_WHITE, clampf(ui->notice_t * 2.0f, 0.0f, 1.0f)), PS2_TEXT_CENTER);
    }
    if (ui->boot_t < BOOT_FADE) {
        /* PS2LOGO fades in from black: alpha = 240 * (N - f) / N, linear over 0.55 s */
        float black = 1.0f - ui->boot_t / BOOT_FADE;
        ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, ps2_argb((int)(255.0f * black), 0, 0, 0));
    }
}
