/*
 * Plain framebuffer game list, see ui_fallback.h. Draws with the BIOS font straight
 * into the framebuffer, no PVR involved.
 */
#include <stdio.h>
#include <string.h>

#include <dc/biosfont.h>
#include <dc/video.h>

#include "input.h"
#include "launch.h"
#include "ui_fallback.h"
#include "ui_list.h"

#define SCREEN_W 640
#define LINE_H (BFONT_HEIGHT + 4)
#define MARGIN_X 32
#define MARGIN_Y 48
#define COL_FG 0xFFFF
#define COL_DIM 0x8410
#define COL_BG 0x0000
#define COL_SEL 0x001F

static void
draw_text(int x, int y, uint16_t fg, uint16_t bg, const char* str) {
    bfont_draw_str_ex(vram_s + y * SCREEN_W + x, SCREEN_W, fg, bg, 16, 1, str);
}

static void
draw_menu(const char* status) {
    char line[64];
    int count = uil_count();

    vid_clear(0, 0, 0);
    draw_text(MARGIN_X, 8, COL_FG, COL_BG, "openMenu");
    draw_text(MARGIN_X, 480 - LINE_H - 8, COL_DIM, COL_BG, status);

    if (count <= 0) {
        draw_text(MARGIN_X, MARGIN_Y, COL_DIM, COL_BG, "No games found. Run GD MENU Card Manager.");
        return;
    }

    for (int row = 0; row < UIL_VISIBLE && uil_top() + row < count; row++) {
        const gd_item* item = uil_item(uil_top() + row);
        if (!item) {
            continue;
        }
        /* The BIOS font is 12 px wide: 640 - 2 * 32 leaves room for 48 characters. */
        snprintf(line, sizeof(line), "%s%.46s", uil_is_folder(item) ? "> " : "  ", item->name);
        draw_text(MARGIN_X, MARGIN_Y + row * LINE_H, COL_FG, (uil_top() + row == uil_cursor()) ? COL_SEL : COL_BG, line);
    }
}

void
ui_fallback_run(const char* status) {
    uil_reset();
    draw_menu(status);
    for (;;) {
        vid_waitvbl();
        const gd_item* game = NULL;
        uil_result r = uil_button(input_poll(), &game);
        if (r == UIL_LAUNCH) {
            launch_disc(game);
        }
        if (r != UIL_NONE) {
            draw_menu(status);
        }
    }
}
