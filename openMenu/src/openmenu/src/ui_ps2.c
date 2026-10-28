/*
 * PS2-style menu on the PVR, see ui_ps2.h. The drawing and the screens are in ps2_engine; this
 * file connects them to the console: the game list (ui_list), the settings (ui_settings), the
 * save file, sound, the launch path and the controller.
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <arch/timer.h>
#include <dc/maple.h>
#include <kos/fs.h>

#include <backend/db_list.h>
#include <backend/gd_list.h>
#include <bios_audio.h>
#include <openmenu_settings.h>
#include <ps2_ui.h>

#include "gfx.h"
#include "history.h"
#include "input.h"
#include "launch.h"
#include "sound.h"
#include "ui_list.h"
#include "ui_ps2.h"
#include "ui_settings.h"

#define PAK_PATH "/cd/PS2.PAK"
#define MAX_PAK_SIZE (4 * 1024 * 1024)

static uint8_t* pak_data;
static ps2_pack pack;
static ps2_ui ui;

static const gd_item* launch_game;   /* picked by the list, started when the animation ends */
static int games_opened;             /* the list has been shown once (last game restored) */
static int have_meta;                /* META.DAT loaded: players, VMU blocks */

/* ---- Pack ------------------------------------------------------------------------------- */

static int
load_pack(void) {
    file_t fd = fs_open(PAK_PATH, O_RDONLY);
    if (fd == FILEHND_INVALID) {
        return -1;
    }
    ssize_t size = fs_total(fd);
    pak_data = (size > 0 && size <= MAX_PAK_SIZE) ? (uint8_t*)memalign(32, ((size_t)size + 31) & ~(size_t)31) : NULL;
    if (pak_data && fs_read(fd, pak_data, (size_t)size) != size) {
        free(pak_data);
        pak_data = NULL;
    }
    fs_close(fd);
    if (!pak_data) {
        return -1;
    }
    if (ps2_pack_open(&pack, pak_data, (size_t)size) != PS2_PACK_OK) {
        free(pak_data);
        pak_data = NULL;
        return -1;
    }
    return 0;
}

/* ---- Game list -------------------------------------------------------------------------- */

static int
h_game_count(void* user) {
    (void)user;
    return uil_count();
}

static int
h_game_cursor(void* user) {
    (void)user;
    return uil_cursor();
}

static void
h_game_get(void* user, int index, ps2_game_entry* out) {
    (void)user;
    const gd_item* item = uil_item(index);
    if (!item) {
        return;
    }
    snprintf(out->name, sizeof(out->name), "%s", item->name);
    out->is_folder = uil_is_folder(item);
    out->disc_total = 1;
    if (out->is_folder) {
        return;
    }
    snprintf(out->product, sizeof(out->product), "%s", item->product);
    out->disc_total = uil_disc_total(item);
    out->disc_index = index == uil_cursor() ? uil_disc_index() : 0;
    char* p = out->info;
    size_t left = sizeof(out->info);
    int n = snprintf(p, left, "%.11s  %.3s", item->product, item->region);
    if (n > 0 && (size_t)n < left) {
        p += n;
        left -= (size_t)n;
    }
    struct db_item* meta = NULL;
    if (have_meta && !db_get_meta(item->product, &meta) && meta && meta->num_players) {
        snprintf(p, left, "  %d player%s", meta->num_players, meta->num_players > 1 ? "s" : "");
    }
}

static int
h_game_nav(void* user, int nav) {
    (void)user;
    static const button_t map[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B};
    if (nav < 0 || nav >= (int)(sizeof(map) / sizeof(map[0]))) {
        return PS2_NAVR_NONE;
    }
    const gd_item* game = NULL;
    uil_result r = uil_button(map[nav], &game);
    switch (r) {
        case UIL_LAUNCH:
            launch_game = game;
            return PS2_NAVR_LAUNCH;
        case UIL_EXIT: return PS2_NAVR_EXIT;
        case UIL_REDRAW: return PS2_NAVR_CHANGED;
        default: return PS2_NAVR_NONE;
    }
}

/* The open folder as "A / B": the list keeps its path with '/' between the names. */
static void
h_game_folder(void* user, char* out, size_t n) {
    (void)user;
    list_view view;
    list_view_get(&view);
    out[0] = '\0';
    size_t len = 0;
    for (const char* p = view.folder_path ? view.folder_path : ""; *p && len + 4 < n; p++) {
        if (*p == '/') {
            out[len++] = ' ';
            out[len++] = '/';
            out[len++] = ' ';
        } else {
            out[len++] = *p;
        }
    }
    out[len] = '\0';
}

static void
h_game_opened(void* user) {
    (void)user;
    if (games_opened) {
        return;
    }
    games_opened = 1;
    int row = history_restore();
    if (row >= 0) {
        uil_goto_real(row);
    }
}

static void
h_game_launch(void* user) {
    (void)user;
    launch_disc(launch_game); /* only returns if the launch failed */
}

static int
h_launch_anim(void* user) {
    (void)user;
    return sf_scroll_art[0] == SCROLL_ART_ON; /* "Launch animation" setting */
}

static int
h_recent_count(void* user) {
    (void)user;
    return history_recent_count();
}

static const char*
h_recent_name(void* user, int index) {
    (void)user;
    const gd_item* g = history_recent(index);
    return g ? g->name : NULL;
}

static void
h_recent_launch(void* user, int index) {
    (void)user;
    const gd_item* g = history_recent(index);
    if (g) {
        launch_disc(g); /* only returns if the launch failed */
    }
}

/* ---- Settings --------------------------------------------------------------------------- */

static int h_set_count(void* u) { (void)u; return uis_count(); }
static const char* h_set_label(void* u, int r) { (void)u; return uis_label(r); }
static const char* h_set_group(void* u, int r) { (void)u; return uis_group(r); }
static const char* h_set_help(void* u, int r) { (void)u; return uis_help(r); }
static const char* h_set_value(void* u, int r) { (void)u; return uis_value(r); }
static int h_set_choice_count(void* u, int r) { (void)u; return uis_choice_count(r); }
static const char* h_set_choice_name(void* u, int r, int c) { (void)u; return uis_choice_name(r, c); }
static int h_set_get(void* u, int r) { (void)u; return uis_get(r); }
static void h_set_set(void* u, int r, int c) { (void)u; uis_set(r, c); }
static int h_set_opens_popup(void* u, int r) { (void)u; return uis_opens_popup(r); }
static void h_set_change(void* u, int r, int d) { (void)u; uis_change(r, d); }

static int
h_set_leave(void* user) {
    (void)user;
    int err = uis_commit();
    /* order, multi-disc and recent list changes show up in the list again */
    list_set_folder_root();
    uil_reset();
    return err;
}

/* ---- Misc ------------------------------------------------------------------------------- */

static int
h_memcard_present(void* user, int slot) {
    (void)user;
    return maple_enum_type(slot, MAPLE_FUNC_MEMCARD) != NULL;
}

static void
h_clock_text(void* user, char* out, size_t n) {
    (void)user;
    time_t now = time(NULL);
    struct tm* t = gmtime(&now); /* the console's clock holds local time */
    if (t) {
        snprintf(out, n, "%02d/%02d/%04d  %02d:%02d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour, t->tm_min);
    }
}

static void
h_sfx(void* user, int s) {
    (void)user;
    static const int map[] = {BAUDIO_SFX_CURSOR, BAUDIO_SFX_CONFIRM, BAUDIO_SFX_CANCEL, BAUDIO_SFX_ENTER, BAUDIO_SFX_ERROR};
    if (s >= 0 && s < (int)(sizeof(map) / sizeof(map[0]))) {
        sound_sfx(map[s]);
    }
}

static ps2_btn
to_ps2(button_t b) {
    switch (b) {
        case BTN_UP: return PS2_BTN_UP;
        case BTN_DOWN: return PS2_BTN_DOWN;
        case BTN_LEFT: return PS2_BTN_LEFT;
        case BTN_RIGHT: return PS2_BTN_RIGHT;
        case BTN_A: return PS2_BTN_A;
        case BTN_B: return PS2_BTN_B;
        case BTN_X: return PS2_BTN_X;
        case BTN_START: return PS2_BTN_START;
        default: return PS2_BTN_NONE;
    }
}

int
ui_ps2_run(const bios_rom* rom) {
    if (load_pack() != 0) {
        return -1;
    }

    ps2_host host;
    memset(&host, 0, sizeof(host));
    host.game_count = h_game_count;
    host.game_cursor = h_game_cursor;
    host.game_get = h_game_get;
    host.game_nav = h_game_nav;
    host.game_folder = h_game_folder;
    host.game_opened = h_game_opened;
    host.game_launch = h_game_launch;
    host.launch_anim = h_launch_anim;
    host.recent_count = h_recent_count;
    host.recent_name = h_recent_name;
    host.recent_launch = h_recent_launch;
    host.set_count = h_set_count;
    host.set_label = h_set_label;
    host.set_group = h_set_group;
    host.set_help = h_set_help;
    host.set_value = h_set_value;
    host.set_choice_count = h_set_choice_count;
    host.set_choice_name = h_set_choice_name;
    host.set_get = h_set_get;
    host.set_set = h_set_set;
    host.set_opens_popup = h_set_opens_popup;
    host.set_change = h_set_change;
    host.set_leave = h_set_leave;
    host.memcard_present = h_memcard_present;
    host.clock_text = h_clock_text;
    host.sfx = h_sfx;
    /* nothing has touched the PVR yet: if anything is missing the caller can still use the BIOS-style menu */
    if (ps2_ui_init(&ui, &pack, &host, gfx_ps2_sink()) != 0 || gfx_init(NULL) != 0 || gfx_ps2_load(&pack) != 0) {
        return -1;
    }

    /* game info (players) comes from META.DAT on the menu disc; db_load_DAT needs it */
    file_t meta_fd = fs_open("/cd/META.DAT", O_RDONLY);
    if (meta_fd != FILEHND_INVALID) {
        fs_close(meta_fd);
        db_load_DAT();
        have_meta = 1;
    }
    if (rom) {
        sound_init(rom);
    }

    uint32_t top, bottom;
    ps2_ui_background(&top, &bottom);
    uint64_t last_us = timer_us_gettime64();
    for (;;) {
        ps2_ui_button(&ui, to_ps2(input_poll()));

        uint64_t now = timer_us_gettime64();
        float dt = (float)(now - last_us) / 1000000.0f; /* animation runs on real time, at 50 or 60 Hz alike */
        last_us = now;
        ps2_ui_update(&ui, dt);

        gfx_begin_frame(top, bottom);
        ps2_ui_draw(&ui);
        gfx_end_frame(); /* pvr_wait_ready() in the next gfx_begin_frame() paces this to the display */
    }
    return 0;
}
