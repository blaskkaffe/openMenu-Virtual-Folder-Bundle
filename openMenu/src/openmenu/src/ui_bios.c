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
#include <bios_case.h>
#include <bios_menu.h>
#include <bios_datetime.h>
#include <bios_list.h>
#include <bios_page.h>
#include <backend/db_list.h>
#include <kos/fs.h>

#include "clock.h"
#include "gfx.h"
#include "history.h"
#include "input.h"
#include "launch.h"
#include "serial_region.h"
#include "sound.h"
#include <openmenu_settings.h>

#include "ui_bios.h"
#include <backend/gd_list.h>

#include "ui_files.h"
#include "ui_list.h"
#include "ui_settings.h"
#include "video.h"

typedef enum { SCREEN_MAIN, SCREEN_GAMES, SCREEN_SETTINGS, SCREEN_DATETIME, SCREEN_FILES } screen_t;

/* The BIOS runs its logic at a fixed 60 steps per second and catches up when a frame takes
 * longer; the animations were written for that rate. */
/* Temporary performance aids: the frame rate readout, and while holding X the cloud layers
 * are skipped, while holding Y the icons are skipped. */
#define UI_DEBUG 1

#define STEPS_PER_SECOND 60
#define MAX_STEPS_PER_FRAME 4

enum { ICON_GAME, ICON_FILES, ICON_MUSIC, ICON_SETTINGS };

#ifndef GFX_MAIN_AUTOSORT
#define GFX_MAIN_AUTOSORT 1
#endif
#ifndef GFX_PRESORT
#define GFX_PRESORT 1
#endif

static const char* const icon_names[BMENU_ICONS] = {"Play", "File", "Music", "Settings"};

#define NOTICE_FRAMES 150

/* Start-up fade (decompile: gui_init 0x8C0101E0, main_menu_update 0x8C020BA0, fade_step_bg_colors 0x8C021620):
 * the background, and the border of the picture, start as one flat colour, the word the boot code left at 0x8C000060
 * with every channel limited to 0xC0 (gui_init reads exactly that). The first frame creates nothing, the second creates
 * the icons, the third the captions, and from the fourth frame on the fade runs one step per frame, 32 steps, to the
 * gradient (border to black). */
#define FADE_STEPS 32
#define FADE_HOLD_STEPS 3
#define BOOT_COLOR_ADDR 0x8C000060u
static int fade_step = -FADE_HOLD_STEPS;
static uint32_t start_color = 0xFF000000u;

static uint32_t
read_boot_color(void) {
    uint32_t v = *(volatile uint32_t*)BOOT_COLOR_ADDR;
    uint32_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    r = r > 0xC0 ? 0xC0 : r;
    g = g > 0xC0 ? 0xC0 : g;
    b = b > 0xC0 ? 0xC0 : b;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
#define START_COLOR start_color

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
static bpage page;
static blist glist;                 /* the game list screen */
static const gd_item* launch_pending; /* a game waiting for the launch animation to end */
static int have_meta;               /* META.DAT loaded: players, VMU blocks */
static bdt dtedit;                    /* the date and time editor */
static int about_open;                /* the About box over the settings */
static int saved_page_cursor, saved_page_top;
static const bios_rom* g_rom;
static mouse_t pointer;
static int settings_popup;     /* a settings row popup is open */
static int settings_popup_sel;
static int games_opened;  /* the game browser has been opened once (last game restored) */
static int recent_open;   /* recently played popup */
static int recent_row;
static int notice_frames;
static char fps_text[40];
static int show_debug; /* fps, triangles and the BIOS/sound line: X on the main menu switches them on and off */
static uint64_t build_us_sum;
static uint64_t phase_us[3]; /* begin frame, background, everything else drawn (summed per second) */
static uint64_t sync_us_sum;
static char perf_text[3][64]; /* the debug overlay's second and third line */
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

/* ---- Game list -------------------------------------------------------------------------- */

#define INFO_X 438.0f
#define INFO_W 182.0f
#define ART_SIZE 140.0f

/* Fit the lines of the right panel: `text` cut at spaces into lines of at most `width` characters. */
static int
wrap_text(const char* text, int width, char out[][20], int max_lines) {
    int lines = 0;
    const char* p = text;
    while (*p && lines < max_lines) {
        while (*p == ' ') p++;
        int len = (int)strlen(p);
        int take = len <= width ? len : width;
        if (len > width) {
            int cut = take;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut > 0) take = cut;
        }
        memcpy(out[lines], p, (size_t)take);
        out[lines][take] = '\0';
        p += take;
        lines++;
        if (lines == max_lines && *p) {
            int n = (int)strlen(out[lines - 1]);
            if (n > width - 2) n = width - 2;
            strcpy(out[lines - 1] + n, "..");
        }
    }
    return lines;
}

/* Scrolling of a long selected title: wait, scroll to the end, wait, start over. */
#define MARQUEE_HOLD 60  /* frames at each end */
#define MARQUEE_PX_PER_FRAME 1
#define TITLE_WINDOW 300       /* pixels available for a title */
#define TITLE_WINDOW_MULTI 232 /* ... on a multi-disc row, which has the disc pill */

static const gd_item* marquee_item;
static int marquee_frame;

/* Region of the console, as the BIOS stores it: 2 is Europe (PAL). */
static int
console_is_pal(void) {
    return (*(volatile uint8_t*)0x8C000072 & 0xF) == 2;
}

/* The CD case of the selected game (right panel): flies in from the side the selection came from. */
static bcase gcase;
static int gcase_prev_cursor = -1;
static const gd_item* gcase_prev_item;

static void
case_bind(void* user, const char* tag) {
    (void)user;
    gfx_model_bind_front(tag);
}

static void
case_update(void) {
    int cursor = uil_cursor();
    const gd_item* cur = uil_count() > 0 ? uil_item(cursor) : NULL;
    if (cursor != gcase_prev_cursor || cur != gcase_prev_item) {
        int dir = cursor >= gcase_prev_cursor ? 1 : -1;
        if (cur && !uil_is_folder(cur)) {
            int pal = cur->product[0] ? serial_is_pal(cur->product) : console_is_pal();
            bcase_show(&gcase, pal ? BMODEL_CASE_PAL : BMODEL_CASE_WHITE, cur->product, dir);
        } else {
            bcase_show(&gcase, -1, "", dir);
        }
        gcase_prev_cursor = cursor;
        gcase_prev_item = cur;
    }
    bcase_step(&gcase);
}

/* Row texts, disc pictures and the right panel's data for the frame about to be drawn. */
static void
games_sync(void) {
    char line[BPAGE_LABEL_MAX];
    case_update();
    blist_set_count(&glist, uil_count());
    blist_set_cursor(&glist, uil_cursor());
    glist.pal_console = console_is_pal();
    for (int s = 0; s < glist.slots; s++) {
        int row = blist_row_in_slot(&glist, s);
        const gd_item* item = row >= 0 ? uil_item(row) : NULL;
        line[0] = '\0';
        int multi = 0;
        if (item) {
            multi = !uil_is_folder(item) && uil_disc_total(item) > 1;
            size_t keep = multi ? 19 : 24; /* a multi-disc row has the disc pill at its right end */
            if (row == uil_cursor() && !glist.launching) {
                snprintf(line, sizeof(line), "%.41s", item->name); /* the selected title scrolls if it is long */
            } else if (strlen(item->name) > keep) {
                snprintf(line, sizeof(line), "%.*s..", (int)keep - 2, item->name);
            } else {
                snprintf(line, sizeof(line), "%s", item->name);
            }
        }
        glist.multi[s] = multi;
        gfx_set_row_scroll(s, 0, 0);
        if (item && row == uil_cursor() && !glist.launching) {
            int window = multi ? TITLE_WINDOW_MULTI : TITLE_WINDOW;
            int width = gfx_text_width(line);
            if (item != marquee_item) {
                marquee_item = item;
                marquee_frame = 0;
            }
            if (width > window) {
                int travel = width - window;
                int moving = travel / MARQUEE_PX_PER_FRAME;
                int t = marquee_frame % (2 * MARQUEE_HOLD + moving);
                int offset = t < MARQUEE_HOLD ? 0 : (t < MARQUEE_HOLD + moving ? (t - MARQUEE_HOLD) * MARQUEE_PX_PER_FRAME : travel);
                gfx_set_row_scroll(s, window, offset);
                marquee_frame++;
            }
        }
        if (multi) {
            int picked = row == uil_cursor() ? uil_disc_index() : 0;
            char num[12];
            snprintf(num, sizeof(num), "%d:%d", picked + 1, uil_disc_total(item));
            gfx_set_label((uint16_t)BLIST_NUM_ID(s), num);
        }
        gfx_set_label((uint16_t)BLIST_TEXT_ID(s), line);
        const char* serial = item && !uil_is_folder(item) ? item->product : "";
        gfx_art_bind_row(s, serial, serial[0] ? serial_is_pal(serial) : console_is_pal());
    }
    blist_sync(&glist);
}

/* Text on the five buttons at the bottom (the CD player's buttons without their pictures):
 * product code, region, players, VMU blocks, discs. */
static void
draw_button_text(const gd_item* cur) {
    char text[BLIST_BUTTONS][12];
    for (int i = 0; i < BLIST_BUTTONS; i++) {
        text[i][0] = '\0';
    }
    if (cur && !uil_is_folder(cur)) {
        snprintf(text[0], sizeof(text[0]), "%.6s", cur->product);
        snprintf(text[1], sizeof(text[1]), "%.4s", cur->region);
        int discs = cur->product[0] ? gd_item_disc_total(cur->disc) : 1;
        snprintf(text[4], sizeof(text[4]), "%dD", discs > 0 ? discs : 1);
        struct db_item* meta = NULL;
        if (have_meta && !db_get_meta(cur->product, &meta) && meta) {
            if (meta->num_players) {
                snprintf(text[2], sizeof(text[2]), "%dP", meta->num_players);
            }
            if (meta->vmu_blocks) {
                snprintf(text[3], sizeof(text[3]), "%dB", meta->vmu_blocks);
            }
        }
    }
    for (int i = 0; i < BLIST_BUTTONS; i++) {
        if (text[i][0]) {
            float x, y;
            blist_button_center_px(i, &x, &y);
            gfx_text(text[i], x - (float)gfx_text_width(text[i]) / 2.0f, y - (float)GFX_LINE_H / 2.0f, 0.5f, 0xFFFFFFFFu, 1);
        }
    }
}

static void
draw_games(void) {
    char lines[3][20];
    blist_draw(&glist, gfx_sink());
    if (uil_count() <= 0) {
        gfx_text("No games found.", 64.0f, 200.0f, 0.5f, 0xFFFFFFFFu, 1);
        return;
    }
    if (glist.launching) {
        return;
    }
    const gd_item* cur = uil_item(uil_cursor());
    draw_button_text(cur);
    float panel_top, panel_bottom;
    blist_rows_extent_px(&glist, &panel_top, &panel_bottom);
    const float art_y = panel_top + 8.0f;
    gfx_rrect(INFO_X - 6.0f, panel_top, INFO_W + 12.0f, panel_bottom - panel_top, 9.0f, 0.4f, 0xB25A5AA0u); /* the colour, alpha and corner radius of the row bars */
    bcase_draw(&gcase, &menu.scene, INFO_X + INFO_W / 2.0f, art_y + ART_SIZE / 2.0f, ART_SIZE + 10.0f, case_bind, NULL, gfx_sink());
    if (cur && !uil_is_folder(cur)) {
        float y = art_y + ART_SIZE + 6.0f;
        int n = wrap_text(cur->name, 15, lines, 3);
        for (int i = 0; i < n; i++) {
            gfx_text(lines[i], INFO_X, y, 0.5f, 0xFFFFFFFFu, 1);
            y += GFX_LINE_H - 4.0f;
        }
    } else if (cur) {
        gfx_text("Folder", INFO_X, art_y, 0.5f, 0xFFFFFFFFu, 1);
    }
    gfx_text("A:go X:recent", INFO_X, panel_bottom - 34.0f, 0.5f, 0xFFA0A0A0u, 0); /* B is the BACK marker */
}

#define POPUP_X 120.0f
#define POPUP_W 400.0f
#define POPUP_ROWS 8

/* The window panels take the colour of their screen, as in the BIOS (the colour of the main menu
 * item the screen belongs to): orange Game, green Files, blue Music / online, magenta Settings. */
static uint32_t
screen_accent(void) {
    switch (screen) {
        case SCREEN_GAMES: return BMENU_ACCENT_GAME;
        case SCREEN_FILES: return BMENU_ACCENT_FILES;
        case SCREEN_SETTINGS:
        case SCREEN_DATETIME: return BMENU_ACCENT_SETTINGS;
        default: return BMENU_ACCENT_MAIN;
    }
}

typedef const char* (*popup_name_fn)(int index);

/* y of the first row of a popup with `rows` rows: the popup panel is centred on the screen. */
static float
popup_rows_y(int rows) {
    float panel_h = (float)(rows + 2) * GFX_LINE_H + 16.0f + 20.0f;
    return 240.0f - panel_h / 2.0f + 50.0f;
}

/* A BIOS-style popup list: title, a few rows with the selected one highlighted, a hint line. */
static void
draw_popup(const char* title, const char* hint, int count, int sel, popup_name_fn name) {
    char line[64];
    int first = sel >= POPUP_ROWS ? sel - POPUP_ROWS + 1 : 0;
    int rows = count < POPUP_ROWS ? count : POPUP_ROWS;
    float h = (float)(rows + 2) * GFX_LINE_H + 16.0f;
    const float rows_y = popup_rows_y(rows);
    bscene_draw_panel(&menu.scene, POPUP_X - 10.0f, rows_y - 50.0f, POPUP_W + 20.0f, h + 20.0f, screen_accent(), gfx_sink());
    gfx_text(title, POPUP_X + 16.0f, rows_y - 32.0f, 0.7f, 0xFFFFFFFFu, 1);
    for (int i = 0; i < rows; i++) {
        float y = rows_y + (float)i * GFX_LINE_H;
        int is_sel = first + i == sel;
        if (is_sel) {
            gfx_rect(POPUP_X + 8.0f, y, POPUP_W - 16.0f, (float)GFX_LINE_H, 0.65f, 0x60FFFFFFu);
        }
        snprintf(line, sizeof(line), "%.38s", name(first + i));
        gfx_text(line, POPUP_X + 16.0f, y, 0.7f, is_sel ? 0xFFFFFFFFu : 0xFFC0C0C0u, is_sel);
    }
    gfx_text(hint, POPUP_X + 16.0f, rows_y + (float)rows * GFX_LINE_H + 2.0f, 0.7f, 0xFFA0A0A0u, 0);
}

static const char*
recent_name(int index) {
    const gd_item* g = history_recent(index);
    return g ? g->name : "?";
}

static const char*
settings_choice_name(int index) {
    return uis_choice_name(page.cursor, index);
}

/* ---- Settings page ---------------------------------------------------------------------- */

static void
page_row(void* user, int index, bpage_row* out) {
    (void)user;
    int icon = uis_icon(index);
    out->icon = icon >= 0 ? BPAGE_ICON_BIOS(icon) : BPAGE_ICON_DIGIT(index % 10); /* digits until real icons exist */
}

/* Text of the visible rows and of the help box, then the objects themselves. */
static void
settings_sync(void) {
    char line[BPAGE_LABEL_MAX];
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        int row = bpage_row_in_slot(&page, s);
        if (row >= 0) {
            snprintf(line, sizeof(line), "%.20s\t%.20s", uis_label(row), uis_value(row));
        } else {
            line[0] = '\0';
        }
        gfx_set_label((uint16_t)BPAGE_TEXT_ID(s), line);
    }
    snprintf(line, sizeof(line), "%.40s\n%.40s", uis_group(page.cursor), uis_help(page.cursor));
    gfx_set_label((uint16_t)BPAGE_HELP_ID, line);
    bpage_sync(&page, page_row, NULL);
}

static void
draw_about(void) {
    char line[48];
    bscene_draw_panel(&menu.scene, 110.0f, 90.0f, 420.0f, 270.0f, screen_accent(), gfx_sink());
    gfx_text("About", 144.0f, 108.0f, 0.7f, 0xFFFFFFFFu, 1);
#ifdef OPENMENU_BUILD_VERSION
    snprintf(line, sizeof(line), "openMenu  %.24s", OPENMENU_BUILD_VERSION);
#else
    snprintf(line, sizeof(line), "openMenu");
#endif
    gfx_text(line, 144.0f, 148.0f, 0.7f, 0xFFFFFFFFu, 0);
    snprintf(line, sizeof(line), "BIOS %.30s", g_rom ? g_rom->revision : "?");
    gfx_text(line, 144.0f, 180.0f, 0.7f, 0xFFC0C0C0u, 0);
    gfx_text(sound_status(), 144.0f, 212.0f, 0.7f, 0xFFC0C0C0u, 0);
    gfx_text("A 3D menu made from the", 144.0f, 252.0f, 0.7f, 0xFFA0A0A0u, 0);
    gfx_text("Dreamcast BIOS menu.", 144.0f, 280.0f, 0.7f, 0xFFA0A0A0u, 0);
    gfx_text("A: close", 144.0f, 316.0f, 0.7f, 0xFFA0A0A0u, 0);
}

static void
draw_settings(void) {
    bpage_draw(&page, gfx_sink());
    /* more rows above / below the four shown */
    if (page.top > 0) {
        gfx_text("^", 590.0f, 40.0f, 0.5f, 0xFFFFFFFFu, 1);
    }
    if (page.top + BPAGE_SLOTS < page.count) {
        gfx_text("v", 590.0f, 370.0f, 0.5f, 0xFFFFFFFFu, 1);
    }
    if (about_open) {
        draw_about();
    }
    if (settings_popup) {
        draw_popup(uis_label(page.cursor), "A: choose   B: cancel", uis_choice_count(page.cursor), settings_popup_sel,
                   settings_choice_name);
    }
}

static void
draw_datetime(void) {
    static const char* const help[4] = {"Set Date/Time. L/R on the", "controller moves the cursor.", "U/D on the controller", "changes the settings."};
    char text[24];
    bscene_draw_panel(&menu.scene, BDT_PANEL_X, BDT_PANEL_Y, BDT_PANEL_W, BDT_PANEL_H, screen_accent(), gfx_sink());
    for (int i = 0; i < 4; i++) {
        gfx_text(help[i], 320.0f - (float)gfx_text_width(help[i]) / 2.0f, BDT_PANEL_Y + 12.0f + (float)i * 26.0f, 0.5f,
                 0xFFFFFFFFu, 1);
    }
    bdt_draw(&dtedit, gfx_sink());
    bdt_format(&dtedit, text, sizeof(text));
    gfx_text(text, bdt_text_x(&dtedit), BDT_TEXT_Y, 0.5f, 0xFFFFFFFFu, 1);
    /* the names are written over the right half of the green ovals, a little lower (as in the BIOS) */
    gfx_text("Select", BDT_BUTTON_X + 9.0f, BDT_SELECT_Y - 14.0f, 0.5f, 0xFFD0D0D0u, 0);
    gfx_text("Cancel", BDT_BUTTON_X + 9.0f, BDT_CANCEL_Y - 14.0f, 0.5f, 0xFFD0D0D0u, 0);
}

/* The mouse pointer: the BIOS' green triangle (model 35) turned so its tip points to the top left
 * and sits on the pointer position. Drawn nearer than everything else. */
static void
draw_pointer(void) {
    if (!pointer.visible) {
        return;
    }
    float ux = (float)pointer.x;
    if (sf_aspect[0] == ASPECT_WIDE) {
        ux = 320.0f + (ux - 320.0f) / 0.75f; /* the picture is squeezed: place it where the screen shows it */
    }
    float tip = 13.6f * 0.7071f; /* the tip is 13.6 px from the model's origin, up and to the left */
    float ox = ux + tip, oy = (float)pointer.y + tip;
    bvm_obj o;
    memset(&o, 0, sizeof(o));
    o.active = 1;
    o.flags = BVM_F_MODEL;
    o.model = 35;
    o.texlist = 35;
    o.pos[0] = (ox - 320.0f) / 13.3333f; /* z = -300: 4000 / 300 pixels per unit */
    o.pos[1] = (240.0f - oy) / 13.3333f;
    o.pos[2] = -300.0f;
    o.rot[2] = 40960; /* 225 degrees: the model's tip points down, this turns it up and to the left */
    o.scale_tw[0].cur = o.scale_tw[1].cur = o.scale_tw[2].cur = 1.1f;
    menu.scene.parts = BSCENE_PART_ALL;
    bscene_draw_object(&menu.scene, &o, gfx_sink());
}

static void
draw_frame(void) {
    uint32_t top, bottom;
    uint64_t t0 = timer_us_gettime64();
    uint32_t held = UI_DEBUG ? input_buttons() : 0;
    gfx_set_aspect(sf_aspect[0] == ASPECT_WIDE);
    dcbg_gradient(&menu.bg, &top, &bottom);
    {
        uint32_t border = blend_color(START_COLOR, 0xFF000000u, fade_step < 0 ? 0 : fade_step);
        vid_border_color((int)((border >> 16) & 0xFF), (int)((border >> 8) & 0xFF), (int)(border & 0xFF));
    }
    if (fade_step < FADE_STEPS) {
        top = blend_color(START_COLOR, top, fade_step < 0 ? 0 : fade_step);
        bottom = blend_color(START_COLOR, bottom, fade_step < 0 ? 0 : fade_step);
    }
    /* The BIOS draws its main menu with the PVR sorting the translucent polygons per pixel: where an icon's parts and
     * its two copies overlap, the farther layer is always blended first. Other screens keep the build's default. */
    {
        int hw = screen == SCREEN_MAIN ? GFX_MAIN_AUTOSORT : !GFX_PRESORT;
        gfx_set_autosort(hw);
        menu.hw_autosort = hw;
    }
    gfx_begin_frame(top, bottom);
    uint64_t t1 = timer_us_gettime64();
    phase_us[0] += t1 - t0;

    if (screen == SCREEN_MAIN) {
        if (!(held & CONT_X)) {
            bscene_draw_background(&menu.bg, gfx_sink());
        }
        phase_us[1] += timer_us_gettime64() - t1;
        if (!(held & CONT_Y)) {
            bmenu_draw_objects(&menu, gfx_sink());
        }
    } else if (screen == SCREEN_SETTINGS) {
        bscene_draw_background(&menu.bg, gfx_sink());
        draw_settings();
    } else if (screen == SCREEN_DATETIME) {
        bscene_draw_background(&menu.bg, gfx_sink());
        draw_datetime();
    } else if (screen == SCREEN_FILES) {
        bscene_draw_background(&menu.bg, gfx_sink());
        uif_draw();
    } else {
        bscene_draw_background(&menu.bg, gfx_sink());
        if (screen == SCREEN_GAMES) {
            draw_games();
            if (recent_open) {
                draw_popup("Recently played", "A: start   B: close", history_recent_count(), recent_row, recent_name);
            }
        }
    }
    if (show_debug) {
        if (screen == SCREEN_MAIN) {
            gfx_text(status_line, TEXT_X, STATUS_Y, 0.5f, 0x80FFFFFFu, 0);
        }
        gfx_text(fps_text, TEXT_X, FPS_Y - 56.0f, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[0], TEXT_X, FPS_Y - 28.0f, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[1], TEXT_X, FPS_Y, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[2], TEXT_X, FPS_Y + 28.0f, 0.5f, 0xFFFFFFFFu, 0);
    }
    if (notice_frames > 0 && notice_text) {
        gfx_text(notice_text, TEXT_X, screen == SCREEN_MAIN ? NOTICE_Y_MAIN : NOTICE_Y_PANEL, 0.5f, 0xFFFFFFFFu, 1);
    }
    draw_pointer();
    phase_us[2] += timer_us_gettime64() - t0;
    build_us_sum += timer_us_gettime64() - t0;
    gfx_end_frame();
}

static void
handle_main(button_t b) {
    switch (b) {
        case BTN_X:
            show_debug = !show_debug;
            break;
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
                if (!games_opened) {
                    games_opened = 1;
                    int row = history_restore();
                    if (row >= 0) {
                        uil_goto_real(row);
                    }
                }
                blist_open(&glist, &menu, UIL_VISIBLE, uil_count());
                blist_set_cursor(&glist, uil_cursor());
                screen = SCREEN_GAMES;
            } else if (menu.selected == ICON_FILES) {
                sound_sfx(BAUDIO_SFX_ENTER);
                uif_open(&menu);
                uif_set_pal(console_is_pal());
                screen = SCREEN_FILES;
            } else if (menu.selected == ICON_SETTINGS) {
                sound_sfx(BAUDIO_SFX_ENTER);
                bpage_open(&page, &menu, uis_count());
                page.pal = console_is_pal();
                settings_popup = 0;
                screen = SCREEN_SETTINGS;
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                show_notice("Not available yet");
            }
            break;
        default: break;
    }
}

static void
handle_recent(button_t b) {
    int n = history_recent_count();
    switch (b) {
        case BTN_UP:
            if (recent_row > 0) {
                recent_row--;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_DOWN:
            if (recent_row < n - 1) {
                recent_row++;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_A:
        case BTN_START: {
            const gd_item* g = history_recent(recent_row);
            if (g) {
                sound_sfx(BAUDIO_SFX_ENTER);
                launch_disc(g); /* only returns if the launch failed */
                sound_sfx(BAUDIO_SFX_ERROR);
                show_notice("Could not start the game");
            }
            recent_open = 0;
            break;
        }
        case BTN_B:
        case BTN_X:
            sound_sfx(BAUDIO_SFX_CANCEL);
            recent_open = 0;
            break;
        default: break;
    }
}

static void
handle_games(button_t b) {
    const gd_item* game = NULL;
    if (recent_open) {
        handle_recent(b);
        return;
    }
    if (b == BTN_X) {
        if (history_recent_count() > 0) {
            recent_open = 1;
            recent_row = 0;
            sound_sfx(BAUDIO_SFX_ENTER);
        } else {
            sound_sfx(BAUDIO_SFX_ERROR);
            show_notice(sf_recently_played[0] == RECENTLY_PLAYED_OFF ? "Recently played is off (Settings)"
                                                                       : "No games played yet");
        }
        return;
    }
    if (launch_pending) {
        return; /* the launch animation is running */
    }
    if (glist.back_selected) {
        /* the cursor is on the BACK marker below the last row: up goes back to the list, A leaves */
        if (b == BTN_UP) {
            glist.back_selected = 0;
            sound_sfx(BAUDIO_SFX_CURSOR);
            return;
        }
        if (b == BTN_A || b == BTN_START) {
            sound_sfx(BAUDIO_SFX_CANCEL);
            glist.back_selected = 0;
            bmenu_show_main(&menu, ICON_GAME);
            screen = SCREEN_MAIN;
            return;
        }
        if (b != BTN_B) {
            return;
        }
    } else if (b == BTN_DOWN && uil_count() > 0 && uil_cursor() == uil_count() - 1) {
        glist.back_selected = 1;
        sound_sfx(BAUDIO_SFX_CURSOR);
        return;
    }
    uil_result r = uil_button(b, &game);
    if (r == UIL_LAUNCH) {
        if (sf_scroll_art[0] == SCROLL_ART_ON) { /* "Launch animation" setting */
            sound_sfx(BAUDIO_SFX_ENTER);
            launch_pending = game;
            blist_launch_start(&glist);
        } else {
            launch_disc(game); /* only returns if the launch failed */
            sound_sfx(BAUDIO_SFX_ERROR);
            show_notice("Could not start the game");
        }
    } else if (r == UIL_EXIT) {
        sound_sfx(BAUDIO_SFX_CANCEL);
        bmenu_show_main(&menu, ICON_GAME);
        screen = SCREEN_MAIN;
    } else if (r == UIL_REDRAW) {
        sound_sfx(b == BTN_A || b == BTN_START ? BAUDIO_SFX_CONFIRM : (b == BTN_B ? BAUDIO_SFX_CANCEL : BAUDIO_SFX_CURSOR));
    }
}

static void
leave_settings(void) {
    if (uis_commit() != 0) {
        sound_sfx(BAUDIO_SFX_ERROR);
        show_notice("Could not save settings");
    } else {
        sound_sfx(BAUDIO_SFX_CANCEL);
    }
    /* order, multi-disc and recent list changes show up in the list again */
    list_set_folder_root();
    uil_reset();
    bmenu_show_main(&menu, ICON_SETTINGS);
    screen = SCREEN_MAIN;
}

static void
enter_datetime(void) {
    int y, mo, d, h, mi;
    clock_get(&y, &mo, &d, &h, &mi);
    saved_page_cursor = page.cursor;
    saved_page_top = page.top;
    bdt_open(&dtedit, &menu, clock_date_order(), y, mo, d, h, mi);
    screen = SCREEN_DATETIME;
}

static void
leave_datetime(void) {
    bpage_open(&page, &menu, uis_count());
    page.pal = console_is_pal();
    page.cursor = saved_page_cursor;
    page.top = saved_page_top;
    screen = SCREEN_SETTINGS;
}

static void
handle_datetime(button_t b) {
    switch (b) {
        case BTN_LEFT:
            if (bdt_move(&dtedit, -1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_RIGHT:
            if (bdt_move(&dtedit, 1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_UP:
            if (bdt_change(&dtedit, 1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_DOWN:
            if (bdt_change(&dtedit, -1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_A:
        case BTN_START:
            if (dtedit.cursor == BDT_SELECT) {
                if (clock_set(dtedit.year, dtedit.month, dtedit.day, dtedit.hour, dtedit.minute) == 0) {
                    sound_sfx(BAUDIO_SFX_CONFIRM);
                    update_clock();
                } else {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    show_notice("Could not set the clock");
                }
                leave_datetime();
            } else if (dtedit.cursor == BDT_CANCEL) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                leave_datetime();
            } else {
                bdt_move(&dtedit, 1); /* A on a field moves on to the next one */
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            leave_datetime();
            break;
        default: break;
    }
}

static void
handle_settings(button_t b) {
    int row = page.cursor;
    if (about_open) {
        if (b == BTN_A || b == BTN_B || b == BTN_START) {
            about_open = 0;
            sound_sfx(BAUDIO_SFX_CANCEL);
        }
        return;
    }
    if (settings_popup) {
        int n = uis_choice_count(row);
        switch (b) {
            case BTN_UP:
                if (settings_popup_sel > 0) {
                    settings_popup_sel--;
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
                break;
            case BTN_DOWN:
                if (settings_popup_sel < n - 1) {
                    settings_popup_sel++;
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
                break;
            case BTN_A:
            case BTN_START:
                uis_set(row, settings_popup_sel);
                sound_sfx(BAUDIO_SFX_CONFIRM);
                settings_popup = 0;
                break;
            case BTN_B:
                sound_sfx(BAUDIO_SFX_CANCEL);
                settings_popup = 0;
                break;
            default: break;
        }
        return;
    }
    switch (b) {
        /* the d-pad as the BIOS Settings cursor table (0x8C037D70): rows wrap, left and right go to BACK */
        case BTN_UP:
            if (bpage_nav(&page, BPAGE_UP)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_DOWN:
            if (bpage_nav(&page, BPAGE_DOWN)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_LEFT:
            if (bpage_nav(&page, BPAGE_LEFT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_RIGHT:
            if (bpage_nav(&page, BPAGE_RIGHT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_A:
            if (page.back_selected) {
                leave_settings();
            } else if (uis_action(row) == UIS_ACTION_DATETIME) {
                sound_sfx(BAUDIO_SFX_ENTER);
                enter_datetime();
            } else if (uis_action(row) == UIS_ACTION_ABOUT) {
                sound_sfx(BAUDIO_SFX_ENTER);
                about_open = 1;
            } else if (uis_opens_popup(row)) {
                settings_popup = 1;
                settings_popup_sel = uis_get(row);
                sound_sfx(BAUDIO_SFX_ENTER);
            } else {
                uis_change(row, 1);
                sound_sfx(BAUDIO_SFX_CONFIRM);
            }
            break;
        case BTN_B:
        case BTN_START: leave_settings(); break;
        default: break;
    }
}

/* The mouse over a row selects it, as the d-pad would. */
static void
apply_hover(void) {
    float ux = (float)pointer.x, uy = (float)pointer.y;
    if (sf_aspect[0] == ASPECT_WIDE) {
        ux = 320.0f + (ux - 320.0f) / 0.75f;
    }
    int popup_rows = 0, popup_sel = -1;
    if (recent_open) {
        popup_rows = history_recent_count();
    } else if (settings_popup) {
        popup_rows = uis_choice_count(page.cursor);
    }
    if (recent_open || settings_popup) {
        int sel = recent_open ? recent_row : settings_popup_sel;
        int first = sel >= POPUP_ROWS ? sel - POPUP_ROWS + 1 : 0;
        int rows = popup_rows < POPUP_ROWS ? popup_rows : POPUP_ROWS;
        const float rows_y = popup_rows_y(rows);
        if (ux >= POPUP_X && ux <= POPUP_X + POPUP_W && uy >= rows_y && uy < rows_y + (float)rows * GFX_LINE_H) {
            popup_sel = first + (int)((uy - rows_y) / GFX_LINE_H);
            if (recent_open) {
                recent_row = popup_sel;
            } else {
                settings_popup_sel = popup_sel;
            }
        }
        return;
    }
    if (about_open) {
        return;
    }
    switch (screen) {
        case SCREEN_MAIN: {
            int i = (uy > 270.0f ? 2 : 0) + (ux >= 320.0f ? 1 : 0);
            if (bmenu_select(&menu, i)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        }
        case SCREEN_GAMES: {
            if (launch_pending) {
                break;
            }
            if (blist_back_at_px(ux, uy)) {
                if (!glist.back_selected) sound_sfx(BAUDIO_SFX_CURSOR);
                glist.back_selected = 1;
                break;
            }
            int row = blist_row_in_slot(&glist, blist_slot_at_px(&glist, ux, uy));
            if (row >= 0) {
                glist.back_selected = 0;
                if (row != uil_cursor()) {
                    uil_set_cursor(row);
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
            }
            break;
        }
        case SCREEN_FILES: uif_hover(ux, uy); break;
        case SCREEN_SETTINGS: {
            if (bpage_back_at_px(ux, uy)) {
                if (!page.back_selected) sound_sfx(BAUDIO_SFX_CURSOR);
                page.back_selected = 1;
                break;
            }
            int row = bpage_row_in_slot(&page, bpage_slot_at_px(&page, ux, uy));
            if (row >= 0 && bpage_set_cursor(&page, row)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        }
        default: break;
    }
}

int
ui_bios_run(const bios_rom* rom) {
    g_rom = rom;
    if (gfx_init(rom) != 0) {
        return -1;
    }

    gfx_load_logo("/cd/LOGO.PVR"); /* optional replacement of the Dreamcast logo */
    /* game info (players, VMU blocks) comes from META.DAT on the menu disc; db_load_DAT needs it */
    file_t meta_fd = fs_open("/cd/META.DAT", O_RDONLY);
    if (meta_fd != FILEHND_INVALID) {
        fs_close(meta_fd);
        db_load_DAT();
        have_meta = 1;
    }

    sound_init(rom);
    snprintf(status_line, sizeof(status_line), "BIOS %s  %s  boot %08X", rom->revision, sound_status(),
             (unsigned)*(volatile uint32_t*)BOOT_COLOR_ADDR);

    start_color = read_boot_color();
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
        uint64_t logic_t0 = timer_us_gettime64();
        button_t b = input_poll();
        input_mouse(&pointer);
        if (pointer.moved) {
            apply_hover();
        }
        int typed = input_typed_char();
        if (typed && screen == SCREEN_GAMES && !recent_open && !launch_pending && uil_jump_to_letter(typed)) {
            sound_sfx(BAUDIO_SFX_CURSOR);
        }
        if (screen == SCREEN_MAIN) {
            handle_main(b);
        } else if (screen == SCREEN_GAMES) {
            handle_games(b);
        } else if (screen == SCREEN_DATETIME) {
            handle_datetime(b);
        } else if (screen == SCREEN_FILES) {
            if (uif_handle(b)) {
                bmenu_show_main(&menu, ICON_FILES);
                screen = SCREEN_MAIN;
            }
        } else {
            handle_settings(b);
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
            if (launch_pending && blist_launch_step(&glist)) {
                const gd_item* g = launch_pending;
                launch_pending = NULL;
                launch_disc(g); /* only returns if the launch failed */
                blist_launch_cancel(&glist);
                sound_sfx(BAUDIO_SFX_ERROR);
                show_notice("Could not start the game");
            }
        }

        if (screen == SCREEN_SETTINGS) {
            settings_sync();
        } else if (screen == SCREEN_DATETIME) {
            bdt_sync(&dtedit);
        } else if (screen == SCREEN_FILES) {
            uif_sync();
        } else if (screen == SCREEN_GAMES) {
            games_sync();
        }

        sync_us_sum += timer_us_gettime64() - logic_t0;
        frames_this_second++;
        if (now - fps_since >= 1000) {
            update_clock();
            gfx_stats gs;
            gfx_stats_take(&gs);
            unsigned n = frames_this_second ? (unsigned)frames_this_second : 1u;
            unsigned sync_t = (unsigned)(sync_us_sum / n / 100u);               /* tenths of a millisecond per frame */
            unsigned wait_t = gs.wait_us / n / 100u;
            unsigned total_t = (unsigned)(build_us_sum / n / 100u);
            unsigned build_t = total_t > wait_t ? total_t - wait_t : 0u;       /* drawing without the wait for the PVR */
            snprintf(fps_text, sizeof(fps_text), "%d fps  %u tris", frames_this_second, gfx_triangles());
            snprintf(perf_text[0], sizeof(perf_text[0]), "sync %u.%u build %u.%u wait %u.%u ms", sync_t / 10, sync_t % 10,
                     build_t / 10, build_t % 10, wait_t / 10, wait_t % 10);
            snprintf(perf_text[1], sizeof(perf_text[1]), "art %u (%u ms) text %u hdr %u", gs.art_loads, gs.art_us / 1000u,
                     gs.text_uploads, gs.headers / n);
            {
                unsigned beg = (unsigned)(phase_us[0] / n / 100u), bg = (unsigned)(phase_us[1] / n / 100u),
                         all = (unsigned)(phase_us[2] / n / 100u);
                snprintf(perf_text[2], sizeof(perf_text[2]), "begin %u.%u bg %u.%u drawn %u.%u ms", beg / 10, beg % 10, bg / 10,
                         bg % 10, all / 10, all % 10);
                phase_us[0] = phase_us[1] = phase_us[2] = 0;
            }
            sync_us_sum = 0;
            build_us_sum = 0;
            frames_this_second = 0;
            fps_since = now;
        }
        draw_frame(); /* pvr_wait_ready() inside paces this to the display */
    }
    return 0;
}
