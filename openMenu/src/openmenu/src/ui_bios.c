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
#include <bios_list.h>
#include <bios_page.h>
#include <backend/db_list.h>
#include <kos/fs.h>

#include "gfx.h"
#include "history.h"
#include "input.h"
#include "launch.h"
#include "sound.h"
#include <openmenu_settings.h>

#include "ui_bios.h"
#include <backend/gd_list.h>

#include "ui_list.h"
#include "ui_settings.h"
#include "video.h"

typedef enum { SCREEN_MAIN, SCREEN_GAMES, SCREEN_SETTINGS } screen_t;

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
static bpage page;
static blist glist;                 /* the game list screen */
static const gd_item* launch_pending; /* a game waiting for the launch animation to end */
static int have_meta;               /* META.DAT loaded: players, VMU blocks */
static int settings_popup;     /* a settings row popup is open */
static int settings_popup_sel;
static int games_opened;  /* the game browser has been opened once (last game restored) */
static int recent_open;   /* recently played popup */
static int recent_row;
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

/* ---- Game list -------------------------------------------------------------------------- */

#define INFO_X 438.0f
#define INFO_W 182.0f
#define ART_SIZE 140.0f
#define ART_Y 66.0f

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

/* Row texts, disc pictures and the right panel's data for the frame about to be drawn. */
static void
games_sync(void) {
    char line[BPAGE_LABEL_MAX];
    blist_set_count(&glist, uil_count());
    blist_set_cursor(&glist, uil_cursor());
    for (int s = 0; s < glist.slots; s++) {
        int row = blist_row_in_slot(&glist, s);
        const gd_item* item = row >= 0 ? uil_item(row) : NULL;
        line[0] = '\0';
        int multi = 0;
        if (item) {
            multi = !uil_is_folder(item) && uil_disc_total(item) > 1;
            size_t keep = multi ? 19 : 24; /* a multi-disc row has the disc pill at its right end */
            if (strlen(item->name) > keep) {
                snprintf(line, sizeof(line), "%.*s..", (int)keep - 2, item->name);
            } else {
                snprintf(line, sizeof(line), "%s", item->name);
            }
        }
        glist.multi[s] = multi;
        if (multi) {
            int picked = row == uil_cursor() ? uil_disc_index() : 0;
            char num[12];
            snprintf(num, sizeof(num), "%d:%d", picked + 1, uil_disc_total(item));
            gfx_set_label((uint16_t)BLIST_NUM_ID(s), num);
        }
        gfx_set_label((uint16_t)BLIST_TEXT_ID(s), line);
        gfx_art_bind_row(s, item && !uil_is_folder(item) ? item->product : "");
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
            gfx_text(text[i], x - (float)strlen(text[i]) * GFX_CHAR_W / 2.0f, y - (float)GFX_LINE_H / 2.0f, 0.5f, 0xFFFFFFFFu, 1);
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
    gfx_rrect(INFO_X - 6.0f, ART_Y - 8.0f, INFO_W + 12.0f, 326.0f, 6.0f, 0.4f, 0x58000000u);
    if (cur && !uil_is_folder(cur)) {
        gfx_art_box(cur->product, INFO_X + (INFO_W - ART_SIZE) / 2.0f, ART_Y, ART_SIZE, ART_SIZE, 0.45f);
        float y = ART_Y + ART_SIZE + 6.0f;
        int n = wrap_text(cur->name, 15, lines, 3);
        for (int i = 0; i < n; i++) {
            gfx_text(lines[i], INFO_X, y, 0.5f, 0xFFFFFFFFu, 1);
            y += GFX_LINE_H - 4.0f;
        }
    } else if (cur) {
        gfx_text("Folder", INFO_X, ART_Y, 0.5f, 0xFFFFFFFFu, 1);
    }
    gfx_text("A:go X:recent", INFO_X, 350.0f, 0.5f, 0xFFA0A0A0u, 0); /* B is the BACK marker */
}

#define POPUP_X 120.0f
#define POPUP_W 400.0f
#define POPUP_ROWS 8

typedef const char* (*popup_name_fn)(int index);

/* A BIOS-style popup list: title, a few rows with the selected one highlighted, a hint line. */
static void
draw_popup(const char* title, const char* hint, int count, int sel, popup_name_fn name) {
    char line[64];
    int first = sel >= POPUP_ROWS ? sel - POPUP_ROWS + 1 : 0;
    int rows = count < POPUP_ROWS ? count : POPUP_ROWS;
    float h = (float)(rows + 2) * GFX_LINE_H + 16.0f;
    gfx_rect(POPUP_X, ROWS_Y - 40.0f, POPUP_W, h, 0.6f, 0xE0102050u);
    gfx_text(title, POPUP_X + 16.0f, ROWS_Y - 32.0f, 0.7f, 0xFFFFFFFFu, 1);
    for (int i = 0; i < rows; i++) {
        float y = ROWS_Y + (float)i * GFX_LINE_H;
        int is_sel = first + i == sel;
        if (is_sel) {
            gfx_rect(POPUP_X + 8.0f, y, POPUP_W - 16.0f, (float)GFX_LINE_H, 0.65f, 0x60FFFFFFu);
        }
        snprintf(line, sizeof(line), "%.38s", name(first + i));
        gfx_text(line, POPUP_X + 16.0f, y, 0.7f, is_sel ? 0xFFFFFFFFu : 0xFFC0C0C0u, is_sel);
    }
    gfx_text(hint, POPUP_X + 16.0f, ROWS_Y + (float)rows * GFX_LINE_H + 2.0f, 0.7f, 0xFFA0A0A0u, 0);
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
    out->icon = BPAGE_ICON_DIGIT(index % 10); /* placeholder icons until the real ones exist */
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
draw_settings(void) {
    bpage_draw(&page, gfx_sink());
    /* more rows above / below the four shown */
    if (page.top > 0) {
        gfx_text("^", 590.0f, 40.0f, 0.5f, 0xFFFFFFFFu, 1);
    }
    if (page.top + BPAGE_SLOTS < page.count) {
        gfx_text("v", 590.0f, 370.0f, 0.5f, 0xFFFFFFFFu, 1);
    }
    if (settings_popup) {
        draw_popup(uis_label(page.cursor), "A: choose   B: cancel", uis_choice_count(page.cursor), settings_popup_sel,
                   settings_choice_name);
    }
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
    } else if (screen == SCREEN_SETTINGS) {
        bscene_draw_background(&menu.bg, gfx_sink());
        draw_settings();
    } else {
        bscene_draw_background(&menu.bg, gfx_sink());
        if (screen == SCREEN_GAMES) {
            draw_games();
            if (recent_open) {
                draw_popup("Recently played", "A: start   B: close", history_recent_count(), recent_row, recent_name);
            }
        }
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
            } else if (menu.selected == ICON_SETTINGS) {
                sound_sfx(BAUDIO_SFX_ENTER);
                bpage_open(&page, &menu, uis_count());
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
handle_settings(button_t b) {
    int row = page.cursor;
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
        case BTN_UP:
            if (bpage_move(&page, -1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_DOWN:
            if (bpage_move(&page, 1)) sound_sfx(BAUDIO_SFX_CURSOR);
            break;
        case BTN_LEFT:
        case BTN_RIGHT:
            uis_change(row, b == BTN_LEFT ? -1 : 1);
            sound_sfx(BAUDIO_SFX_CONFIRM);
            break;
        case BTN_A:
            if (uis_opens_popup(row)) {
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

int
ui_bios_run(const bios_rom* rom) {
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
        } else if (screen == SCREEN_GAMES) {
            handle_games(b);
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
        } else if (screen == SCREEN_GAMES) {
            games_sync();
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
