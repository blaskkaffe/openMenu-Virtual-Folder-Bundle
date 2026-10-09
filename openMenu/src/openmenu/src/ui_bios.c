/*
 * BIOS-style menu on the PVR, see ui_bios.h.
 */
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/maple/controller.h>
#include <dc/video.h>

#include <bios_audio.h>
#include <bios_menu.h>

#include "gfx.h"
#include "input.h"
#include "launch.h"
#include "sound.h"
#include "ui_bios.h"
#include "ui_list.h"
#include "video.h"

typedef enum { SCREEN_MAIN, SCREEN_GAMES } screen_t;

/* The BIOS runs its logic at a fixed 60 steps per second and catches up when a frame takes
 * longer; the animations were written for that rate. */
/* Temporary performance aids: the frame rate readout, and while holding X the cloud layers
 * are skipped, while holding Y the icons are skipped. */
#define UI_DEBUG 1

#define STEPS_PER_SECOND 60
#define MAX_STEPS_PER_FRAME 4

enum { ICON_GAME, ICON_FILES, ICON_MUSIC, ICON_SETTINGS };

static const char* const icon_names[BMENU_ICONS] = {"Game", "Files", "Music", "Settings"};

#define NOTICE_FRAMES 150

/* Start-up fade (decompile: gui_init, fade_step_bg_colors): the background starts as one flat
 * colour, the colour the boot animation leaves behind (white-ish), and blends linearly to the
 * gradient over 32 logic steps. START_COLOR is that flat colour. */
#define FADE_STEPS 32
#define START_COLOR 0xFFFFFFFFu
static int fade_step;

static uint32_t
blend_color(uint32_t from, uint32_t to, int t) {
    uint32_t out = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        int a = (int)((from >> sh) & 0xFF);
        int b = (int)((to >> sh) & 0xFF);
        out |= (uint32_t)(a + (b - a) * t / FADE_STEPS) << sh;
    }
    return out;
}

/* Layout in the safe area (about 5% of the picture is cropped by many TVs and scalers) */
#define PANEL_X 48.0f
#define PANEL_Y 56.0f
#define PANEL_W 544.0f
#define TITLE_Y 60.0f
#define ROWS_Y 96.0f
#define TEXT_X 64.0f
#define STATUS_Y 432.0f
#define NOTICE_Y_MAIN 404.0f
#define NOTICE_Y_PANEL 424.0f
#define FPS_Y 376.0f

static bmenu menu;
static screen_t screen;
static int notice_frames;
static char fps_text[40];
static uint64_t build_us_sum;
static const char* notice_text;

static char status_line[64];

/* The BIOS shows the date and time in the header bar; the console's clock holds local time. */
static void
update_clock(void) {
    char text[24];
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    if (t) {
        snprintf(text, sizeof(text), "%02d/%02d/%04d  %02d:%02d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour,
                 t->tm_min);
        gfx_set_label(BMENU_ID_HEADER, text);
    }
}

static void
show_notice(const char* text) {
    notice_text = text;
    notice_frames = NOTICE_FRAMES;
}

static void
draw_games(void) {
    char line[64];
    int count = uil_count();

    gfx_rect(PANEL_X, PANEL_Y, PANEL_W, (ROWS_Y - PANEL_Y) + UIL_VISIBLE * GFX_LINE_H + 40.0f, 0.4f, 0x90000000u);
    gfx_text("Game", TEXT_X, TITLE_Y, 0.5f, 0xFFFFFFFFu, 1);

    if (count <= 0) {
        gfx_text("No games found.", TEXT_X, ROWS_Y + 4.0f, 0.5f, 0xFFC0C0C0u, 0);
        return;
    }
    for (int row = 0; row < UIL_VISIBLE && uil_top() + row < count; row++) {
        const gd_item* item = uil_item(uil_top() + row);
        if (!item) {
            continue;
        }
        float y = ROWS_Y + (float)row * GFX_LINE_H;
        int selected = (uil_top() + row == uil_cursor());
        if (selected) {
            gfx_rect(PANEL_X + 8.0f, y, PANEL_W - 16.0f, (float)GFX_LINE_H, 0.45f, 0x50FFFFFFu);
        }
        snprintf(line, sizeof(line), "%s%.40s", uil_is_folder(item) ? "> " : "", item->name);
        gfx_text(line, TEXT_X, y, 0.5f, selected ? 0xFFFFFFFFu : 0xFFC0C0C0u, selected);
    }
    gfx_text("A: start   B: back", TEXT_X, ROWS_Y + UIL_VISIBLE * GFX_LINE_H + 4.0f, 0.5f, 0xFFA0A0A0u, 0);
}

static void
draw_frame(void) {
    uint32_t top, bottom;
    uint64_t t0 = timer_us_gettime64();
    uint32_t held = UI_DEBUG ? input_buttons() : 0;
    dcbg_gradient(&menu.bg, &top, &bottom);
    if (fade_step < FADE_STEPS) {
        top = blend_color(START_COLOR, top, fade_step);
        bottom = blend_color(START_COLOR, bottom, fade_step);
    }
    gfx_begin_frame(top, bottom);

    if (screen == SCREEN_MAIN) {
        if (!(held & CONT_X)) {
            bscene_draw_background(&menu.bg, gfx_sink());
        }
        if (!(held & CONT_Y)) {
            bmenu_draw_objects(&menu, gfx_sink());
        }
    } else {
        bscene_draw_background(&menu.bg, gfx_sink());
        draw_games();
    }
    if (screen == SCREEN_MAIN) {
        gfx_text(status_line, TEXT_X, STATUS_Y, 0.5f, 0x80FFFFFFu, 0);
    }
    gfx_text(fps_text, TEXT_X, FPS_Y, 0.5f, 0x80FFFFFFu, 0);
    if (notice_frames > 0 && notice_text) {
        gfx_text(notice_text, TEXT_X, screen == SCREEN_MAIN ? NOTICE_Y_MAIN : NOTICE_Y_PANEL, 0.5f, 0xFFFFFFFFu, 1);
    }
    build_us_sum += timer_us_gettime64() - t0;
    gfx_end_frame();
}

static void
handle_main(button_t b) {
    switch (b) {
        case BTN_UP:
            if (bmenu_move(&menu, BMENU_UP)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_DOWN:
            if (bmenu_move(&menu, BMENU_DOWN)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_LEFT:
            if (bmenu_move(&menu, BMENU_LEFT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_RIGHT:
            if (bmenu_move(&menu, BMENU_RIGHT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_A:
        case BTN_START:
            if (menu.selected == ICON_GAME) {
                sound_sfx(BAUDIO_SFX_ENTER);
                uil_reset();
                screen = SCREEN_GAMES;
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                show_notice("Not available yet");
            }
            break;
        default: break;
    }
}

static void
handle_games(button_t b) {
    const gd_item* game = NULL;
    uil_result r = uil_button(b, &game);
    if (r == UIL_LAUNCH) {
        launch_disc(game); /* only returns if the launch failed */
        sound_sfx(BAUDIO_SFX_ERROR);
        show_notice("Could not start the game");
    } else if (r == UIL_EXIT) {
        sound_sfx(BAUDIO_SFX_CANCEL);
        screen = SCREEN_MAIN;
    } else if (r == UIL_REDRAW) {
        sound_sfx(b == BTN_A || b == BTN_START ? BAUDIO_SFX_CONFIRM : (b == BTN_B ? BAUDIO_SFX_CANCEL : BAUDIO_SFX_CURSOR));
    }
}

int
ui_bios_run(const bios_rom* rom) {
    if (gfx_init(rom) != 0) {
        return -1;
    }

    sound_init(rom);
    snprintf(status_line, sizeof(status_line), "BIOS %s  %s", rom->revision, sound_status());

    bmenu_init(&menu, rom, NULL);
    bmenu_show_main(&menu, ICON_GAME);
    for (int i = 0; i < BMENU_ICONS; i++) {
        gfx_set_label((uint16_t)BMENU_ID_ICON(i), icon_names[i]);
    }
    update_clock();

    /* Cover the first frames (texture uploads) with the start colour so nothing pops in. */
    for (int i = 0; i < 3; i++) {
        gfx_begin_frame(START_COLOR, START_COLOR);
        gfx_end_frame();
    }

    uint64_t last_ms = timer_ms_gettime64();
    uint64_t fps_since = last_ms;
    uint32_t step_credit = 0;
    int frames_this_second = 0;

    for (;;) {
        button_t b = input_poll();
        if (screen == SCREEN_MAIN) {
            handle_main(b);
        } else {
            handle_games(b);
        }
        if (notice_frames > 0) {
            notice_frames--;
        }

        /* Real-time logic steps: as many as the elapsed time asks for. */
        uint64_t now = timer_ms_gettime64();
        uint64_t elapsed = now - last_ms;
        last_ms = now;
        if (elapsed > 100) {
            elapsed = 100; /* after a stall do not fast-forward */
        }
        step_credit += (uint32_t)elapsed * (uint32_t)video_refresh_hz();
        int steps = (int)(step_credit / 1000);
        step_credit %= 1000;
        if (steps > MAX_STEPS_PER_FRAME) {
            steps = MAX_STEPS_PER_FRAME;
        }
        for (int i = 0; i < steps; i++) {
            bmenu_update(&menu);
            if (fade_step < FADE_STEPS) {
                fade_step++;
            }
        }

        frames_this_second++;
        if (now - fps_since >= 1000) {
            update_clock();
            snprintf(fps_text, sizeof(fps_text), "%d fps %u tri b%u ms", frames_this_second, gfx_triangles(),
                     frames_this_second ? (unsigned)(build_us_sum / 1000 / (uint64_t)frames_this_second) : 0u);
            build_us_sum = 0;
            frames_this_second = 0;
            fps_since = now;
        }
        draw_frame(); /* pvr_wait_ready() inside paces this to the display */
    }
    return 0;
}
