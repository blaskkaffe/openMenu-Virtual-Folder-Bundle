/*
 * File: main.c
 * Minimal openMenu: boot, list the games on the GDEMU SD card, launch one.
 *
 * Everything else the full openMenu does (themes, artwork, online features,
 * music, VMU integration, settings screens) is left out on purpose. The
 * placeholder UI draws with the BIOS font straight into the framebuffer so
 * that nothing here depends on the PVR; the real front end replaces
 * draw_menu() later.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/video.h>
#include <kos.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include <bios_rom.h>

#include "launch.h"

#define SCREEN_W    640
#define LINE_H      (BFONT_HEIGHT + 4)
#define MARGIN_X    32
#define MARGIN_Y    48
#define VISIBLE     14
#define COL_FG      0xFFFF
#define COL_DIM     0x8410
#define COL_BG      0x0000
#define COL_SEL     0x001F

typedef enum { BTN_NONE, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B } button_t;

static char bios_status[64] = "";
static int cursor = 0;
static int top = 0;

static void
draw_text(int x, int y, uint16_t fg, uint16_t bg, const char* str) {
    bfont_draw_str_ex(vram_s + y * SCREEN_W + x, SCREEN_W, fg, bg, 16, 1, str);
}

static int
item_is_folder(const gd_item* item) {
    return item && !strncmp(item->disc, "DIR", 3);
}

static void
draw_menu(void) {
    char line[64];
    int count = list_length();

    vid_clear(0, 0, 0);
    draw_text(MARGIN_X, 8, COL_FG, COL_BG, "openMenu");
    draw_text(MARGIN_X, 480 - LINE_H - 8, COL_DIM, COL_BG, bios_status);

    if (count <= 0) {
        draw_text(MARGIN_X, MARGIN_Y, COL_DIM, COL_BG, "No games found. Run GD MENU Card Manager.");
        return;
    }

    for (int row = 0; row < VISIBLE && top + row < count; row++) {
        const gd_item* item = list_item_get(top + row);
        if (!item) {
            continue;
        }
        /* The BIOS font is 12 px wide: 640 - 2 * 32 leaves room for 48 characters. */
        snprintf(line, sizeof(line), "%s%.46s", item_is_folder(item) ? "> " : "  ", item->name);
        int sel = (top + row == cursor);
        draw_text(MARGIN_X, MARGIN_Y + row * LINE_H, COL_FG, sel ? COL_SEL : COL_BG, line);
    }
}

static button_t
poll_button(void) {
    static uint32_t prev = 0;
    static int held = 0;
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t* st = dev ? (cont_state_t*)maple_dev_status(dev) : NULL;
    uint32_t now = st ? st->buttons : 0;
    uint32_t edge = now & ~prev;
    button_t out = BTN_NONE;

    if (edge & CONT_A) {
        out = BTN_A;
    } else if (edge & CONT_B) {
        out = BTN_B;
    } else if (now & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) {
        /* Fire on the first frame, then repeat after ~0.4 s every 5 frames. */
        if ((edge & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)) || (held > 24 && held % 5 == 0)) {
            if (now & CONT_DPAD_UP) {
                out = BTN_UP;
            } else if (now & CONT_DPAD_DOWN) {
                out = BTN_DOWN;
            } else if (now & CONT_DPAD_LEFT) {
                out = BTN_LEFT;
            } else {
                out = BTN_RIGHT;
            }
        }
        held++;
    } else {
        held = 0;
    }

    prev = now;
    return out;
}

static void
move_cursor(int delta) {
    int count = list_length();
    if (count <= 0) {
        return;
    }
    cursor += delta;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > count - 1) {
        cursor = count - 1;
    }
    if (cursor < top) {
        top = cursor;
    }
    if (cursor >= top + VISIBLE) {
        top = cursor - VISIBLE + 1;
    }
}

/* Returns 1 when the screen needs redrawing. */
static int
handle_button(button_t btn) {
    const gd_item* item = list_item_get(cursor);

    switch (btn) {
        case BTN_UP: move_cursor(-1); return 1;
        case BTN_DOWN: move_cursor(1); return 1;
        case BTN_LEFT: move_cursor(-VISIBLE); return 1;
        case BTN_RIGHT: move_cursor(VISIBLE); return 1;
        case BTN_A:
            if (!item) {
                return 0;
            }
            if (item_is_folder(item)) {
                list_folder_enter(item->name, cursor);
                cursor = 0;
                top = 0;
                return 1;
            }
            launch_disc(item);
            return 1; /* only reached if the launch failed */
        case BTN_B:
            if (!list_folder_is_root()) {
                int restored = list_folder_go_back();
                cursor = restored < 0 ? 0 : restored;
                top = cursor > VISIBLE / 2 ? cursor - VISIBLE / 2 : 0;
                move_cursor(0);
                return 1;
            }
            return 0;
        default: return 0;
    }
}

int
main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    maple_wait_scan();

    vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);
    bfont_set_encoding(BFONT_CODE_ISO8859_1);

    /* The boot ROM is memory mapped (uncached) at 0xA0000000. Phase 1 only checks
     * that it can be read; the UI does not use it yet. */
    bios_rom rom;
    int rom_err = bios_rom_init(&rom, (const void*)0xA0000000, BIOS_ROM_SIZE);
    if (rom_err == BIOS_ROM_OK) {
        snprintf(bios_status, sizeof(bios_status), "BIOS %s: %d textures, %d scripts", rom.revision,
                 bios_texture_count(&rom), bios_script_count(&rom));
    } else {
        snprintf(bios_status, sizeof(bios_status), "BIOS ROM not usable (error %d)", rom_err);
    }

    savefile_init();

    if (list_read_default() == 0) {
        list_folder_init();
        list_set_sort_alphabetical();
    }

    draw_menu();
    for (;;) {
        vid_waitvbl();
        if (handle_button(poll_button())) {
            draw_menu();
        }
    }

    savefile_close();
    return 0;
}
