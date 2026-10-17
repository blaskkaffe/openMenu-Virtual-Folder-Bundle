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
#include "dbglog.h"
#include "serial_log.h"
#include "qr.h"
#include <bios_case.h>
#include <bios_menu.h>
#include <bios_datetime.h>
#include <bios_list.h>
#include <bios_page.h>
#include <backend/db_list.h>
#include <kos/fs.h>

#include "clock.h"
#include <bios_surface.h>
#include "gfx.h"
#include "history.h"
#include "input.h"
#include "launch.h"
#include "serial_region.h"
#include "sound.h"
#include <openmenu_settings.h>

#include "online.h"
#include "ui_bios.h"
#include "ui_internet.h"
#include <backend/gd_list.h>

#include "ui_files.h"
#include <bios_cdplayer.h>
#include "ui_list.h"
#include "ui_settings.h"
#include "video.h"

typedef enum { SCREEN_MAIN, SCREEN_GAMES, SCREEN_SETTINGS, SCREEN_DATETIME, SCREEN_FILES, SCREEN_MUSIC, SCREEN_INTERNET } screen_t;

/* The BIOS runs its logic at a fixed 60 steps per second and catches up when a frame takes
 * longer; the animations were written for that rate. */
#define STEPS_PER_SECOND 60
#define MAX_STEPS_PER_FRAME 4

enum { ICON_GAME, ICON_FILES, ICON_MUSIC, ICON_SETTINGS };

#ifndef GFX_MAIN_AUTOSORT
#define GFX_MAIN_AUTOSORT 1
#endif

static const char* const icon_names[BMENU_ICONS] = {"Play", "File", "Music", "Settings"};

/* The main menu's icon labels: the Music icon is "Internet" while the online features are on, manual or automatic
 * connect (Settings, Online). The Internet screen is still to come: the icon opens the music player for now. */
static void
set_icon_labels(void) {
    for (int i = 0; i < BMENU_ICONS; i++) {
        gfx_set_label((uint16_t)BMENU_ID_ICON(i), i == ICON_MUSIC && sf_dcnow[0] != DCNOW_OFF ? "Internet" : icon_names[i]);
    }
}

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
static int have_meta;               /* META.DAT loaded: players, VMU blocks */
static bcdplayer cdp;                 /* the Music screen: the BIOS CD player, its buttons do nothing yet */
static bdt dtedit;                    /* the date and time editor */
static int about_open;                /* the About box over the settings */
static int saved_page_cursor, saved_page_top;
static const bios_rom* g_rom;
static mouse_t pointer;

/* x of the pointer on the 640 px picture (16:9 squeezes the picture: undo that) */
static float
pointer_screen_x(void) {
    float ux = (float)pointer.x;
    return sf_aspect[0] == ASPECT_WIDE ? 320.0f + (ux - 320.0f) / 0.75f : ux;
}
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
static int report_built;
static int report_page; /* 0 off, 1 the table of the debug log, 2 the QR code with the same numbers */
static dbglog_sample last_sample;

static char status_line[64];

/* The BIOS shows the date and time in the header bar, in its own small antialiased font (fx_debug_hex_text
 * 0x8C021FC0): the header's 256x32 text surface, from (40, 16). Format of format_datetime_string 0x8C01BAE0 for the
 * console's date order; the console's clock holds local time. */
static void
update_clock(void) {
    char text[24];
    int y, mo, d, h, mi;
    clock_get(&y, &mo, &d, &h, &mi);
    switch (clock_date_order()) {
        case 0: snprintf(text, sizeof(text), "%04d/%02d/%02d %02d:%02d", y, mo, d, h, mi); break;
        case 2: snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", d, mo, y, h, mi); break;
        default: snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", mo, d, y, h, mi); break;
    }
    static char shown[24];
    static uint32_t shown_serial;
    bsurf* s = bsurf_get(BMENU_ID_HEADER, 256, 32);
    if (s && (strcmp(shown, text) != 0 || s->serial != shown_serial)) {
        bsurf_clear(s);
        bsurf_clock_text(s, 40, 16, text);
        strcpy(shown, text);
        shown_serial = s->serial;
    } else if (!s) {
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

/* A new case waits for its box art (at most CASE_ART_WAIT frames), so its front does not pop in after it has
 * arrived; the pictures of the next and previous games are read ahead while the cursor rests. */
#define CASE_ART_WAIT 10
static int case_wait;

static void
case_prefetch(int cursor) {
    static const int ahead[] = {1, -1, 2, -2};
    for (unsigned i = 0; i < sizeof(ahead) / sizeof(ahead[0]); i++) {
        int row = cursor + ahead[i];
        if (row < 0 || row >= uil_count()) {
            continue;
        }
        const gd_item* it = uil_item(row);
        if (it && !uil_is_folder(it) && it->product[0]) {
            gfx_art_prefetch(it->product, 1);
        }
    }
}

static void
case_update(void) {
    int cursor = uil_cursor();
    const gd_item* cur = uil_count() > 0 ? uil_item(cursor) : NULL;
    if (cursor != gcase_prev_cursor || cur != gcase_prev_item) {
        int ready = !cur || uil_is_folder(cur) || gfx_art_prefetch(cur->product, 1);
        if (!ready && case_wait < CASE_ART_WAIT) {
            case_wait++; /* the old case stays a little longer */
            bcase_step(&gcase);
            return;
        }
        case_wait = 0;
        int dir = cursor >= gcase_prev_cursor ? 1 : -1;
        if (cur && !uil_is_folder(cur)) {
            int pal = cur->product[0] ? serial_is_pal(cur->product) : console_is_pal();
            bcase_show(&gcase, pal ? BMODEL_CASE_PAL : BMODEL_CASE_WHITE, cur->product, dir);
        } else {
            bcase_show(&gcase, -1, "", dir);
        }
        gcase_prev_cursor = cursor;
        gcase_prev_item = cur;
    } else {
        case_prefetch(cursor);
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
            if (row == uil_cursor()) {
                snprintf(line, sizeof(line), "%.41s", item->name); /* the selected title scrolls if it is long */
            } else if (strlen(item->name) > keep) {
                snprintf(line, sizeof(line), "%.*s..", (int)keep - 2, item->name);
            } else {
                snprintf(line, sizeof(line), "%s", item->name);
            }
        }
        glist.multi[s] = multi;
        gfx_set_row_scroll(s, 0, 0);
        if (item && row == uil_cursor()) {
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

/* The five buttons along the bottom (the CD player's, without their pictures) are the list's tools: X moves the cursor
 * there. The leftmost is the recently played list; the others are still free. */
static const char* const button_names[BLIST_BUTTONS] = {"Recent", "", "", "", ""};

static void
draw_button_text(void) {
    for (int i = 0; i < BLIST_BUTTONS; i++) {
        if (button_names[i][0]) {
            float x, y;
            blist_button_center_px(i, &x, &y);
            gfx_text(button_names[i], x - (float)gfx_text_width(button_names[i]) / 2.0f, y - (float)GFX_LINE_H / 2.0f, 0.5f,
                     0xFFFFFFFFu, 1);
        }
    }
}

/* What we know of a game for the info panel: product code and region, then players, VMU blocks and discs. */
static int
game_info_lines(const gd_item* cur, char out[2][24]) {
    out[0][0] = out[1][0] = '\0';
    if (!cur || uil_is_folder(cur)) {
        return 0;
    }
    snprintf(out[0], sizeof(out[0]), "%.10s %.4s", cur->product, cur->region);
    int discs = cur->product[0] ? gd_item_disc_total(cur->disc) : 1;
    int players = 0, blocks = 0;
    struct db_item* meta = NULL;
    if (have_meta && !db_get_meta(cur->product, &meta) && meta) {
        players = meta->num_players;
        blocks = meta->vmu_blocks;
    }
    char p[8] = "", v[8] = "";
    if (players) snprintf(p, sizeof(p), "%dP ", players);
    if (blocks) snprintf(v, sizeof(v), "%dB ", blocks);
    snprintf(out[1], sizeof(out[1]), "%s%s%dD", p, v, discs > 0 ? discs : 1);
    return 2;
}

/* The PVR sorts the translucent polygons by depth (autosort): our own panels lie at a fixed depth, and a model meant to
 * be seen in front of one goes through this sink, which brings it nearer. Scaling 1/w keeps the order of the model's
 * own polygons. */
#define PANE_Z 0.0028f
#define NEAR_SCALE 4.0f

typedef struct {
    const bscene_sink* out;
} near_wrap;

static void
near_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    const near_wrap* w = (const near_wrap*)user;
    bscene_vtx n[3] = {v[0], v[1], v[2]};
    for (int i = 0; i < 3; i++) {
        n[i].invw *= NEAR_SCALE;
    }
    w->out->triangle(w->out->user, n, tex);
}

static void
near_text(void* user, const bvm_obj* o, float x, float y, float invw) {
    const near_wrap* w = (const near_wrap*)user;
    w->out->text(w->out->user, o, x, y, invw * NEAR_SCALE);
}

static const bscene_sink*
near_sink(const bscene_sink* out) {
    static near_wrap w;
    static bscene_sink s = {&w, near_triangle, near_text, NULL};
    w.out = out;
    return &s;
}

static void draw_viewer(void);
static int viewer_open;

static void
draw_games(void) {
    char lines[3][20];
    if (viewer_open) {
        draw_viewer();
        return;
    }
    blist_draw(&glist, gfx_sink());
    if (uil_count() <= 0) {
        gfx_text("No games found.", 64.0f, 200.0f, 0.5f, 0xFFFFFFFFu, 1);
        return;
    }
    const gd_item* cur = uil_item(uil_cursor());
    draw_button_text();
    float panel_top, panel_bottom;
    blist_rows_extent_px(&glist, &panel_top, &panel_bottom);
    const float art_y = panel_top + 8.0f;
    /* the colour, alpha (BSCENE_PANE_ARGB) and corner radius of the row bars; just in front of the clouds (~0.0026), behind the case */
    gfx_pane(INFO_X - 6.0f, panel_top, INFO_W + 12.0f, panel_bottom - panel_top, PANE_Z, BSCENE_PANE_ARGB);
    bcase_draw(&gcase, &menu.scene, INFO_X + INFO_W / 2.0f, art_y + ART_SIZE / 2.0f, ART_SIZE + 10.0f, case_bind, NULL,
               near_sink(gfx_sink()));
    if (cur && !uil_is_folder(cur)) {
        float y = art_y + ART_SIZE + 6.0f;
        int n = wrap_text(cur->name, 15, lines, 3);
        for (int i = 0; i < n; i++) {
            gfx_text(lines[i], INFO_X, y, 0.5f, 0xFFFFFFFFu, 1);
            y += GFX_LINE_H - 4.0f;
        }
        char info[2][24];
        int k = game_info_lines(cur, info);
        y = panel_bottom - 8.0f - (float)k * (GFX_LINE_H - 4.0f);
        for (int i = 0; i < k; i++) {
            gfx_text(info[i], INFO_X, y, 0.5f, 0xFFC0C0C0u, 1);
            y += GFX_LINE_H - 4.0f;
        }
    } else if (cur) {
        gfx_text("Folder", INFO_X, art_y, 0.5f, 0xFFFFFFFFu, 1);
    }
}

/* ---- Case viewer (Y, or a click on the case): the case large on the right two thirds, turned with the stick, the
 * d-pad or by dragging with the mouse, zoomed with the triggers; a panel where the rows start, for the game's
 * information; the BACK marker selected (A, B, Y or a click on it leave). Turning and zooming ease towards where they
 * are heading as the cases of the list do. */
#define VIEW_PANE_X 40.0f /* where the rows start */
#define VIEW_PANE_W 190.0f
#define VIEW_CX 440.0f
#define VIEW_SIZE 300.0f
#define VIEW_ZOOM_MIN 0.5f
#define VIEW_ZOOM_MAX 2.2f
#define VIEW_EASE 0.26f /* bios_case EASE: the share of the remaining way covered per frame */
#define VIEW_DPAD_STEP 45.0f

static float viewer_rot[2], viewer_target[2]; /* degrees about x (up/down) and y (left/right) */
static float viewer_zoom = 1.0f, viewer_zoom_target = 1.0f;
static int drag_x, drag_y, dragging;

static void
viewer_reset(void) {
    viewer_rot[0] = viewer_rot[1] = viewer_target[0] = viewer_target[1] = 0.0f;
    viewer_zoom = viewer_zoom_target = 1.0f;
    dragging = 0;
}

/* once per logic frame */
static void
viewer_step(void) {
    int jx, jy, lt, rt;
    input_analog(&jx, &jy, &lt, &rt);
    viewer_target[1] += (float)jx * 0.03f; /* full deflection: about 230 degrees a second */
    viewer_target[0] += (float)jy * 0.03f;
    viewer_zoom_target *= 1.0f + (float)(rt - lt) * (0.02f / 255.0f);
    viewer_zoom_target = viewer_zoom_target < VIEW_ZOOM_MIN ? VIEW_ZOOM_MIN
                                                            : (viewer_zoom_target > VIEW_ZOOM_MAX ? VIEW_ZOOM_MAX : viewer_zoom_target);
    for (int k = 0; k < 2; k++) {
        viewer_rot[k] += (viewer_target[k] - viewer_rot[k]) * VIEW_EASE;
    }
    viewer_zoom += (viewer_zoom_target - viewer_zoom) * VIEW_EASE;
}

/* once per frame: dragging with the left button held turns the case with the pointer */
static void
viewer_drag(const mouse_t* p) {
    if (!p->left) {
        dragging = 0;
        return;
    }
    if (dragging) {
        viewer_target[1] += (float)(p->x - drag_x) * 0.6f;
        viewer_target[0] += (float)(p->y - drag_y) * 0.6f;
    }
    dragging = 1;
    drag_x = p->x;
    drag_y = p->y;
}

static void
draw_viewer(void) {
    char lines[3][20];
    float top, bottom;
    blist_rows_extent_px(&glist, &top, &bottom);
    gfx_pane(VIEW_PANE_X, top, VIEW_PANE_W, bottom - top, PANE_Z, BSCENE_PANE_ARGB);
    const gd_item* cur = uil_item(uil_cursor());
    if (cur) {
        float y = top + 10.0f;
        int n = wrap_text(cur->name, 15, lines, 3);
        for (int i = 0; i < n; i++) {
            gfx_text(lines[i], VIEW_PANE_X + 10.0f, y, 0.5f, 0xFFFFFFFFu, 1);
            y += GFX_LINE_H - 4.0f;
        }
        char info[2][24];
        int k = game_info_lines(cur, info);
        y += 8.0f;
        for (int i = 0; i < k; i++) {
            gfx_text(info[i], VIEW_PANE_X + 10.0f, y, 0.5f, 0xFFC0C0C0u, 1);
            y += GFX_LINE_H - 4.0f;
        }
    }
    float rot[3] = {viewer_rot[0], viewer_rot[1], 0.0f};
    bcase_draw_posed(&gcase, &menu.scene, VIEW_CX, (top + bottom) / 2.0f, VIEW_SIZE * viewer_zoom, rot, case_bind, NULL,
                     near_sink(gfx_sink()));
    /* the BACK marker of the list, selected: lit, its frame blinking */
    const bvm_obj* back = bvm_find(&menu.vm, 0x1110);
    if (back) {
        bmenu_back_style(&menu, 1, glist.anim, glist.pal_console);
        menu.scene.parts = BSCENE_PART_ALL;
        bscene_draw_object(&menu.scene, back, gfx_sink());
        menu.scene.ovr_n = 0;
    }
}

static void
close_viewer(void) {
    viewer_open = 0;
    dragging = 0;
    sound_sfx(BAUDIO_SFX_CANCEL);
}

static void
handle_viewer(button_t b) {
    switch (b) {
        case BTN_Y:
        case BTN_B: close_viewer(); break;
        case BTN_A:
        case BTN_START:
            /* BACK is the selected item; a click only leaves when it is on BACK (elsewhere it starts a drag) */
            if (!pointer.clicked || blist_back_at_px(pointer_screen_x(), (float)pointer.y)) {
                close_viewer();
            }
            break;
        case BTN_LEFT: viewer_target[1] -= VIEW_DPAD_STEP; break; /* eased there, as a held stick would */
        case BTN_RIGHT: viewer_target[1] += VIEW_DPAD_STEP; break;
        case BTN_UP: viewer_target[0] -= VIEW_DPAD_STEP; break;
        case BTN_DOWN: viewer_target[0] += VIEW_DPAD_STEP; break;
        case BTN_X: viewer_reset(); break; /* back to the front */
        default: break;
    }
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
        case SCREEN_MUSIC:
        case SCREEN_INTERNET: return BMENU_ACCENT_MUSIC;
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
    snprintf(line, sizeof(line), "%.36s\n%.36s", uis_group(page.cursor), uis_help(page.cursor)); /* 36 fit the help box */
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
    /* every number centred over and under its arrow, the separators halfway between the numbers */
    for (int pos = 0; pos < 5; pos++) {
        int f = bdt_field_at(&dtedit, pos);
        snprintf(text, sizeof(text), f == BDT_YEAR ? "%04d" : "%02d", bdt_field_value(&dtedit, f));
        float cx = bdt_field_center_px(&dtedit, f);
        gfx_text(text, cx - (float)gfx_text_width(text) / 2.0f, BDT_TEXT_Y, 0.5f, 0xFFFFFFFFu, 1);
        if (pos < 4 && pos != 2) {
            const char* sep = pos == 3 ? ":" : "/";
            float nx = bdt_field_center_px(&dtedit, bdt_field_at(&dtedit, pos + 1));
            gfx_text(sep, (cx + nx) / 2.0f - (float)gfx_text_width(sep) / 2.0f, BDT_TEXT_Y, 0.5f, 0xFFFFFFFFu, 1);
        }
    }
    /* the names are written over the right half of the green ovals, a little lower (as in the BIOS) */
    gfx_text("Select", BDT_BUTTON_X + 9.0f, BDT_SELECT_Y - 14.0f, 0.5f, 0xFFD0D0D0u, 0);
    gfx_text("Cancel", BDT_BUTTON_X + 9.0f, BDT_CANCEL_Y - 14.0f, 0.5f, 0xFFD0D0D0u, 0);
}

/* The mouse pointer: the BIOS' green arrow (model 35, the scroll arrows of its lists) with its tip to the top left,
 * on the pointer position, nearer than everything else.
 *
 * The model is drawn where it always is -- the middle of the screen, unturned, so it is lit exactly as the BIOS
 * lights its down arrow, and the scene's output cache sends the same 7 triangles again every frame -- and this sink
 * turns those by 225 degrees in the picture and moves them to the pointer. */
#define POINTER_NEAR 400.0f /* 1/300 -> 1.33 */

typedef struct {
    const bscene_sink* out;
    float c, s, dx, dy;
} pointer_xform;

static void
pointer_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    const pointer_xform* p = (const pointer_xform*)user;
    bscene_vtx w[3];
    for (int i = 0; i < 3; i++) {
        float x = v[i].x - 320.0f, y = v[i].y - 240.0f;
        w[i] = v[i];
        w[i].x = 320.0f + p->dx + x * p->c + y * p->s;
        w[i].y = 240.0f + p->dy - x * p->s + y * p->c;
        /* nearer than anything else on the screen (the PVR sorts by depth): models brought forward by near_sink are
         * at ~0.01, text and panels at most ~0.7; scaling keeps the order of the pointer's own polygons */
        w[i].invw = v[i].invw * POINTER_NEAR;
    }
    p->out->triangle(p->out->user, w, tex);
}

static void
draw_pointer(void) {
    if (!pointer.visible) {
        return;
    }
    float ux = (float)pointer.x;
    if (sf_aspect[0] == ASPECT_WIDE) {
        ux = 320.0f + (ux - 320.0f) / 0.75f; /* the picture is squeezed: place it where the screen shows it */
    }
    float tip = 13.6f * 0.7071f; /* the tip is 13.6 px from the model's origin, up and to the left once turned */
    static pointer_xform px;
    px.out = gfx_sink();
    px.c = -0.70710678f; /* 225 degrees, as a turn of the model about the view axis */
    px.s = -0.70710678f;
    px.dx = ux + tip - 320.0f;
    px.dy = (float)pointer.y + tip - 240.0f;
    bscene_sink sink = {&px, pointer_triangle, NULL, NULL};
    bvm_obj o;
    memset(&o, 0, sizeof(o));
    o.active = 1;
    o.flags = BVM_F_MODEL;
    o.model = 35;
    o.texlist = 35;
    o.pos[2] = -300.0f; /* 4000 / 300 pixels per unit */
    o.scale_tw[0].cur = o.scale_tw[1].cur = o.scale_tw[2].cur = 1.1f;
    menu.scene.parts = BSCENE_PART_MODEL;
    bscene_draw_object(&menu.scene, &o, &sink);
    menu.scene.parts = BSCENE_PART_ALL;
}

/* Debug: Y on the main screen (overlay on) isolates what costs time, one mode per press, the name is in the overlay. */
static int debug_iso;
static const char* const iso_names[] = {"normal", "no clouds", "no icons", "nothing", "CPU sorted", "built, not sent", "no cache"};
#define ISO_MODES 7

static void
null_tri(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user; (void)v; (void)tex;
}

static void
null_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user; (void)obj; (void)x; (void)y; (void)invw;
}

static const bscene_sink null_sink = {NULL, null_tri, null_text, NULL};

static const char*
log_label(void) {
    static char label[20];
    switch (screen) {
        case SCREEN_MAIN: snprintf(label, sizeof(label), "main/%s", iso_names[debug_iso]); return label;
        case SCREEN_GAMES: return viewer_open ? "games/viewer" : (recent_open ? "games/recent" : "games");
        case SCREEN_SETTINGS: return "settings";
        case SCREEN_DATETIME: return "date/time";
        case SCREEN_FILES: return "files";
        case SCREEN_MUSIC: return "cd player";
        case SCREEN_INTERNET: return "internet";
    }
    return "?";
}

/* The debug log page: B opens it on the main screen while the overlay is on. */
static void
draw_report(void) {
    const float Z = 0.9f, ZT = 0.95f;
    gfx_rect(0.0f, 0.0f, 640.0f, 480.0f, Z, 0xFF101828u);
    if (report_page == 1) {
        char l[48], r[48];
        gfx_text_scaled("Debug log   fps/min  tris   ms: sync build wait gpu ta", 16.0f, 6.0f, ZT, 0xFFF0E060u, 0.62f);
        gfx_text_scaled("v = vertices  h = headers  a/p = PVR/CPU sorted", 16.0f, 22.0f, ZT, 0xFFA0A0A0u, 0.62f);
        int n = dbglog_count();
        for (int i = 0; i < n && i < 16; i++) {
            dbglog_row(i, l, r, sizeof(l));
            float y = 44.0f + (float)i * 21.0f;
            gfx_text_scaled(l, 16.0f, y, ZT, 0xFFFFFFFFu, 0.8f);
            gfx_text_scaled(r, 262.0f, y, ZT, 0xFFD0E0FFu, 0.8f);
        }
        if (n > 16) {
            snprintf(l, sizeof(l), "+%d more (in the QR code)", n - 16);
            gfx_text_scaled(l, 16.0f, 44.0f + 16.0f * 21.0f, ZT, 0xFFA0A0A0u, 0.8f);
        }
        dbglog_boot_lines(l, r, sizeof(l));
        gfx_text_scaled(l, 16.0f, 408.0f, ZT, 0xFFD0E0FFu, 0.62f);
        gfx_text_scaled(r, 16.0f, 424.0f, ZT, 0xFFD0E0FFu, 0.62f);
        int fp = serial_log_flash_progress();
        if (fp >= 0 && fp <= 100) {
            snprintf(l, sizeof(l), "Sending the flash ROM to serial: %d%%", fp);
        } else {
            snprintf(l, sizeof(l), "B: QR code  A: clear  X: flash to serial%s", fp > 100 ? " (sent)" : "");
        }
        gfx_text_scaled(l, 16.0f, 452.0f, ZT, 0xFFA0A0A0u, 0.8f);
    } else {
        static uint8_t modules[QR_MAX_SIZE * QR_MAX_SIZE];
        static char text[2800];
        static int size;
        if (!report_built) {
            char info[40];
            snprintf(info, sizeof(info), "%s %s", console_is_pal() ? "PAL" : "NTSC", sf_aspect[0] == ASPECT_WIDE ? "16:9" : "4:3");
            int len = dbglog_text(text, sizeof(text), info);
            size = qr_encode((const uint8_t*)text, len, modules);
            report_built = 1;
        }
        if (size) {
            int px = 440 / (size + 8);
            if (px < 2) px = 2;
            float side = (float)((size + 8) * px);
            float x0 = (640.0f - side) / 2.0f, y0 = (480.0f - side) / 2.0f;
            gfx_rect(x0, y0, side, side, ZT, 0xFFFFFFFFu);
            for (int y = 0; y < size; y++) {
                int x = 0;
                while (x < size) { /* a run of dark modules is one rectangle */
                    if (!modules[y * size + x]) {
                        x++;
                        continue;
                    }
                    int x1 = x;
                    while (x1 < size && modules[y * size + x1]) x1++;
                    gfx_rect(x0 + (float)((x + 4) * px), y0 + (float)((y + 4) * px), (float)((x1 - x) * px), (float)px, 0.97f,
                             0xFF000000u);
                    x = x1;
                }
            }
        } else {
            gfx_text_scaled("Too much to show as a QR code", 16.0f, 200.0f, ZT, 0xFFFFFFFFu, 0.8f);
        }
        gfx_text_scaled("B: close   (scan it, paste the text)", 16.0f, 456.0f, ZT, 0xFFA0A0A0u, 0.8f);
    }
}

static void
draw_frame(void) {
    uint32_t top, bottom;
    uint64_t t0 = timer_us_gettime64();
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
    /* The BIOS draws its menus with the PVR sorting the translucent polygons per pixel: where an icon's parts and
     * its two copies overlap, the farther layer is always blended first. The CD player needs it too: the pictures on
     * its buttons are separate polygons in front of the buttons, and sent first in presort mode their transparent
     * parts would hide the buttons. Every screen's BACK marker needs it as well: its blinking frame sits at the
     * button's depth, and in presort mode the PVR's depth test drops it behind the button. Measured on hardware the
     * PVR takes the same ~9 ms a frame either way. */
    {
        int hw = gfx_set_autosort(screen == SCREEN_MAIN ? (debug_iso == 4 ? 0 : GFX_MAIN_AUTOSORT) : 1);
        menu.hw_autosort = hw; /* not sorted by the PVR: the CPU sorter takes over */
    }
    gfx_begin_frame(top, bottom);
    uint64_t t1 = timer_us_gettime64();
    phase_us[0] += t1 - t0;

    if (screen == SCREEN_MAIN) {
        const bscene_sink* ms = debug_iso == 5 ? &null_sink : gfx_sink();
        if (debug_iso != 1 && debug_iso != 3) {
            bscene_draw_background(&menu.bg, ms);
        }
        phase_us[1] += timer_us_gettime64() - t1;
        if (debug_iso != 2 && debug_iso != 3) {
            bmenu_draw_objects(&menu, ms);
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
    } else if (screen == SCREEN_MUSIC) {
        bscene_draw_background(&menu.bg, gfx_sink());
        bcd_draw(&cdp, gfx_sink());
    } else if (screen == SCREEN_INTERNET) {
        bscene_draw_background(&menu.bg, gfx_sink());
        uin_draw();
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
            if (debug_iso) {
                char iso[32];
                snprintf(iso, sizeof(iso), "Y: %s", iso_names[debug_iso]);
                gfx_text(iso, TEXT_X, FPS_Y - 84.0f, 0.5f, 0xFFF0E060u, 0);
            }
        }
        gfx_text(fps_text, TEXT_X, FPS_Y - 56.0f, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[0], TEXT_X, FPS_Y - 28.0f, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[1], TEXT_X, FPS_Y, 0.5f, 0xFFFFFFFFu, 0);
        gfx_text(perf_text[2], TEXT_X, FPS_Y + 28.0f, 0.5f, 0xFFFFFFFFu, 0);
    }
    if (notice_frames > 0 && notice_text) {
        gfx_text(notice_text, TEXT_X, screen == SCREEN_MAIN ? NOTICE_Y_MAIN : NOTICE_Y_PANEL, 0.5f, 0xFFFFFFFFu, 1);
    }
    if (report_page) {
        draw_report();
    }
    draw_pointer();
    phase_us[2] += timer_us_gettime64() - t0;
    build_us_sum += timer_us_gettime64() - t0;
    gfx_end_frame();
}

static void
handle_report(button_t b) {
    if (b == BTN_B || b == BTN_START) {
        report_page = report_page == 1 ? 2 : 0;
        sound_sfx(BAUDIO_SFX_CANCEL);
    } else if (b == BTN_A && report_page == 1) {
        dbglog_clear();
        sound_sfx(BAUDIO_SFX_ENTER);
    } else if (b == BTN_X && report_page == 1) {
        /* the whole flash ROM as hex over the coder's cable, for decoding what the browsers save */
        sound_sfx(serial_log_flash_start() == 0 ? BAUDIO_SFX_ENTER : BAUDIO_SFX_ERROR);
    }
}

static void
handle_main(button_t b) {
    switch (b) {
        case BTN_X:
            show_debug = !show_debug;
            if (!show_debug) {
                debug_iso = 0;
                bscene_cache_enable(1);
            }
            break;
        case BTN_B:
            if (show_debug) {
                report_page = 1;
                report_built = 0;
                {
                    /* the page's numbers as text on the serial port, so they need no photo */
                    static char text[2800];
                    char info[40];
                    snprintf(info, sizeof(info), "%s %s", console_is_pal() ? "PAL" : "NTSC",
                             sf_aspect[0] == ASPECT_WIDE ? "16:9" : "4:3");
                    dbglog_text(text, sizeof(text), info);
                    serial_log_block("log", text);
                }
            }
            break;
        case BTN_Y:
            if (show_debug) {
                debug_iso = (debug_iso + 1) % ISO_MODES;
                bscene_cache_enable(debug_iso != 6); /* 6: every object is transformed, lit and projected every frame */
            }
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
                viewer_open = 0;
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
            } else if (menu.selected == ICON_MUSIC && sf_dcnow[0] != DCNOW_OFF) {
                sound_sfx(BAUDIO_SFX_ENTER); /* online features on: the icon is Internet */
                uin_open(&menu, console_is_pal());
                screen = SCREEN_INTERNET;
            } else if (menu.selected == ICON_MUSIC) {
                sound_sfx(BAUDIO_SFX_ENTER);
                bcd_open(&cdp, &menu, 1, 0, 0); /* the disc of the drive is shown; nothing to play */
                cdp.pal = console_is_pal();
                screen = SCREEN_MUSIC;
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

/* The recently played list (a popup over the game list). */
static void
open_recent(void) {
    if (history_recent_count() > 0) {
        recent_open = 1;
        recent_row = 0;
        sound_sfx(BAUDIO_SFX_ENTER);
    } else {
        sound_sfx(BAUDIO_SFX_ERROR);
        show_notice(sf_recently_played[0] == RECENTLY_PLAYED_OFF ? "Recently played is off (Settings)" : "No games played yet");
    }
}

/* The case of the right panel is under a point (pixels). */
static int
case_at_px(float x, float y) {
    float top, bottom;
    blist_rows_extent_px(&glist, &top, &bottom);
    float art_y = top + 8.0f;
    return x >= INFO_X - 6.0f && x <= INFO_X + INFO_W + 6.0f && y >= art_y - 10.0f && y <= art_y + ART_SIZE + 10.0f;
}

static void
handle_games(button_t b) {
    const gd_item* game = NULL;
    if (recent_open) {
        handle_recent(b);
        return;
    }
    if (viewer_open) {
        handle_viewer(b);
        return;
    }
    if (glist.button >= 0) {
        /* the cursor is on the buttons along the bottom: left of the first is BACK, as in the CD player */
        switch (b) {
            case BTN_LEFT:
                if (glist.button > 0) {
                    glist.button--;
                } else {
                    glist.button = -1;
                    glist.back_selected = 1;
                }
                sound_sfx(BAUDIO_SFX_CURSOR);
                break;
            case BTN_RIGHT:
                if (glist.button < BLIST_BUTTONS - 1) {
                    glist.button++;
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
                break;
            case BTN_UP:
            case BTN_X:
            case BTN_B:
                glist.button = -1;
                sound_sfx(BAUDIO_SFX_CURSOR);
                break;
            case BTN_A:
            case BTN_START:
                blist_press_button(&glist, glist.button);
                if (glist.button == 0) {
                    open_recent();
                } else {
                    sound_sfx(BAUDIO_SFX_ERROR);
                }
                break;
            default: break;
        }
        return;
    }
    if (b == BTN_X) { /* to the buttons */
        glist.back_selected = 0;
        glist.button = 0;
        sound_sfx(BAUDIO_SFX_CURSOR);
        return;
    }
    int on_case = b == BTN_A && pointer.clicked && case_at_px(pointer_screen_x(), (float)pointer.y);
    if (b == BTN_Y || on_case) { /* Y, or a click on the case: the case viewer */
        const gd_item* cur = uil_item(uil_cursor());
        if (cur && !uil_is_folder(cur) && (on_case || !glist.back_selected)) {
            glist.back_selected = 0;
            viewer_open = 1;
            viewer_reset();
            sound_sfx(BAUDIO_SFX_ENTER);
        } else {
            sound_sfx(BAUDIO_SFX_ERROR);
        }
        return;
    }
    if (glist.back_selected) {
        if (b == BTN_RIGHT) { /* along the bottom row to the buttons */
            glist.back_selected = 0;
            glist.button = 0;
            sound_sfx(BAUDIO_SFX_CURSOR);
            return;
        }
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
        launch_disc(game); /* straight away; only returns if the launch failed */
        sound_sfx(BAUDIO_SFX_ERROR);
        show_notice("Could not start the game");
    } else if (r == UIL_EXIT) {
        sound_sfx(BAUDIO_SFX_CANCEL);
        bmenu_show_main(&menu, ICON_GAME);
        screen = SCREEN_MAIN;
    } else if (r == UIL_RECENT) {
        open_recent();
    } else if (r == UIL_REDRAW) {
        sound_sfx(b == BTN_A || b == BTN_START ? BAUDIO_SFX_CONFIRM : (b == BTN_B ? BAUDIO_SFX_CANCEL : BAUDIO_SFX_CURSOR));
    }
}

/* The Music screen: the BIOS CD player (cdplayer_cursor_cb). Left / right move, A on a button makes it jump with the
 * confirm sound, A on BACK or B leaves. */
static void
handle_music(button_t b) {
    switch (b) {
        case BTN_LEFT:
            if (bcd_nav(&cdp, BCD_LEFT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_RIGHT:
            if (bcd_nav(&cdp, BCD_RIGHT)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_A:
        case BTN_START:
            if (cdp.cursor != BCD_BACK) {
                if (bcd_press(&cdp, cdp.cursor)) sound_sfx(BAUDIO_SFX_CONFIRM);
                break;
            }
            /* fall through: A on BACK */
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            bmenu_show_main(&menu, ICON_MUSIC);
            screen = SCREEN_MAIN;
            break;
        default: break;
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
    set_icon_labels();
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
            } else if (uis_action(row) == UIS_ACTION_VMUCLOCK) {
                sound_sfx(BAUDIO_SFX_ENTER);
                uis_run(row);
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
            if (viewer_open) {
                break;
            }
            if (blist_back_at_px(ux, uy)) {
                if (!glist.back_selected) sound_sfx(BAUDIO_SFX_CURSOR);
                glist.back_selected = 1;
                glist.button = -1;
                break;
            }
            {
                int bt = blist_button_at_px(ux, uy);
                if (bt >= 0) {
                    if (bt != glist.button) sound_sfx(BAUDIO_SFX_CURSOR);
                    glist.button = bt;
                    glist.back_selected = 0;
                    break;
                }
            }
            int row = blist_row_in_slot(&glist, blist_slot_at_px(&glist, ux, uy));
            if (row >= 0) {
                glist.back_selected = 0;
                glist.button = -1;
                if (row != uil_cursor()) {
                    uil_set_cursor(row);
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
            }
            break;
        }
        case SCREEN_FILES: uif_hover(ux, uy); break;
        case SCREEN_INTERNET: uin_hover(ux, uy); break;
        case SCREEN_MUSIC: {
            int i = bcd_item_at_px(ux, uy);
            if (i >= 0 && bcd_set_cursor(&cdp, i)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        }
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

/* A modem connection is torn down in a way that blocks (before a launch): say so on two frames first. */
static void
show_hangup(void) {
    show_notice("Hanging up...");
    for (int i = 0; i < 2; i++) {
        draw_frame();
    }
}

int
ui_bios_run(const bios_rom* rom) {
    g_rom = rom;
    if (gfx_init(rom) != 0) {
        return -1;
    }
    dbglog_boot("gfx", (unsigned)timer_ms_gettime64());

    gfx_load_logo("/cd/LOGO.PVR"); /* optional replacement of the Dreamcast logo */
    dbglog_boot("logo", (unsigned)timer_ms_gettime64());
    /* game info (players, VMU blocks) comes from META.DAT on the menu disc; db_load_DAT needs it */
    file_t meta_fd = fs_open("/cd/META.DAT", O_RDONLY);
    if (meta_fd != FILEHND_INVALID) {
        fs_close(meta_fd);
        db_load_DAT();
        have_meta = 1;
    }
    dbglog_boot("meta", (unsigned)timer_ms_gettime64());

    sound_init(rom);
    dbglog_boot("sound", (unsigned)timer_ms_gettime64());
    snprintf(status_line, sizeof(status_line), "BIOS %s  %s  boot %08X", rom->revision, sound_status(),
             (unsigned)*(volatile uint32_t*)BOOT_COLOR_ADDR);

    start_color = read_boot_color();
    bmenu_init(&menu, rom, NULL);
    bmenu_show_main(&menu, ICON_GAME);
    set_icon_labels();
    update_clock();
    online_set_hangup_hook(show_hangup);
    dbglog_boot("menu", (unsigned)timer_ms_gettime64());

    /* Cover the first frames (texture uploads) with the start colour so nothing pops in. */
    for (int i = 0; i < 3; i++) {
        gfx_begin_frame(START_COLOR, START_COLOR);
        gfx_end_frame();
    }
    dbglog_boot("frames", (unsigned)timer_ms_gettime64());
    {
        char a[64], b2[64];
        dbglog_boot_lines(a, b2, sizeof(a));
        serial_log_block("boot", a);
        serial_log_block("boot", b2);
    }

    uint64_t last_ms = timer_ms_gettime64();
    uint64_t fps_since = last_ms;
    uint32_t step_credit = 0;
    int frames_this_second = 0;

    for (;;) {
        uint64_t logic_t0 = timer_us_gettime64();
        button_t b = input_poll();
        input_mouse(&pointer);
        if (screen == SCREEN_GAMES && viewer_open) {
            viewer_drag(&pointer);
        }
        if (pointer.moved || pointer.clicked) {
            apply_hover(); /* a click acts on what is under the pointer, even if it has not moved */
        }
        int typed = input_typed_char();
        if (typed && screen == SCREEN_GAMES && !recent_open && uil_jump_to_letter(typed)) {
            sound_sfx(BAUDIO_SFX_CURSOR);
        }
        if (report_page) {
            handle_report(b);
            b = BTN_NONE;
        }
        if (screen == SCREEN_MAIN) {
            handle_main(b);
        } else if (screen == SCREEN_GAMES) {
            handle_games(b);
        } else if (screen == SCREEN_DATETIME) {
            handle_datetime(b);
        } else if (screen == SCREEN_FILES) {
            uif_handle(b);
        } else if (screen == SCREEN_MUSIC) {
            handle_music(b);
        } else if (screen == SCREEN_INTERNET) {
            if (uin_handle(b)) {
                bmenu_show_main(&menu, ICON_MUSIC);
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
            if (screen == SCREEN_GAMES && viewer_open) {
                viewer_step();
            }
            if (screen == SCREEN_FILES && uif_step()) {
                bmenu_show_main(&menu, ICON_FILES);
                screen = SCREEN_MAIN;
            }
        }

        if (screen == SCREEN_SETTINGS) {
            settings_sync();
        } else if (screen == SCREEN_DATETIME) {
            bdt_sync(&dtedit);
        } else if (screen == SCREEN_FILES) {
            uif_sync();
        } else if (screen == SCREEN_MUSIC) {
            bcd_sync(&cdp);
        } else if (screen == SCREEN_INTERNET) {
            uin_sync();
        } else if (screen == SCREEN_GAMES) {
            games_sync();
        }

        /* the online engine: a game the DreamPi's phone page asked for starts only on the plain main menu or game list */
        online_tick((screen == SCREEN_MAIN || screen == SCREEN_GAMES) && !recent_open && !settings_popup && !about_open &&
                    !viewer_open);

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
            snprintf(perf_text[1], sizeof(perf_text[1]), "gpu %u.%u ta %u.%u ms  %u vtx  hdr %u%s", gs.render_us / n / 1000u,
                     gs.render_us / n / 100u % 10u, gs.ta_us / n / 1000u, gs.ta_us / n / 100u % 10u, gs.vertices / n, gs.headers / n,
                     gfx_autosort() ? " auto" : " pre");
            {
                unsigned beg = (unsigned)(phase_us[0] / n / 100u), bg = (unsigned)(phase_us[1] / n / 100u),
                         all = (unsigned)(phase_us[2] / n / 100u);
                snprintf(perf_text[2], sizeof(perf_text[2]), "begin %u.%u bg %u.%u drawn %u.%u ms", beg / 10, beg % 10, bg / 10,
                         bg % 10, all / 10, all % 10);
                last_sample.beg = beg;
                last_sample.bg = bg;
                last_sample.drawn = all;
                phase_us[0] = phase_us[1] = phase_us[2] = 0;
            }
            if (show_debug && !report_page) {
                dbglog_sample ds = {(unsigned)frames_this_second, gfx_triangles(), sync_t, build_t, wait_t,
                                    gs.render_us / n / 100u, gs.ta_us / n / 100u, gs.vertices / n, gs.headers / n,
                                    (unsigned)gfx_autosort(), last_sample.beg, last_sample.bg, last_sample.drawn};
                dbglog_add(log_label(), &ds);
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
