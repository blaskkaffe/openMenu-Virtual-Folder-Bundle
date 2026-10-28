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

#define VISIBLE_GAMES 3  /* cubes shown on each side of the selected one */
#define SETTINGS_ROWS 6
#define POPUP_ROWS 7

#define COL_WHITE 0xFFFFFFFFu
#define COL_DIM 0xFFB8C4E0u
#define COL_ACCENT 0xFF6AA8FFu

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

static int
tex(const ps2_pack* p, const char* name) {
    return ps2_pack_find_tex(p, name);
}

int
ps2_ui_init(ps2_ui* ui, const ps2_pack* pack, const ps2_host* host, const ps2_sink* sink) {
    memset(ui, 0, sizeof(*ui));
    ui->pack = pack;
    ui->host = *host;
    ps2d_init(&ui->draw, pack, sink);
    static const char* const fog[5] = {"OFOG0", "OFOG1", "OFOG2", "OFOG3", "OFOG4"};
    for (int i = 0; i < 5; i++) {
        ui->id.fog[i] = tex(pack, fog[i]);
    }
    ui->id.flare = tex(pack, "OFLAR");
    ui->id.ref = tex(pack, "OREF");
    ui->id.blur = tex(pack, "CBLUR");
    ui->id.smoke = tex(pack, "CSMOK");
    ui->id.glow1 = tex(pack, "BNAV1");
    ui->id.glow2 = tex(pack, "BNAV2");
    ui->id.ring = tex(pack, "OCRLE");
    ui->id.btn = tex(pack, "BBTTN");
    ui->id.arrow = tex(pack, "BARRW");
    ui->id.kabe = tex(pack, "CKABE");
    ui->id.wall = tex(pack, "OWAL0");
    ui->id.m_disc = ps2_pack_find_model(pack, "MBPS2D");
    ui->id.m_card = ps2_pack_find_model(pack, "MBPS2M");
    ui->id.m_cube = ps2_pack_find_model(pack, "MBFNOR");
    ui->id.m_system = ps2_pack_find_model(pack, "MBYSYS");
    ui->id.t_disc = tex(pack, "TBPS2D");
    ui->id.t_card = tex(pack, "TBPS2M");
    ui->id.t_cube = tex(pack, "TBFNOR");
    ui->id.t_folder = tex(pack, "TBFSCE");
    ui->id.t_system = tex(pack, "TBYSYS");
    if (ui->id.m_disc < 0 || ui->id.m_card < 0 || ui->id.m_cube < 0 || ui->id.m_system < 0) {
        return -1;
    }
    ui->screen = PS2_SCREEN_MAIN;
    ui->main_sel = MAIN_BROWSER;
    ui->main_pos = 0.0f;
    ui->dev_sel = DEV_DISC;
    ui->dev_pos = (float)DEV_DISC;
    ui->boot_t = 0.0f;
    return 0;
}

void
ps2_ui_background(uint32_t* top, uint32_t* bottom) {
    *top = 0xFF02030A;
    *bottom = 0xFF0A1A46;
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
        ui->game_pos = ui->host.game_cursor ? (float)ui->host.game_cursor(ui->host.user) : 0.0f;
        ui->game_count_seen = -1;
    } else if (ui->screen == PS2_SCREEN_SETTINGS) {
        ui->settings_cursor = 0;
        ui->settings_top = 0;
        ui->popup = 0;
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
    if (b == PS2_BTN_LEFT && ui->main_sel > 0) {
        ui->main_sel--;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_RIGHT && ui->main_sel < MAIN_COUNT - 1) {
        ui->main_sel++;
        sfx(ui, PS2_SFX_CURSOR);
    } else if (b == PS2_BTN_A || b == PS2_BTN_START) {
        sfx(ui, PS2_SFX_ENTER);
        go(ui, ui->main_sel == MAIN_BROWSER ? PS2_SCREEN_DEVICES : PS2_SCREEN_SETTINGS);
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
        case PS2_BTN_UP: nav = PS2_NAV_PAGE_PREV; break;
        case PS2_BTN_DOWN: nav = PS2_NAV_PAGE_NEXT; break;
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

static void
button_settings(ps2_ui* ui, ps2_btn b) {
    int n = ui->host.set_count ? ui->host.set_count(ui->host.user) : 0;
    int row = ui->settings_cursor;
    if (n <= 0) {
        if (b == PS2_BTN_B) go(ui, PS2_SCREEN_MAIN);
        return;
    }
    if (ui->popup) {
        int count = ui->host.set_choice_count(ui->host.user, row);
        if (b == PS2_BTN_UP && ui->popup_sel > 0) {
            ui->popup_sel--;
            sfx(ui, PS2_SFX_CURSOR);
        } else if (b == PS2_BTN_DOWN && ui->popup_sel < count - 1) {
            ui->popup_sel++;
            sfx(ui, PS2_SFX_CURSOR);
        } else if (b == PS2_BTN_A || b == PS2_BTN_START) {
            ui->host.set_set(ui->host.user, row, ui->popup_sel);
            sfx(ui, PS2_SFX_CONFIRM);
            ui->popup = 0;
        } else if (b == PS2_BTN_B) {
            sfx(ui, PS2_SFX_CANCEL);
            ui->popup = 0;
        }
        return;
    }
    switch (b) {
        case PS2_BTN_UP:
            if (row > 0) {
                ui->settings_cursor--;
                sfx(ui, PS2_SFX_CURSOR);
            }
            break;
        case PS2_BTN_DOWN:
            if (row < n - 1) {
                ui->settings_cursor++;
                sfx(ui, PS2_SFX_CURSOR);
            }
            break;
        case PS2_BTN_LEFT:
        case PS2_BTN_RIGHT:
            ui->host.set_change(ui->host.user, row, b == PS2_BTN_LEFT ? -1 : 1);
            sfx(ui, PS2_SFX_CONFIRM);
            break;
        case PS2_BTN_A:
            if (ui->host.set_opens_popup(ui->host.user, row)) {
                ui->popup = 1;
                ui->popup_sel = ui->host.set_get(ui->host.user, row);
                sfx(ui, PS2_SFX_ENTER);
            } else {
                ui->host.set_change(ui->host.user, row, 1);
                sfx(ui, PS2_SFX_CONFIRM);
            }
            break;
        case PS2_BTN_B:
        case PS2_BTN_START:
            if (ui->host.set_leave && ui->host.set_leave(ui->host.user) != 0) {
                sfx(ui, PS2_SFX_ERROR);
                notice(ui, "Could not save settings");
            } else {
                sfx(ui, PS2_SFX_CANCEL);
            }
            go(ui, PS2_SCREEN_MAIN);
            break;
        default: break;
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
    if (ui->screen == PS2_SCREEN_GAMES && ui->host.game_cursor) {
        int count = ui->host.game_count(ui->host.user);
        char folder[64] = "";
        if (ui->host.game_folder) {
            ui->host.game_folder(ui->host.user, folder, sizeof(folder));
        }
        int target = ui->host.game_cursor(ui->host.user);
        if (count != ui->game_count_seen || strcmp(folder, ui->game_folder_seen) != 0) {
            ui->game_pos = (float)target; /* another list (a folder was entered or left): no sweep */
            ui->game_count_seen = count;
            snprintf(ui->game_folder_seen, sizeof(ui->game_folder_seen), "%s", folder);
        }
        ui->game_pos = ease_to(ui->game_pos, (float)target, dt);
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

/* ---- Drawing: background ---------------------------------------------------------------- */

static float
frac(float v) {
    return v - floorf(v);
}

/* Pseudo-random number in 0..1 from an index and a salt */
static float
rnd(int i, int salt) {
    float v = sinf((float)i * 12.9898f + (float)salt * 78.233f) * 43758.5453f;
    return frac(v);
}

static void
draw_background(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    float t = ui->time;
    float saved = d->alpha;
    d->alpha = 1.0f;

    /* the towers: tall soft pillars of light that sway and breathe */
    for (int i = 0; i < 7; i++) {
        float phase = rnd(i, 1) * 6.283f;
        float x = 40.0f + (float)i * 95.0f + sinf(t * 0.15f + phase) * 26.0f;
        float h = 300.0f + 220.0f * rnd(i, 2);
        float w = 70.0f + 70.0f * rnd(i, 3);
        float breathe = 0.55f + 0.45f * sinf(t * (0.3f + 0.2f * rnd(i, 4)) + phase);
        uint32_t c = ps2_argb((int)(34.0f + 40.0f * breathe), 60, 110 + (int)(60.0f * rnd(i, 5)), 255);
        ps2d_sprite(d, ui->id.blur, x, 480.0f - h * 0.35f, w * 2.2f, h * 1.6f, 0.0f, c, PS2_BLEND_ADD);
        ps2d_sprite(d, ui->id.glow2, x, 480.0f - h * 0.30f, w * 0.7f, h * 1.1f, 0.0f, ps2_argb((int)(26.0f * breathe), 150, 190, 255),
                    PS2_BLEND_ADD);
    }
    /* slow drifting fog */
    for (int i = 0; i < 3; i++) {
        int f = ui->id.fog[i % 5];
        float a = t * (0.03f + 0.02f * (float)i) * (i & 1 ? -1.0f : 1.0f);
        float cx = 320.0f + sinf(t * 0.07f + (float)i * 2.0f) * 90.0f;
        float cy = 250.0f + cosf(t * 0.05f + (float)i) * 50.0f;
        ps2d_sprite(d, f, cx, cy, 900.0f, 700.0f, a, ps2_argb(26, 50, 100, 230), PS2_BLEND_ADD);
    }
    /* rising sparks */
    for (int i = 0; i < 26; i++) {
        float speed = 14.0f + 26.0f * rnd(i, 6);
        float y = 520.0f - fmodf(rnd(i, 7) * 540.0f + t * speed, 540.0f);
        float x = rnd(i, 8) * 640.0f + sinf(t * 0.6f + (float)i) * 14.0f;
        float size = 5.0f + 11.0f * rnd(i, 9);
        float tw = 0.5f + 0.5f * sinf(t * (1.0f + rnd(i, 10) * 2.0f) + (float)i);
        float edge = smooth(y / 90.0f) * smooth((480.0f - y) / 120.0f);
        ps2d_sprite(d, ui->id.glow2, x, y, size * 2.0f, size * 2.0f, 0.0f, ps2_argb((int)(110.0f * tw * edge), 170, 200, 255),
                    PS2_BLEND_ADD);
    }
    /* darkened top and bottom */
    ps2d_gradient(d, 0.0f, 0.0f, 640.0f, 90.0f, 0xB0000008u, 0x00000008u, PS2_BLEND_ALPHA);
    ps2d_gradient(d, 0.0f, 380.0f, 640.0f, 100.0f, 0x00000008u, 0xC0000008u, PS2_BLEND_ALPHA);
    d->alpha = saved;
}

/* ---- Drawing: common parts -------------------------------------------------------------- */

typedef struct {
    int glyph; /* 0 cross (A), 1 circle (B), 2 square (X) */
    const char* text;
} hint_t;

/* One of the pad symbols of the PS2: the sheet holds square, triangle (top row), cross, circle. */
static void
draw_symbol(ps2_ui* ui, int which, float x, float y, float size) {
    static const float qu[3] = {0.0f, 0.5f, 0.0f}; /* cross, circle, square */
    static const float qv[3] = {0.5f, 0.5f, 0.0f};
    ps2d_quad(&ui->draw, ui->id.btn, x, y, x + size, y + size, qu[which], qv[which], qu[which] + 0.5f, qv[which] + 0.5f, COL_WHITE,
              PS2_BLEND_ALPHA);
}

static void
draw_hints(ps2_ui* ui, const hint_t* hints, int n) {
    ps2_draw* d = &ui->draw;
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        total += 30.0f + ps2d_text_width(d, hints[i].text, 0.5f) + 26.0f;
    }
    float x = 320.0f - total / 2.0f;
    ps2d_gradient_h(d, 0.0f, 432.0f, 640.0f, 1.0f, 0x00FFFFFFu, 0x66FFFFFFu, PS2_BLEND_ADD);
    for (int i = 0; i < n; i++) {
        draw_symbol(ui, hints[i].glyph, x, 441.0f, 26.0f);
        ps2d_text(d, hints[i].text, x + 30.0f, 439.0f, 0.5f, COL_WHITE, PS2_TEXT_LEFT);
        x += 30.0f + ps2d_text_width(d, hints[i].text, 0.5f) + 26.0f;
    }
}

static void
draw_header(ps2_ui* ui, const char* title, const char* trail, int icon_model, int icon_tex) {
    ps2_draw* d = &ui->draw;
    float icon_scale = icon_model == ui->id.m_system ? 7.0f : 11.0f;
    float icon_yoff = icon_model == ui->id.m_system ? -2.0f : 0.0f;
    float icon_pitch = icon_model == ui->id.m_system ? 0.1f : 0.9f;
    if (icon_model >= 0) {
        ps2_model_view mv = {.cx = 66.0f, .cy = 40.0f, .scale = icon_scale, .yaw = ui->time * 1.2f, .pitch = icon_pitch,
                             .y_offset = icon_yoff, .tint = COL_WHITE, .tex = icon_tex};
        ps2d_model(d, icon_model, &mv);
    }
    ps2d_text(d, title, 96.0f, 24.0f, 0.72f, COL_WHITE, PS2_TEXT_LEFT);
    if (trail && trail[0]) {
        float w = ps2d_text_width(d, title, 0.72f);
        ps2d_text_fit(d, trail, 96.0f + w + 14.0f, 31.0f, 0.55f, COL_DIM, PS2_TEXT_LEFT, 330.0f - w);
    }
    ps2d_gradient_h(d, 40.0f, 74.0f, 560.0f, 1.5f, 0xAAFFFFFFu, 0x00FFFFFFu, PS2_BLEND_ADD);
    if (ui->clock[0]) {
        ps2d_text(d, ui->clock, 600.0f, 28.0f, 0.5f, COL_DIM, PS2_TEXT_RIGHT);
    }
}

/* ---- Drawing: icons --------------------------------------------------------------------- */

/* The soft glow behind the selected icon */
static void
draw_stage(ps2_ui* ui, float cx, float cy, float size, float amount) {
    ps2_draw* d = &ui->draw;
    ps2d_sprite(d, ui->id.glow2, cx, cy, size * 2.2f, size * 1.7f, 0.0f, ps2_argb((int)(100.0f * amount), 90, 140, 255), PS2_BLEND_ADD);
}

static void
draw_disc(ps2_ui* ui, float cx, float cy, float scale, float yaw, uint32_t tint) {
    ps2_model_view mv = {.cx = cx, .cy = cy, .scale = scale, .yaw = yaw, .pitch = 1.0f, .tint = tint, .tex = ui->id.t_disc};
    ps2d_model(&ui->draw, ui->id.m_disc, &mv);
}

static void
draw_card(ps2_ui* ui, float cx, float cy, float scale, float yaw, uint32_t tint) {
    ps2_model_view mv = {.cx = cx, .cy = cy, .scale = scale, .yaw = yaw, .pitch = 0.95f, .tint = tint, .tex = ui->id.t_card};
    ps2d_model(&ui->draw, ui->id.m_card, &mv);
}

static void
draw_system(ps2_ui* ui, float cx, float cy, float scale, float yaw, uint32_t tint) {
    ps2_model_view mv = {.cx = cx, .cy = cy, .scale = scale, .yaw = yaw, .pitch = 0.25f, .y_offset = -2.0f, .tint = tint,
                         .tex = ui->id.t_system};
    ps2d_model(&ui->draw, ui->id.m_system, &mv);
}

static uint32_t
dim_tint(float focus) {
    int v = (int)(120.0f + 135.0f * focus);
    return ps2_argb(255, v, v, v);
}

/* ---- Drawing: main menu ----------------------------------------------------------------- */

static void
draw_main(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    static const char* const names[MAIN_COUNT] = {"Browser", "Settings"};
    static const char* const texts[MAIN_COUNT] = {"Games on the SD card and memory cards", "Change how openMenu works"};
    ps2d_text(d, "OpenMenu", 56.0f, 24.0f, 0.72f, COL_WHITE, PS2_TEXT_LEFT);
    if (ui->clock[0]) {
        ps2d_text(d, ui->clock, 600.0f, 28.0f, 0.5f, COL_DIM, PS2_TEXT_RIGHT);
    }
    ps2d_gradient_h(d, 40.0f, 74.0f, 560.0f, 1.5f, 0xAAFFFFFFu, 0x00FFFFFFu, PS2_BLEND_ADD);

    for (int i = 0; i < MAIN_COUNT; i++) {
        float focus = 1.0f - clampf(fabsf((float)i - ui->main_pos), 0.0f, 1.0f);
        float cx = 190.0f + (float)i * 260.0f;
        float cy = 205.0f - 6.0f * focus + 4.0f * sinf(ui->time * 1.6f + (float)i) * focus;
        float s = 62.0f + 22.0f * focus;
        uint32_t tint = dim_tint(focus);
        float spin = ui->time * (0.5f + 0.9f * focus);
        draw_stage(ui, cx, cy + 6.0f, 110.0f, focus);
        if (i == MAIN_BROWSER) {
            draw_card(ui, cx - 46.0f, cy - 10.0f, s * 0.50f, spin * 0.7f + 0.5f, tint);
            draw_disc(ui, cx + 40.0f, cy + 12.0f, s * 0.54f, -spin, tint);
        } else {
            draw_system(ui, cx, cy, s * 0.40f, sinf(ui->time * 0.9f) * 0.5f, tint);
        }
        uint32_t tc = ps2_mix(0xFF7080A0u, COL_WHITE, focus);
        ps2d_text(d, names[i], cx, 322.0f, 0.62f + 0.14f * focus, tc, PS2_TEXT_CENTER);
    }
    float f = clampf(1.0f - fabsf(ui->main_pos - (float)ui->main_sel) * 2.0f, 0.0f, 1.0f);
    ps2d_text(d, texts[ui->main_sel], 320.0f, 372.0f, 0.5f, ps2_fade(COL_DIM, f), PS2_TEXT_CENTER);

    hint_t hints[] = {{0, "Enter"}};
    draw_hints(ui, hints, 1);
}

/* ---- Drawing: device screen ------------------------------------------------------------- */

static void
draw_devices(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    static const char* const names[DEV_COUNT] = {"Memory Card 1", "Memory Card 2", "Disc"};
    draw_header(ui, "Browser", NULL, ui->id.m_disc, ui->id.t_disc);
    char info[DEV_COUNT][48];
    for (int i = 0; i < DEV_COUNT; i++) {
        info[i][0] = '\0';
    }
    for (int i = 0; i < 2; i++) {
        int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, i) : 0;
        snprintf(info[i], sizeof(info[i]), "%s", present ? "Memory card inserted" : "No memory card");
    }
    snprintf(info[DEV_DISC], sizeof(info[DEV_DISC]), "SD card games");

    for (int i = 0; i < DEV_COUNT; i++) {
        float rel = (float)i - ui->dev_pos;
        float focus = 1.0f - clampf(fabsf(rel), 0.0f, 1.0f);
        float cx = 320.0f + rel * 200.0f;
        float cy = 215.0f - 6.0f * focus;
        float s = 32.0f + 14.0f * focus;
        uint32_t tint = dim_tint(focus);
        if (focus > 0.05f) {
            draw_stage(ui, cx, cy, 110.0f, focus);
        }
        float yaw = ui->time * (0.4f + 0.9f * focus);
        if (i == DEV_DISC) {
            draw_disc(ui, cx, cy, s, -yaw, tint);
        } else {
            int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, i) : 0;
            draw_card(ui, cx, cy, s, yaw * 0.8f, present ? tint : ps2_mix(tint, 0xFF404858u, 0.5f));
        }
    }
    ps2d_text(d, names[ui->dev_sel], 320.0f, 322.0f, 0.78f, COL_WHITE, PS2_TEXT_CENTER);
    ps2d_text(d, info[ui->dev_sel], 320.0f, 372.0f, 0.5f, COL_DIM, PS2_TEXT_CENTER);
    hint_t hints[] = {{0, "Enter"}, {1, "Back"}};
    draw_hints(ui, hints, 2);
}

static void
draw_card_screen(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    char title[32];
    snprintf(title, sizeof(title), "Memory Card %d", ui->card_slot + 1);
    draw_header(ui, title, NULL, ui->id.m_card, ui->id.t_card);
    int present = ui->host.memcard_present ? ui->host.memcard_present(ui->host.user, ui->card_slot) : 0;
    draw_card(ui, 320.0f, 190.0f, 44.0f, ui->time * 0.8f, present ? COL_WHITE : 0xFF606878u);
    ps2d_text(d, present ? "No data" : "No memory card", 320.0f, 310.0f, 0.78f, COL_WHITE, PS2_TEXT_CENTER);
    if (present) {
        ps2d_text(d, "Browsing memory cards is not available yet", 320.0f, 366.0f, 0.5f, COL_DIM, PS2_TEXT_CENTER);
    }
    hint_t hints[] = {{1, "Back"}};
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
    draw_header(ui, "Disc", folder, ui->id.m_disc, ui->id.t_disc);

    if (count <= 0) {
        ps2d_text(d, "No data", 320.0f, 210.0f, 0.78f, COL_WHITE, PS2_TEXT_CENTER);
        ps2d_text(d, "No games found", 320.0f, 262.0f, 0.5f, COL_DIM, PS2_TEXT_CENTER);
        hint_t hints[] = {{1, "Back"}};
        draw_hints(ui, hints, 1);
        return;
    }

    float launch = ui->launching ? smooth(ui->launch_t / LAUNCH_TIME) : 0.0f;
    int first = (int)floorf(ui->game_pos) - VISIBLE_GAMES;
    int last = (int)ceilf(ui->game_pos) + VISIBLE_GAMES;
    int art_slot = 0;
    ps2_game_entry selected;
    memset(&selected, 0, sizeof(selected));

    /* far cubes first, the selected one on top */
    for (int pass = VISIBLE_GAMES + 1; pass >= 0; pass--) {
        for (int i = first; i <= last; i++) {
            float rel = (float)i - ui->game_pos;
            if (i < 0 || i >= count || (int)floorf(fabsf(rel) + 0.5f) != pass) {
                continue;
            }
            float a = fabsf(rel);
            float focus = 1.0f - clampf(a, 0.0f, 1.0f);
            float edge = 1.0f - smooth((a - 1.8f) / 1.4f);
            ps2_game_entry e;
            memset(&e, 0, sizeof(e));
            ui->host.game_get(ui->host.user, i, &e);
            if (i == cursor) {
                selected = e;
            }
            float cx = 320.0f + (rel < 0 ? -1.0f : 1.0f) * (a < 1.0f ? a * 128.0f : 128.0f + (a - 1.0f) * 92.0f);
            float cy = 205.0f - 8.0f * focus;
            float s = 33.0f + 20.0f * focus;
            float yaw = i == cursor ? ui->time * 1.1f : (rel < 0 ? 0.55f : -0.55f);
            float fade = edge * (1.0f - (i == cursor ? 0.0f : launch));
            if (i == cursor && ui->launching) {
                s *= 1.0f + 2.6f * launch * launch;
                cy += 40.0f * launch;
                yaw += launch * 9.0f;
            }
            ps2_model_view mv = {.cx = cx, .cy = cy, .scale = s, .yaw = yaw, .pitch = 0.32f, .y_offset = -1.04f,
                                 .tint = ps2_fade(dim_tint(focus), fade), .tex = e.is_folder ? ui->id.t_folder : ui->id.t_cube};
            if (!e.is_folder && e.product[0] && art_slot < PS2_ART_SLOTS) {
                if (ui->draw.sink.bind_art) {
                    ui->draw.sink.bind_art(ui->draw.sink.user, art_slot, e.product);
                }
                mv.tex = PS2_TEX_ART + art_slot;
                mv.planar_uv = 1;
                art_slot++;
            }
            if (focus > 0.2f && !ui->launching) {
                draw_stage(ui, cx, cy, 100.0f, focus * fade);
            }
            ps2d_model(d, ui->id.m_cube, &mv);
        }
    }

    float f = clampf(1.0f - fabsf(ui->game_pos - (float)cursor) * 1.5f, 0.0f, 1.0f) * (1.0f - launch);
    char line[112];
    ps2d_text_fit(d, selected.name, 320.0f, 322.0f, 0.74f, ps2_fade(COL_WHITE, f), PS2_TEXT_CENTER, 560.0f);
    if (selected.is_folder) {
        snprintf(line, sizeof(line), "Folder");
    } else if (selected.disc_total > 1) {
        snprintf(line, sizeof(line), "%s   Disc %d of %d", selected.info, selected.disc_index + 1, selected.disc_total);
    } else {
        snprintf(line, sizeof(line), "%s", selected.info);
    }
    ps2d_text_fit(d, line, 320.0f, 366.0f, 0.5f, ps2_fade(COL_DIM, f), PS2_TEXT_CENTER, 560.0f);
    snprintf(line, sizeof(line), "%d / %d", cursor + 1, count);
    ps2d_text(d, line, 600.0f, 52.0f, 0.5f, ps2_fade(COL_DIM, 1.0f - launch), PS2_TEXT_RIGHT);

    if (!ui->launching) {
        hint_t hints[] = {{0, "Enter"}, {1, "Back"}, {2, "Recent"}};
        draw_hints(ui, hints, 3);
    }
    if (launch > 0.55f) {
        ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, ps2_argb((int)(255.0f * smooth((launch - 0.55f) / 0.45f)), 255, 255, 255));
    }
}

/* ---- Drawing: popups and settings ------------------------------------------------------- */

static void
draw_panel_alpha(ps2_ui* ui, float x, float y, float w, float h, uint32_t alpha) {
    ps2_draw* d = &ui->draw;
    ps2d_gradient(d, x, y, w, h, (alpha << 24) | 0x183070u, (alpha << 24) | 0x0A1438u, PS2_BLEND_ALPHA);
    ps2d_gradient_h(d, x, y, w, 2.0f, 0x00AACCFFu, 0xCCAACCFFu, PS2_BLEND_ADD);
    ps2d_gradient_h(d, x, y + h - 2.0f, w, 2.0f, 0xCCAACCFFu, 0x00AACCFFu, PS2_BLEND_ADD);
}

static void
draw_bar(ps2_ui* ui, float x, float y, float w, float h) {
    ps2_draw* d = &ui->draw;
    float pulse = 0.85f + 0.15f * sinf(ui->time * 4.0f);
    ps2d_gradient_h(d, x, y, w * 0.7f, h, ps2_argb((int)(40.0f * pulse), 80, 130, 255), ps2_argb((int)(125.0f * pulse), 120, 175, 255),
                    PS2_BLEND_ADD);
    ps2d_gradient_h(d, x + w * 0.7f, y, w * 0.3f, h, ps2_argb((int)(125.0f * pulse), 120, 175, 255), ps2_argb(40, 80, 130, 255),
                    PS2_BLEND_ADD);
    ps2d_gradient_h(d, x, y, w, 1.5f, 0x00FFFFFFu, 0xAAFFFFFFu, PS2_BLEND_ADD);
    ps2d_gradient_h(d, x, y + h - 1.5f, w, 1.5f, 0xAAFFFFFFu, 0x00FFFFFFu, PS2_BLEND_ADD);
}

static void
draw_panel(ps2_ui* ui, float x, float y, float w, float h) {
    draw_panel_alpha(ui, x, y, w, h, 0xE0u);
}

/* A list popup in the style of the PS2 menus: title, rows, the picked one lit */
typedef const char* (*name_fn)(ps2_ui* ui, int index);

static void
draw_popup(ps2_ui* ui, const char* title, int count, int sel, name_fn name) {
    ps2_draw* d = &ui->draw;
    int rows = count < POPUP_ROWS ? count : POPUP_ROWS;
    int first = sel >= POPUP_ROWS ? sel - POPUP_ROWS + 1 : 0;
    float w = 380.0f, h = 70.0f + (float)rows * 38.0f;
    float x = 320.0f - w / 2.0f, y = 240.0f - h / 2.0f;
    ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, 0xB0000010u);
    draw_panel_alpha(ui, x, y, w, h, 0xFCu); /* what is under a popup must not shine through its rows */
    ps2d_text(d, title, 320.0f, y + 10.0f, 0.62f, COL_WHITE, PS2_TEXT_CENTER);
    for (int i = 0; i < rows; i++) {
        float ry = y + 56.0f + (float)i * 38.0f;
        int on = first + i == sel;
        if (on) {
            draw_bar(ui, x + 12.0f, ry, w - 24.0f, 36.0f);
        }
        ps2d_text_fit(d, name(ui, first + i), x + 30.0f, ry + 3.0f, 0.58f, on ? COL_WHITE : COL_DIM, PS2_TEXT_LEFT, w - 60.0f);
    }
}

static const char*
popup_recent_name(ps2_ui* ui, int index) {
    const char* n = ui->host.recent_name ? ui->host.recent_name(ui->host.user, index) : NULL;
    return n ? n : "?";
}

static const char*
popup_choice_name(ps2_ui* ui, int index) {
    return ui->host.set_choice_name(ui->host.user, ui->settings_cursor, index);
}

static void
draw_settings(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    int n = ui->host.set_count ? ui->host.set_count(ui->host.user) : 0;
    draw_header(ui, "Settings", NULL, ui->id.m_system, ui->id.t_system);
    if (ui->settings_cursor < ui->settings_top) {
        ui->settings_top = ui->settings_cursor;
    }
    if (ui->settings_cursor >= ui->settings_top + SETTINGS_ROWS) {
        ui->settings_top = ui->settings_cursor - SETTINGS_ROWS + 1;
    }
    const float x = 56.0f, w = 528.0f, y0 = 92.0f, rh = 40.0f;
    draw_panel(ui, x - 10.0f, y0 - 8.0f, w + 20.0f, rh * SETTINGS_ROWS + 16.0f);
    for (int i = 0; i < SETTINGS_ROWS; i++) {
        int row = ui->settings_top + i;
        if (row >= n) {
            break;
        }
        float y = y0 + (float)i * rh;
        int on = row == ui->settings_cursor;
        if (on) {
            draw_bar(ui, x, y, w, rh - 4.0f);
        }
        uint32_t col = on ? COL_WHITE : COL_DIM;
        ps2d_text_fit(d, ui->host.set_label(ui->host.user, row), x + 20.0f, y + 2.0f, 0.62f, col, PS2_TEXT_LEFT, 250.0f);
        char value[64];
        snprintf(value, sizeof(value), "%s", ui->host.set_value(ui->host.user, row));
        float vx = x + w - 24.0f;
        if (on) {
            float vw = ps2d_text_width(d, value, 0.62f);
            ps2d_text(d, "<", vx - vw - 22.0f, y + 2.0f, 0.62f, COL_ACCENT, PS2_TEXT_LEFT);
            ps2d_text(d, ">", vx + 8.0f, y + 2.0f, 0.62f, COL_ACCENT, PS2_TEXT_LEFT);
        }
        ps2d_text_fit(d, value, vx, y + 2.0f, 0.62f, col, PS2_TEXT_RIGHT, 220.0f);
    }
    if (n > SETTINGS_ROWS) { /* scroll bar */
        float track = rh * SETTINGS_ROWS;
        float bar = track * (float)SETTINGS_ROWS / (float)n;
        float by = y0 + (track - bar) * (float)ui->settings_top / (float)(n - SETTINGS_ROWS);
        ps2d_rect(d, x + w + 4.0f, y0, 3.0f, track, 0x40FFFFFFu);
        ps2d_rect(d, x + w + 4.0f, by, 3.0f, bar, 0xC0FFFFFFu);
    }
    if (n > 0) {
        char line[96];
        snprintf(line, sizeof(line), "%s", ui->host.set_group(ui->host.user, ui->settings_cursor));
        draw_panel(ui, x - 10.0f, 360.0f, w + 20.0f, 62.0f);
        ps2d_text(d, line, x + 12.0f, 363.0f, 0.5f, COL_ACCENT, PS2_TEXT_LEFT);
        ps2d_text_fit(d, ui->host.set_help(ui->host.user, ui->settings_cursor), x + 12.0f, 388.0f, 0.55f, COL_WHITE, PS2_TEXT_LEFT,
                      w - 20.0f);
    }
    if (ui->popup) {
        draw_popup(ui, ui->host.set_label(ui->host.user, ui->settings_cursor), ui->host.set_choice_count(ui->host.user, ui->settings_cursor),
                   ui->popup_sel, popup_choice_name);
        hint_t hints[] = {{0, "Choose"}, {1, "Cancel"}};
        draw_hints(ui, hints, 2);
    } else {
        hint_t hints[] = {{0, "Change"}, {1, "Back"}};
        draw_hints(ui, hints, 2);
    }
}

/* ---- Frame ------------------------------------------------------------------------------ */

void
ps2_ui_draw(ps2_ui* ui) {
    ps2_draw* d = &ui->draw;
    d->alpha = 1.0f;
    d->ox = d->oy = 0.0f;
    draw_background(ui);

    /* leaving / arriving: fade and slide */
    float k = 1.0f;
    if (ui->transitioning) {
        float half = SCREEN_TRANSITION / 2.0f;
        k = smooth(ui->transitioning == 1 ? ui->trans / half : 1.0f - ui->trans / half);
    }
    d->alpha = k;
    d->oy = (1.0f - k) * 14.0f;
    switch (ui->screen) {
        case PS2_SCREEN_MAIN: draw_main(ui); break;
        case PS2_SCREEN_DEVICES: draw_devices(ui); break;
        case PS2_SCREEN_GAMES: draw_games(ui); break;
        case PS2_SCREEN_CARD: draw_card_screen(ui); break;
        case PS2_SCREEN_SETTINGS: draw_settings(ui); break;
        default: break;
    }
    if (ui->screen == PS2_SCREEN_GAMES && ui->recent_open) {
        int n = ui->host.recent_count ? ui->host.recent_count(ui->host.user) : 0;
        draw_popup(ui, "Recently played", n, ui->recent_sel, popup_recent_name);
    }
    d->alpha = 1.0f;
    d->oy = 0.0f;
    if (ui->notice_t > 0.0f) {
        ps2d_text(d, ui->notice, 320.0f, 396.0f, 0.55f, ps2_fade(COL_WHITE, clampf(ui->notice_t * 2.0f, 0.0f, 1.0f)), PS2_TEXT_CENTER);
    }
    if (ui->boot_t < BOOT_FADE) {
        /* PS2LOGO fades in from black: alpha = 240 * (N - f) / N, linear over 0.55 s */
        float black = 1.0f - ui->boot_t / BOOT_FADE;
        ps2d_rect(d, 0.0f, 0.0f, 640.0f, 480.0f, ps2_argb((int)(255.0f * black), 0, 0, 0));
    }
}
