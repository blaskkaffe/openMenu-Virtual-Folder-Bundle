/* ps2_ui: the PS2-style menu, see ps2_ui.h. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ps2_ui.h"

#define PI_F 3.14159265f

/* Timings (seconds). The PS2 BIOS counts video fields; its browser waits 30 NTSC / 25 PAL fields
 * for a transition, which is half a second, and the logo fades in over 33 / 28 fields (0.55 s). */
#define SCREEN_TRANSITION 0.5f
#define BOOT_FADE 0.55f
#define LAUNCH_TIME 0.7f
#define NOTICE_TIME 2.5f
#define EASE_RATE 12.0f

#define GRID_ROWS 4

#define COL_WHITE 0xFFFFFFFFu
#define COL_GREY 0xFFB4B4B4u
#define COL_BLUE 0xFF3C8CFFu   /* selected entry of the main menu */
#define COL_CYAN 0xFF2CB4F0u   /* setting name */
#define COL_YELLOW 0xFFEEDC28u /* screen title of the configuration */

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

static float
frac(float v) {
    return v - floorf(v);
}

/* Pseudo-random number in 0..1 from an index and a salt */
static float
rnd(int i, int salt) {
    return frac(sinf((float)i * 12.9898f + (float)salt * 78.233f) * 43758.5453f);
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
        sfx(ui, PS2_SFX_CONFIRM);
        notice(ui, ui->host.version ? ui->host.version(ui->host.user) : "OpenMenu");
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
            ui->settings_cursor = next;
            sfx(ui, PS2_SFX_CURSOR);
        }
    } else if (n > 0 && (b == PS2_BTN_UP || b == PS2_BTN_DOWN || b == PS2_BTN_A)) {
        ui->host.set_change(ui->host.user, row, b == PS2_BTN_UP ? -1 : 1);
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
} hint_t;

/* Button hints along the bottom, each a symbol and a word */
static void
draw_hints(ps2_ui* ui, const hint_t* hints, int n, float centre) {
    ps2_draw* d = &ui->draw;
    const float gap = 44.0f, sym = 24.0f;
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        total += sym + 6.0f + ps2d_text_width(d, hints[i].text, 0.5f) + (i + 1 < n ? gap : 0.0f);
    }
    float x = centre - total / 2.0f;
    for (int i = 0; i < n; i++) {
        draw_symbol(ui, hints[i].glyph, x, 428.0f, sym);
        ps2d_text(d, hints[i].text, x + sym + 6.0f, 426.0f, 0.5f, COL_WHITE, PS2_TEXT_LEFT);
        x += sym + 6.0f + ps2d_text_width(d, hints[i].text, 0.5f) + gap;
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

#define RING_X 232.0f
#define RING_Y 232.0f
#define MENU_X 420.0f

/* The seven glowing balls turning around a circle (the PS2 main menu) */
static void
draw_main(ps2_ui* ui) {
    static const char* const names[MAIN_COUNT] = {"Browser", "Settings"};
    ps2_draw* d = &ui->draw;
    float t = ui->time;
    for (int i = 0; i < 7; i++) {
        float a = t * 0.55f + (float)i * (2.0f * PI_F / 7.0f);
        float r = 92.0f + 10.0f * sinf(t * 0.7f + (float)i * 1.7f);
        float x = RING_X + r * cosf(a), y = RING_Y + r * 0.92f * sinf(a);
        float size = 15.0f + 6.0f * rnd(i, 3) + 2.5f * sinf(t * 1.3f + (float)i);
        draw_orb(ui, x, y, size, 0.85f + 0.15f * sinf(t * 2.0f + (float)i * 2.3f));
    }
    /* the list: the picked entry sits at the top slot in blue, the others follow in grey */
    for (int i = 0; i < MAIN_COUNT; i++) {
        float rel = (float)i - ui->main_pos;
        float focus = 1.0f - clampf(fabsf(rel), 0.0f, 1.0f);
        float y = 190.0f + rel * 48.0f - (rel > 0.0f ? 0.0f : 0.0f);
        float fade = rel < 0.0f ? 1.0f + rel * 1.4f : 1.0f;
        ps2d_text(d, names[i], MENU_X, y, 0.78f + 0.08f * focus - 0.12f * (1.0f - focus), ps2_fade(ps2_mix(0xFFB0B0B0u, COL_BLUE, focus), fade),
                  PS2_TEXT_CENTER);
    }
    hint_t left[] = {{SYM_CROSS, "Enter"}};
    hint_t right[] = {{SYM_TRIANGLE, "Version"}};
    draw_hints(ui, left, 1, 200.0f);
    draw_hints(ui, right, 1, 470.0f);
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

static void
draw_devices(ps2_ui* ui) {
    static const char* const names[DEV_COUNT] = {"Memory Card 1", "Memory Card 2", "Disc"};
    char info[48];
    if (ui->dev_sel == DEV_DISC) {
        snprintf(info, sizeof(info), "SD card games");
    } else {
        int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, ui->dev_sel) : 0;
        snprintf(info, sizeof(info), "%s", present ? "Memory card inserted" : "No memory card");
    }
    draw_panel(ui, names[ui->dev_sel], info, ui->id.m_disc, ui->id.t_disc);
    for (int i = 0; i < DEV_COUNT; i++) {
        float x = 320.0f + ((float)i - 1.0f) * 125.0f;
        int sel = i == ui->dev_sel;
        if (i == DEV_DISC) {
            draw_grid_icon(ui, ui->id.m_disc, ui->id.t_disc, 0, x, 255.0f, 24.0f, sel, 1.0f, 0.0f);
        } else {
            draw_grid_icon(ui, ui->id.m_card, ui->id.t_card, 0, x, 255.0f, 24.0f, sel, 1.0f, 0.0f);
        }
    }
    hint_t hints[] = {{SYM_CROSS, "Enter"}, {SYM_CIRCLE, "Back"}};
    draw_hints(ui, hints, 2, 320.0f);
}

static void
draw_card_screen(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    char title[32];
    snprintf(title, sizeof(title), "Memory Card %d", ui->card_slot + 1);
    int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, ui->card_slot) : 0;
    draw_panel(ui, title, present ? "Memory card inserted" : "No memory card", ui->id.m_card, ui->id.t_card);
    ps2d_text(d, present ? "No data" : "No memory card", 320.0f, 230.0f, 0.7f, COL_WHITE, PS2_TEXT_CENTER);
    if (present) {
        ps2d_text(d, "Browsing memory cards is not available yet", 320.0f, 280.0f, 0.42f, 0xFFD0D0D0u, PS2_TEXT_CENTER);
    }
    hint_t hints[] = {{SYM_CIRCLE, "Back"}};
    draw_hints(ui, hints, 1, 320.0f);
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
        hint_t hints[4] = {{SYM_CROSS, "Enter"}, {SYM_CIRCLE, "Back"}, {SYM_SQUARE, "Recent"}, {SYM_TRIANGLE, "Disc"}};
        int n = sel.disc_total > 1 ? 4 : 3;
        draw_hints(ui, hints, n, 320.0f);
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

#define CFG_X 190.0f
#define CFG_Y 238.0f
#define CFG_TEXT_X 436.0f

static void
draw_settings_background(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    float t = ui->time;
    ps2d_gradient(d, 0.0f, 0.0f, 640.0f, 480.0f, 0xFF52428Eu, 0xFF38295Fu, PS2_BLEND_ALPHA);
    /* beams of blue glass fanning out of the middle */
    for (int i = 0; i < 12; i++) {
        float a = t * 0.12f + (float)i * (2.0f * PI_F / 12.0f) + rnd(i, 1) * 0.3f;
        float len = 190.0f + 90.0f * rnd(i, 2);
        float r = 70.0f + len * 0.5f;
        float w = 22.0f + 22.0f * rnd(i, 3);
        ps2d_sprite(d, ui->id.blur, CFG_X + cosf(a) * r, CFG_Y + sinf(a) * r * 0.9f, len, w * 2.2f, a, ps2_argb(70 + (int)(40.0f * rnd(i, 4)), 40, 110, 255),
                    PS2_BLEND_ADD);
    }
    /* the dark eye in the middle and the sparks around it */
    ps2d_sprite(d, ui->id.glow2, CFG_X, CFG_Y, 190.0f, 190.0f, 0.0f, 0xE0100820u, PS2_BLEND_ALPHA);
    for (int i = 0; i < 7; i++) {
        float a = -t * 0.4f + (float)i * 0.9f;
        float r = 40.0f + 40.0f * rnd(i, 5);
        draw_orb(ui, CFG_X + r * cosf(a), CFG_Y + r * sinf(a), 5.0f + 4.0f * rnd(i, 6), 0.7f);
    }
}

/* One glass cube of the ring */
static void
draw_glass_cube(ps2_ui* ui, float x, float y, float scale, float yaw, int selected, float depth) {
    uint32_t tint = selected ? 0xFF2C78FFu : ps2_argb(150, 205, 200, 235);
    if (!selected && depth < 0.0f) {
        tint = ps2_argb(110, 170, 165, 215); /* the far side of the ring */
    }
    ps2_model_view mv = {.cx = x, .cy = y, .scale = scale, .yaw = yaw, .pitch = 0.5f, .y_offset = -1.04f, .tint = tint, .tex = PS2_TEX_NONE,
                         .spec = selected ? 0.5f : 0.3f};
    ps2d_model(&ui->draw, ui->id.m_cube, &mv);
}

static void
draw_settings(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    int n = ui->host.set_count ? ui->host.set_count(ui->host.user) : 0;
    draw_settings_background(ui);
    if (ui->clock[0]) { /* "YYYY/MM/DD HH:MM:SS": date at the left, time at the right */
        char date[16], time_s[16];
        const char* sp = strchr(ui->clock, ' ');
        size_t dl = sp ? (size_t)(sp - ui->clock) : strlen(ui->clock);
        if (dl > sizeof(date) - 1) dl = sizeof(date) - 1;
        memcpy(date, ui->clock, dl);
        date[dl] = '\0';
        snprintf(time_s, sizeof(time_s), "%s", sp ? sp + 1 : "");
        ps2d_text(d, date, 36.0f, 24.0f, 0.62f, COL_WHITE, PS2_TEXT_LEFT);
        ps2d_text(d, time_s, 606.0f, 24.0f, 0.62f, COL_WHITE, PS2_TEXT_RIGHT);
        draw_orb(ui, 520.0f, 38.0f, 5.0f, 0.9f);
    }
    /* the ring: the picked setting sits at the bottom and is the biggest */
    if (n > 0) {
        int order[64];
        float depth[64];
        int m = n < 64 ? n : 64;
        for (int i = 0; i < m; i++) {
            float a = 0.5f * PI_F + ((float)i - ui->settings_pos) * (2.0f * PI_F / (float)(m < 5 ? 5 : m));
            depth[i] = sinf(a);
            order[i] = i;
        }
        for (int i = 1; i < m; i++) { /* far ones first */
            int v = order[i], j = i;
            while (j > 0 && depth[order[j - 1]] > depth[v]) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = v;
        }
        for (int k = 0; k < m; k++) {
            int i = order[k];
            float a = 0.5f * PI_F + ((float)i - ui->settings_pos) * (2.0f * PI_F / (float)(m < 5 ? 5 : m));
            float x = CFG_X + 108.0f * cosf(a), y = CFG_Y + 130.0f * sinf(a);
            float scale = 22.0f + 7.0f * depth[i];
            draw_glass_cube(ui, x, y, scale, ui->time * 0.6f + (float)i, i == ui->settings_cursor, depth[i]);
        }
    }
    ps2d_text(d, "Settings", CFG_TEXT_X, 168.0f, 0.82f, COL_YELLOW, PS2_TEXT_CENTER);
    if (n > 0) {
        ps2d_text_fit(d, ui->host.set_label(ui->host.user, ui->settings_cursor), CFG_TEXT_X, 222.0f, 0.82f, COL_CYAN, PS2_TEXT_CENTER, 300.0f);
        ps2d_text_fit(d, ui->host.set_value(ui->host.user, ui->settings_cursor), CFG_TEXT_X, 262.0f, 0.82f, 0xFFE8E8E8u, PS2_TEXT_CENTER, 300.0f);
        ps2d_text_fit(d, ui->host.set_help(ui->host.user, ui->settings_cursor), CFG_TEXT_X, 316.0f, 0.42f, 0xFFD0C8E8u, PS2_TEXT_CENTER, 330.0f);
        draw_arrow(ui, 2, 592.0f, 236.0f, 18.0f, 0xFFE0E0E0u); /* up/down change the value */
        draw_arrow(ui, 3, 592.0f, 262.0f, 18.0f, 0xFFE0E0E0u);
    }
    hint_t hints[] = {{SYM_CROSS, "Change"}, {SYM_CIRCLE, "Back"}};
    draw_hints(ui, hints, 2, 380.0f);
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
        k = smooth(ui->transitioning == 1 ? ui->trans / half : 1.0f - ui->trans / half);
    }
    d->alpha = k;
    switch (ui->screen) {
        case PS2_SCREEN_MAIN: draw_main(ui); break;
        case PS2_SCREEN_DEVICES: draw_devices(ui); break;
        case PS2_SCREEN_GAMES: draw_games(ui); break;
        case PS2_SCREEN_CARD: draw_card_screen(ui); break;
        case PS2_SCREEN_SETTINGS: draw_settings(ui); break;
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
