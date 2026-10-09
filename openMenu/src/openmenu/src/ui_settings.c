/* ui_settings: the launcher's settings, see ui_settings.h. */
#include <stdio.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "ui_settings.h"

typedef struct {
    const char* group;
    const char* label; /* up to about 20 characters */
    const char* help;  /* up to about 40 characters */
    uint8_t** var;     /* points at the saved setting (a pointer into the save file buffer) */
    int count;
    const char* const* names;
} row_def;

static const char* const sort_modes[] = {"A-Z", "SD card order"};
static const char* const multidisc_modes[] = {"Show all discs", "Compact"};
static const char* const multidisc_groups[] = {"Anywhere", "Same folder only"};
static const char* const recent_modes[] = {"Off", "Last 5", "Last 10", "Last 15", "Last 20", "Last 25", "Last 50"};
static const char* const on_off[] = {"Off", "On"};
static const char* const boot_modes[] = {"Full", "License only", "Animation only", "Fast"};
static const char* const disc_exit[] = {"Standard", "Alternate", "Alternate 3D"};

/* To add a setting, add a row here (and the choice names above). */
static const row_def rows[] = {
    {"Game list", "Game order", "Order of the games", &sf_sort, 2, sort_modes},
    {"Game list", "Multi-disc games", "List every disc or one entry per set", &sf_multidisc, MULTIDISC_END + 1, multidisc_modes},
    {"Game list", "Disc sets from", "Where the other discs are looked for", &sf_multidisc_grouping, MULTIDISC_GROUPING_END + 1,
     multidisc_groups},
    {"Game list", "Recently played", "Games in the popup opened with X", &sf_recently_played, RECENTLY_PLAYED_END + 1, recent_modes},
    {"Game list", "Remember last game", "Start browsing where you left off", &sf_remember_last_game, REMEMBER_LAST_GAME_END + 1,
     on_off},
    {"Starting games", "Boot animation", "What the BIOS shows before a game", &sf_boot_mode, BOOT_MODE_END + 1, boot_modes},
    /* stored in the otherwise unused "scroll art" setting (default On) */
    {"Starting games", "Launch animation", "Discs fly out before a game starts", &sf_scroll_art, 2, on_off},
    {"Starting games", "Exit to BIOS", "How non-game discs return to the BIOS", &sf_bios_3d, BIOS_3D_END + 1, disc_exit},
};

#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

#define VALID(r) ((r) >= 0 && (r) < NUM_ROWS)

int
uis_count(void) {
    return NUM_ROWS;
}

const char*
uis_label(int row) {
    return VALID(row) ? rows[row].label : "";
}

const char*
uis_group(int row) {
    return VALID(row) ? rows[row].group : "";
}

const char*
uis_help(int row) {
    return VALID(row) ? rows[row].help : "";
}

int
uis_choice_count(int row) {
    return VALID(row) ? rows[row].count : 0;
}

const char*
uis_choice_name(int row, int choice) {
    return VALID(row) && choice >= 0 && choice < rows[row].count ? rows[row].names[choice] : "?";
}

int
uis_get(int row) {
    return VALID(row) ? (*rows[row].var)[0] : 0;
}

void
uis_set(int row, int choice) {
    if (VALID(row) && choice >= 0 && choice < rows[row].count) {
        (*rows[row].var)[0] = (uint8_t)choice;
    }
}

const char*
uis_value(int row) {
    return uis_choice_name(row, uis_get(row));
}

int
uis_opens_popup(int row) {
    return VALID(row) && rows[row].count > 2;
}

void
uis_change(int row, int delta) {
    if (!VALID(row)) {
        return;
    }
    int v = (uis_get(row) + delta) % rows[row].count;
    if (v < 0) {
        v += rows[row].count;
    }
    uis_set(row, v);
}

int
uis_commit(void) {
    return savefile_save() >= 0 ? 0 : -1;
}
