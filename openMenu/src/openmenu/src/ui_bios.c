/*
 * BIOS-style menu on the PVR, see ui_bios.h.
 */
#include <stdio.h>
#include <string.h>

#include <dc/video.h>

#include <bios_menu.h>

#include "gfx.h"
#include "input.h"
#include "launch.h"
#include "ui_bios.h"
#include "ui_list.h"

typedef enum { SCREEN_MAIN, SCREEN_GAMES } screen_t;

enum { ICON_GAME, ICON_FILES, ICON_MUSIC, ICON_SETTINGS };

static const char* const icon_names[BMENU_ICONS] = {"Game", "Files", "Music", "Settings"};

#define NOTICE_FRAMES 150

static bmenu menu;
static screen_t screen;
static int notice_frames;
static const char* notice_text;

static void
show_notice(const char* text) {
    notice_text = text;
    notice_frames = NOTICE_FRAMES;
}

static void
draw_games(void) {
    char line[64];
    int count = uil_count();

    gfx_rect(40.0f, 56.0f, 560.0f, 40.0f + UIL_VISIBLE * GFX_LINE_H + 52.0f, 0.4f, 0x90000000u);
    gfx_text("Game", 56.0f, 62.0f, 0.5f, 0xFFFFFFFFu, 1);

    if (count <= 0) {
        gfx_text("No games found.", 56.0f, 108.0f, 0.5f, 0xFFC0C0C0u, 0);
        return;
    }
    for (int row = 0; row < UIL_VISIBLE && uil_top() + row < count; row++) {
        const gd_item* item = uil_item(uil_top() + row);
        if (!item) {
            continue;
        }
        float y = 104.0f + (float)row * GFX_LINE_H;
        int selected = (uil_top() + row == uil_cursor());
        if (selected) {
            gfx_rect(48.0f, y, 544.0f, (float)GFX_LINE_H, 0.45f, 0x50FFFFFFu);
        }
        snprintf(line, sizeof(line), "%s%.40s", uil_is_folder(item) ? "> " : "", item->name);
        gfx_text(line, 56.0f, y, 0.5f, selected ? 0xFFFFFFFFu : 0xFFC0C0C0u, selected);
    }
    gfx_text("A: start   B: back", 56.0f, 104.0f + UIL_VISIBLE * GFX_LINE_H + 8.0f, 0.5f, 0xFFA0A0A0u, 0);
}

static void
draw_frame(void) {
    uint32_t top, bottom;
    dcbg_gradient(&menu.bg, &top, &bottom);
    gfx_begin_frame(top, bottom);

    if (screen == SCREEN_MAIN) {
        bmenu_draw(&menu, gfx_sink());
    } else {
        bscene_draw_background(&menu.bg, gfx_sink());
        draw_games();
    }
    if (notice_frames > 0 && notice_text) {
        gfx_text(notice_text, 32.0f, 430.0f, 0.5f, 0xFFFFFFFFu, 1);
    }
    gfx_end_frame();
}

static void
handle_main(button_t b) {
    switch (b) {
        case BTN_UP: bmenu_move(&menu, BMENU_UP); break;
        case BTN_DOWN: bmenu_move(&menu, BMENU_DOWN); break;
        case BTN_LEFT: bmenu_move(&menu, BMENU_LEFT); break;
        case BTN_RIGHT: bmenu_move(&menu, BMENU_RIGHT); break;
        case BTN_A:
        case BTN_START:
            if (menu.selected == ICON_GAME) {
                uil_reset();
                screen = SCREEN_GAMES;
            } else {
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
        show_notice("Could not start the game");
    } else if (r == UIL_EXIT) {
        screen = SCREEN_MAIN;
    }
}

int
ui_bios_run(const bios_rom* rom) {
    if (gfx_init(rom) != 0) {
        return -1;
    }

    bmenu_init(&menu, rom, NULL);
    bmenu_show_main(&menu, ICON_GAME);
    for (int i = 0; i < BMENU_ICONS; i++) {
        gfx_set_label((uint16_t)BMENU_ID_ICON(i), icon_names[i]);
    }
    gfx_set_label(BMENU_ID_HEADER, "openMenu");

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
        bmenu_update(&menu);
        draw_frame(); /* pvr_wait_ready() inside paces this to the display */
    }
    return 0;
}
