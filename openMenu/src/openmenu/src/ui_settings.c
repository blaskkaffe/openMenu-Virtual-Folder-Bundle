/* ui_settings: the launcher's settings, see ui_settings.h. */
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <ini.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include <time.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include "clock.h"
#include "serial_log.h"
#include "sound.h"
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
    int sysoff;        /* nonzero: the choice is a byte of the BIOS system settings at 0x8C000078 + sysoff */
} row_def;

static const char* const sort_modes[] = {"A-Z", "SD card order", "Release year"}; /* sf_sort 0, 1 (SORT_NAME), 2 (SORT_DATE) */
static const char* const multidisc_modes[] = {"Show all discs", "Compact"};
static const char* const multidisc_groups[] = {"Anywhere", "Same folder only"};
static const char* const recent_modes[] = {"Off", "Last 10", "Last 20", "Last 30", "Last 40", "Last 50"}; /* as 1.7.1 */
static const char* const clock_modes[] = {"12 hour", "24 hour", "Date only", "12 hour + .beat", "24 hour + .beat"}; /* sf_clock 0..4 */
static const char* const on_off[] = {"Off", "On"};
static const char* const serial_vmu_modes[] = {"Off", "On (A1)", "On (A2)", "On (B1)", "On (B2)", "On (C1)", "On (C2)", "On (D1)", "On (D2)"}; /* as 1.7.1 */
static const char* const vm2_modes[] = {"Send to all", "Send to first", "Off"};
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

/* the BIOS settings */
static const char* const languages[] = {"Japanese", "English", "German", "French", "Spanish", "Italian"};
static const char* const sound_modes[] = {"Stereo", "Mono"};
static const char* const autostart_modes[] = {"On", "Off"};

/* To add a setting, add a row here (and the choice names above). */
static const row_def rows[] = {
    /* the BIOS settings come first, as in the BIOS menu; the bytes live in the BIOS's RAM copy of the system settings and
     * are written to the flash with the clock */
    {0, 0, "BIOS", "Language", "Language of the BIOS messages", NULL, 6, languages, 5},
    {UIS_ACTION_DATETIME, 1, "BIOS", "Date and time", "Set the clock of the console", NULL, 0, NULL, 0},
    {0, 2, "BIOS", "Sound", "Stereo or mono output", NULL, 2, sound_modes, 6},
    {0, 3, "BIOS", "Auto start", "Start the disc in the drive at power on", NULL, 2, autostart_modes, 7},
    {UIS_ACTION_VMUCLOCK, 1, "BIOS", "Memory card clock", "Set the clock of the memory cards", NULL, 0, NULL, 0},
    {0, 1, "BIOS", "Clock extender", "Keep the date in flash at each start", &sf_clock_extender, CLOCK_EXTENDER_END + 1,
     on_off},
    {0, -1, "Game list", "Game order", "Order of the games", &sf_sort, 3, sort_modes},
    {0, -1, "Game list", "Multi-disc games", "List every disc or one entry per set", &sf_multidisc, MULTIDISC_END + 1,
     multidisc_modes},
    {0, -1, "Game list", "Disc sets from", "Where the other discs are looked for", &sf_multidisc_grouping,
     MULTIDISC_GROUPING_END + 1, multidisc_groups},
    {0, -1, "Game list", "Recently played", "Games in the Recent button's list", &sf_recently_played, RECENTLY_PLAYED_END + 1,
     recent_modes},
    {0, -1, "Game list", "Remember last game", "Start browsing where you left off", &sf_remember_last_game,
     REMEMBER_LAST_GAME_END + 1, on_off},
    {0, -1, "Game list", "Serial VMU", "Each game's VMU image kept on the serial SD card", &sf_serial_vmu, SERIAL_VMU_END + 1,
     serial_vmu_modes},
    {0, -1, "Display", "Clock format", ".beat needs Online time sync (the time zone)", &sf_clock, CLOCK_END + 1, clock_modes},
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
    {0, -1, "System", "VM2 game ID", "Tell VM2, VMU Pro and co. the game", &sf_vm2_send_all, VM2_SEND_END + 1, vm2_modes},
    {0, -1, "Starting games", "Boot animation", "What the BIOS shows before a game", &sf_boot_mode, BOOT_MODE_END + 1, boot_modes},
    {0, -1, "Starting games", "Exit to BIOS", "How other discs return to the BIOS", &sf_bios_3d, BIOS_3D_END + 1, disc_exit},
    {0, -1, "Other", "Card defaults", "Use DEFAULTS.INI [BIOSMENU] at start", &sf_honor_defaults, HONOR_DEFAULTS_END + 1,
     on_off},
    {UIS_ACTION_BENCH, -1, "Other", "Performance test", "Scripted run, results on serial port", NULL, 0, NULL},
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

#define SYS(r) (((volatile uint8_t*)0x8C000078)[rows[r].sysoff])

static int sys_dirty;
static char vmu_result[24] = "";

int
uis_get(int row) {
    if (VALID(row) && rows[row].sysoff) {
        int v = SYS(row);
        return v < rows[row].count ? v : 0;
    }
    return VALID(row) && rows[row].var ? (*rows[row].var)[0] : 0;
}

void
uis_set(int row, int choice) {
    if (VALID(row) && rows[row].sysoff && choice >= 0 && choice < rows[row].count) {
        SYS(row) = (uint8_t)choice;
        sys_dirty = 1;
        if (rows[row].sysoff == 6) {
            sound_stereo(choice);
        }
        return;
    }
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
    if (VALID(row) && rows[row].action == UIS_ACTION_VMUCLOCK) {
        return vmu_result;
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
    if (!VALID(row) || !(rows[row].var || rows[row].sysoff)) {
        return;
    }
    int v = (uis_get(row) + delta) % rows[row].count;
    if (v < 0) {
        v += rows[row].count;
    }
    uis_set(row, v);
}

void
uis_run(int row) {
    if (!VALID(row) || rows[row].action != UIS_ACTION_VMUCLOCK) {
        return;
    }
    /* as the BIOS's "Adjust the memory card(s) clock": every memory card gets the console's time */
    int found = 0, set = 0;
    time_t now = time(NULL);
    for (int i = 0; i < MAPLE_PORT_COUNT * MAPLE_UNIT_COUNT; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_CLOCK);
        if (dev) {
            found++;
            set += vmu_set_datetime(dev, now) == MAPLE_EOK;
        }
    }
    if (!found) {
        snprintf(vmu_result, sizeof(vmu_result), "No memory card");
    } else {
        snprintf(vmu_result, sizeof(vmu_result), set == found ? "Done (%d)" : "Failed (%d/%d)", set, found);
    }
}

int
uis_commit(void) {
    if (sys_dirty) {
        sys_dirty = 0;
        save_syscfg_settings();
    }
    return savefile_save() >= 0 ? 0 : -1;
}

/* ---- Card defaults: the [BIOSMENU] section of DEFAULTS.INI on the menu disc ------------------------------------------
 * Derek's Card Manager copies its tools/openMenu/menu_data folder into the menu image and keeps DEFAULTS.INI sections
 * it does not know when it rewrites the file, so a stock Card Manager carries this section to the console. Each key is a
 * settings row's label in lower case with '_' between the words, each value one of its choices the same way, or the
 * choice's number (0 = first):
 *   [BIOSMENU]
 *   game_order=sd_card_order
 *   online_features=on_auto_connect
 *   recently_played=last_20
 * With "Card defaults" on (Settings > Other, the 1.7.1 Honor Menu Defaults byte) the keys that are there win at every
 * start; rows without a key keep what was saved, and what is changed on the console lasts until the next start. The
 * BIOS's own rows (language, sound, auto start: flash, not the save file) are not taken from the card. */
static void
cfg_norm(const char* in, char* out, size_t size) {
    size_t n = 0;
    int gap = 0;
    for (; *in && n + 1 < size; in++) {
        if ((*in == '+' || *in == '-') && isdigit((unsigned char)in[1])) { /* UTC+1 / UTC-1: a word of its own */
            const char* w = *in == '+' ? "plus" : "minus";
            if (n > 0 && n + 2 < size) {
                out[n++] = '_';
            }
            while (*w && n + 1 < size) {
                out[n++] = *w++;
            }
            gap = 1;
            continue;
        }
        if (isalnum((unsigned char)*in)) {
            if (gap && n > 0 && n + 2 < size) {
                out[n++] = '_';
            }
            out[n++] = (char)tolower((unsigned char)*in);
            gap = 0;
        } else {
            gap = 1;
        }
    }
    out[n] = '\0';
}

static int cfg_applied;

static int
cfg_handler(void* user, const char* section, const char* name, const char* value) {
    (void)user;
    if (strcasecmp(section, "BIOSMENU") != 0) {
        return 1;
    }
    char key[48], want[48], have[48], line[96];
    cfg_norm(name, key, sizeof(key));
    cfg_norm(value, want, sizeof(want));
    for (int r = 0; r < NUM_ROWS; r++) {
        if (!rows[r].var || rows[r].sysoff || rows[r].var == &sf_honor_defaults) {
            continue;
        }
        cfg_norm(rows[r].label, have, sizeof(have));
        if (strcmp(have, key) != 0) {
            continue;
        }
        int pick = -1;
        char* end;
        long n = strtol(value, &end, 10);
        if (end != value && *end == '\0' && n >= 0 && n < rows[r].count) {
            pick = (int)n;
        }
        for (int c = 0; pick < 0 && c < rows[r].count; c++) {
            cfg_norm(rows[r].names[c], have, sizeof(have));
            if (!strcmp(have, want)) {
                pick = c;
            }
        }
        if (pick < 0) {
            snprintf(line, sizeof(line), "bad value %.24s=%.40s", name, value);
            serial_log_block("cfg", line);
            return 1;
        }
        (*rows[r].var)[0] = (uint8_t)pick;
        cfg_applied++;
        return 1;
    }
    snprintf(line, sizeof(line), "unknown key %.40s", name);
    serial_log_block("cfg", line);
    return 1;
}

int
uis_apply_card_defaults(void) {
    cfg_applied = 0;
    if (sf_honor_defaults[0] != HONOR_DEFAULTS_ON) {
        return 0;
    }
    if (ini_parse("/cd/DEFAULTS.INI", cfg_handler, NULL) == -1) {
        return 0; /* no file: nothing to do */
    }
    settings_sanitize();
    char line[48];
    snprintf(line, sizeof(line), "%d settings from DEFAULTS.INI", cfg_applied);
    serial_log_block("cfg", line);
    return cfg_applied;
}
