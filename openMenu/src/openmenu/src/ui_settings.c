/* ui_settings: the launcher's settings, see ui_settings.h. */
#include <stdio.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "clock.h"
#include "ui_settings.h"

typedef struct {
    int action;        /* UIS_ACTION_*, 0 for a choice */
    int icon;          /* -1 digit, 0..3 BIOS icon */
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
static const char* const aspect_modes[] = {"4:3", "16:9"};
static const char* const boot_modes[] = {"Full", "License only", "Animation only", "Fast"};
static const char* const disc_exit[] = {"Standard", "Alternate", "Alternate 3D"};
/* the main branch's choice names */
static const char* const online_modes[] = {"Off", "On (Manual Connect)", "On (Auto-Connect)"};
static const char* const refresh_modes[] = {"Off", "10 seconds", "20 seconds", "30 seconds", "45 seconds", "60 seconds"};
static const char* const time_sync_modes[] = {
    "Off",         "On (UTC-12)",    "On (UTC-11)", "On (UTC-10)",    "On (UTC-9:30)",  "On (UTC-9)",     "On (UTC-8)",
    "On (UTC-7)",  "On (UTC-6)",     "On (UTC-5)",  "On (UTC-4)",     "On (UTC-3:30)",  "On (UTC-3)",     "On (UTC-2)",
    "On (UTC-1)",  "On (UTC+0)",     "On (UTC+1)",  "On (UTC+2)",     "On (UTC+3)",     "On (UTC+3:30)",  "On (UTC+4)",
    "On (UTC+4:30)", "On (UTC+5)",   "On (UTC+5:30)", "On (UTC+5:45)", "On (UTC+6)",    "On (UTC+6:30)",  "On (UTC+7)",
    "On (UTC+8)",  "On (UTC+8:45)",  "On (UTC+9)",  "On (UTC+9:30)",  "On (UTC+10)",    "On (UTC+10:30)", "On (UTC+11)",
    "On (UTC+12)", "On (UTC+12:45)", "On (UTC+13)", "On (UTC+14)"};

/* To add a setting, add a row here (and the choice names above). */
static const row_def rows[] = {
    {0, -1, "Game list", "Game order", "Order of the games", &sf_sort, 2, sort_modes},
    {0, -1, "Game list", "Multi-disc games", "List every disc or one entry per set", &sf_multidisc, MULTIDISC_END + 1,
     multidisc_modes},
    {0, -1, "Game list", "Disc sets from", "Where the other discs are looked for", &sf_multidisc_grouping,
     MULTIDISC_GROUPING_END + 1, multidisc_groups},
    {0, -1, "Game list", "Recently played", "Games in the Recent button's list", &sf_recently_played, RECENTLY_PLAYED_END + 1,
     recent_modes},
    {0, -1, "Game list", "Remember last game", "Start browsing where you left off", &sf_remember_last_game,
     REMEMBER_LAST_GAME_END + 1, on_off},
    {0, -1, "Display", "Aspect ratio", "16:9 is squeezed for wide TVs", &sf_aspect, ASPECT_END + 1, aspect_modes},
    /* the main branch's online (DC Now) setting, same values: Off, On, Auto-connect. While it is on, the third main
     * menu icon is "Internet" instead of "Music" */
    {0, -1, "Online", "Online features", "On: the Music icon becomes Internet", &sf_dcnow, DCNOW_END + 1, online_modes},
    {0, -1, "Online", "Player list refresh", "How often the online lists update", &sf_dcnow_refresh, DCNOW_REFRESH_END + 1,
     refresh_modes},
    {0, -1, "Online", "VMU online status", "Online status on the VMU screen", &sf_dcnow_vmu, DCNOW_VMU_END + 1, on_off},
    {0, -1, "Online", "Online time sync", "Set the clock from the internet", &sf_online_time_sync, ONLINE_TIME_SYNC_END + 1,
     time_sync_modes},
    {0, -1, "System", "VMU time sync", "Set the clock from a VMU at start", &sf_vmu_time_sync, VMU_TIME_SYNC_END + 1, on_off},
    {UIS_ACTION_DATETIME, 1, "System", "Date and time", "Set the clock of the console", NULL, 0, NULL},
    {0, -1, "Starting games", "Boot animation", "What the BIOS shows before a game", &sf_boot_mode, BOOT_MODE_END + 1, boot_modes},
    {0, -1, "Starting games", "Exit to BIOS", "How other discs return to the BIOS", &sf_bios_3d, BIOS_3D_END + 1, disc_exit},
    {UIS_ACTION_ABOUT, 3, "Other", "About", "Version and credits", NULL, 0, NULL},
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
    return VALID(row) && rows[row].var ? (*rows[row].var)[0] : 0;
}

void
uis_set(int row, int choice) {
    if (VALID(row) && rows[row].var && choice >= 0 && choice < rows[row].count) {
        (*rows[row].var)[0] = (uint8_t)choice;
    }
}

const char*
uis_value(int row) {
    static char date[24];
    if (VALID(row) && rows[row].action == UIS_ACTION_DATETIME) {
        int y, mo, d, h, mi;
        clock_get(&y, &mo, &d, &h, &mi);
        snprintf(date, sizeof(date), "%02d-%02d %02d:%02d", mo, d, h, mi); /* the year is in the editor */
        return date;
    }
    if (VALID(row) && rows[row].action) {
        return "";
    }
    return uis_choice_name(row, uis_get(row));
}

int
uis_action(int row) {
    return VALID(row) ? rows[row].action : UIS_ACTION_NONE;
}

int
uis_icon(int row) {
    return VALID(row) ? rows[row].icon : -1;
}

int
uis_opens_popup(int row) {
    return VALID(row) && rows[row].count > 2;
}

void
uis_change(int row, int delta) {
    if (!VALID(row) || !rows[row].var) {
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
