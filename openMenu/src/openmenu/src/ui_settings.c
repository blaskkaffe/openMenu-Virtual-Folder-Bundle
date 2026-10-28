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

#include "bg_palette.h"
#include "clock.h"
#include "favorites.h"
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

/* This menu's own settings: the choice is bits of favorites_flags() (favorites.h), not OPENMENU.SYS. The bytes only give the rows a
 * variable to point at (so the card defaults can name them); uis_get / uis_set read and write the flags. */
static const char* const bg_modes[] = {"BIOS blue", "Green", "Orange", "Purple", "Red", "Grey"}; /* bg_palette.c */
static uint8_t bg_byte[1], dark_byte[1];
static uint8_t* bg_var = bg_byte;
static uint8_t* dark_var = dark_byte;

/* To add a setting, add a row here (and the choice names above). The rows run in the order of the Settings list; the last NET_ROWS rows
 * are not in it, the Internet screen shows them (uis_net_first). */
static const row_def rows[] = {
    /* the BIOS settings come first, as in the BIOS menu; the bytes live in the BIOS's RAM copy of the system settings and
     * are written to the flash with the clock */
    {0, UIS_ICON_LANGUAGE, "BIOS", "Language", "Language of the BIOS messages", NULL, 6, languages, 5},
    {0, UIS_ICON_SOUND, "BIOS", "Sound", "Stereo or mono output", NULL, 2, sound_modes, 6},
    {0, UIS_ICON_OTHER, "BIOS", "Auto start", "Start the disc in the drive at power on", NULL, 2, autostart_modes, 7},
    /* everything about the clock and the date, one group: the BIOS clock, the header clock, and the three ways to keep it right */
    {UIS_ACTION_DATETIME, UIS_ICON_CLOCK, "Clock and date", "Date and time", "Set the clock of the console", NULL, 0, NULL, 0},
    {0, UIS_ICON_CLOCK, "Clock and date", "Clock format", ".beat uses the Online time sync zone", &sf_clock, 5, clock_modes},
    {UIS_ACTION_VMUCLOCK, UIS_ICON_CLOCK, "Clock and date", "Memory card clock", "Set the clock of the memory cards", NULL, 0, NULL, 0},
    {0, UIS_ICON_CLOCK, "Clock and date", "Clock extender", "Keep the date in flash at each start", &sf_clock_extender, CLOCK_EXTENDER_END + 1,
     on_off},
    {0, UIS_ICON_CLOCK, "Clock and date", "VMU time sync", "Set the clock from a VMU at start", &sf_vmu_time_sync, VMU_TIME_SYNC_END + 1, on_off},
    {0, UIS_ICON_CLOCK, "Clock and date", "Online time sync", "Set the clock from the internet", &sf_online_time_sync, ONLINE_TIME_SYNC_END + 1,
     time_sync_modes},
    {0, -1, "Display", "Aspect ratio", "16:9 is squeezed for wide TVs", &sf_aspect, ASPECT_END + 1, aspect_modes},
    {0, -1, "Display", "Background", "Colour of the menu background", &bg_var, BG_PALETTE_COUNT, bg_modes},
    {0, -1, "Display", "Dark mode", "Dim the background", &dark_var, 2, on_off},
    {0, UIS_ICON_DISC, "Game list", "Game order", "Order of the games", &sf_sort, 3, sort_modes},
    {0, UIS_ICON_DISC, "Game list", "Multi-disc games", "List every disc or one entry per set", &sf_multidisc, MULTIDISC_END + 1,
     multidisc_modes},
    {0, UIS_ICON_DISC, "Game list", "Disc sets from", "Where the other discs are looked for", &sf_multidisc_grouping,
     MULTIDISC_GROUPING_END + 1, multidisc_groups},
    {0, UIS_ICON_DISC, "Game list", "Recently played", "Games in the Recent button's list", &sf_recently_played, RECENTLY_PLAYED_END + 1,
     recent_modes},
    {0, UIS_ICON_DISC, "Game list", "Remember last game", "Start browsing where you left off", &sf_remember_last_game,
     REMEMBER_LAST_GAME_END + 1, on_off},
    {0, UIS_ICON_VMU, "Game list", "Serial VMU", "Each game's VMU image kept on the serial SD card", &sf_serial_vmu, SERIAL_VMU_END + 1,
     serial_vmu_modes},
    {0, UIS_ICON_VMU, "Game list", "Serial VMU Slots", "Five VMU images per game, asked at each start", &sf_serial_vmu_multislot,
     SERIAL_VMU_MULTISLOT_END + 1, on_off},
    {0, UIS_ICON_DISC, "Starting games", "Boot animation", "What the BIOS shows before a game", &sf_boot_mode, BOOT_MODE_END + 1, boot_modes},
    {0, UIS_ICON_DISC, "Starting games", "Exit to BIOS", "How other discs return to the BIOS", &sf_bios_3d, BIOS_3D_END + 1, disc_exit},
    {0, UIS_ICON_SOUND, "System", "VMU beep on save", "The VMU beeps when a save is written", &sf_beep, BEEP_END + 1, on_off},
    {0, UIS_ICON_VMU, "System", "VM2 game ID", "Tell VM2, VMU Pro and co. the game", &sf_vm2_send_all, VM2_SEND_END + 1, vm2_modes},
    /* the main branch's online (DC Now) setting, same values: Off, On, Auto-connect. While it is on, the third main
     * menu icon is "Internet" instead of "Music". It stays here: the Internet screen only exists once it is on. */
    {0, -1, "Internet", "Online features", "On: the Music icon becomes Internet", &sf_dcnow, DCNOW_END + 1, online_modes},
    {0, -1, "Other", "Card defaults", "Use DEFAULTS.INI [BIOSMENU] at start", &sf_honor_defaults, HONOR_DEFAULTS_END + 1,
     on_off},
    {UIS_ACTION_BENCH, -1, "Other", "Performance test", "Scripted run, results on serial port", NULL, 0, NULL},
    {UIS_ACTION_ABOUT, UIS_ICON_OTHER, "Other", "About", "Version and credits", NULL, 0, NULL},
    /* shown on the Internet screen's Settings page */
    {0, -1, "Internet", "Player list refresh", "How often the online lists update", &sf_dcnow_refresh, DCNOW_REFRESH_END + 1,
     refresh_modes},
    {0, -1, "Internet", "VMU online status", "Online status on the VMU screen", &sf_dcnow_vmu, DCNOW_VMU_END + 1, on_off},
    {0, UIS_ICON_CLOCK, "Internet", "Online time sync", "Set the clock from the internet", &sf_online_time_sync, ONLINE_TIME_SYNC_END + 1,
     time_sync_modes},
};

#define NET_ROWS 3
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

#define VALID(r) ((r) >= 0 && (r) < NUM_ROWS)

int
uis_count(void) {
    return NUM_ROWS - NET_ROWS;
}

int
uis_net_first(void) {
    return NUM_ROWS - NET_ROWS;
}

int
uis_net_count(void) {
    return NET_ROWS;
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

/* Clock format: the 12 / 24 hour / date only choice is the save file's sf_clock (as 1.7.1), the .beat part is a flag of this menu's own file
 * (favorites.h), so OPENMENU.SYS only ever holds what 1.7.1 knows. 0 12 hour, 1 24 hour, 2 date only, 3 12 hour + .beat, 4 24 hour + .beat. */
int
uis_clock_mode(void) {
    int base = sf_clock[0];
    return (favorites_flags() & FORK_FLAG_BEAT) && base < 2 ? base + 3 : base;
}

static void
clock_mode_set(int mode) {
    sf_clock[0] = (uint8_t)(mode >= 3 ? mode - 3 : mode);
    unsigned flags = favorites_flags() & ~(unsigned)FORK_FLAG_BEAT;
    favorites_set_flags(mode >= 3 ? flags | FORK_FLAG_BEAT : flags);
}

/* An older build of this menu saved the .beat choices as sf_clock 3 and 4 (the loader turned them into 12 / 24 hour and noted it): move
 * that to the flag. Call once the favorites file is read. */
void
uis_clock_migrate(void) {
    if (sf_clock_legacy) {
        favorites_set_flags(favorites_flags() | FORK_FLAG_BEAT);
        sf_clock_legacy = 0;
    }
}

int
uis_get(int row) {
    if (VALID(row) && rows[row].sysoff) {
        int v = SYS(row);
        return v < rows[row].count ? v : 0;
    }
    if (VALID(row) && rows[row].var == &sf_clock) {
        return uis_clock_mode();
    }
    if (VALID(row) && rows[row].var == &bg_var) {
        int v = (int)((favorites_flags() & FORK_BG_MASK) >> FORK_BG_SHIFT);
        return v < BG_PALETTE_COUNT ? v : 0;
    }
    if (VALID(row) && rows[row].var == &dark_var) {
        return (favorites_flags() & FORK_FLAG_DARK) != 0;
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
    if (VALID(row) && rows[row].var == &sf_clock && choice >= 0 && choice < rows[row].count) {
        clock_mode_set(choice);
        return;
    }
    if (VALID(row) && rows[row].var == &bg_var && choice >= 0 && choice < rows[row].count) {
        favorites_set_flags((favorites_flags() & ~(unsigned)FORK_BG_MASK) | ((unsigned)choice << FORK_BG_SHIFT));
        return;
    }
    if (VALID(row) && rows[row].var == &dark_var && choice >= 0 && choice < rows[row].count) {
        favorites_set_flags(choice ? favorites_flags() | FORK_FLAG_DARK : favorites_flags() & ~(unsigned)FORK_FLAG_DARK);
        return;
    }
    if (VALID(row) && rows[row].var && choice >= 0 && choice < rows[row].count) {
        (*rows[row].var)[0] = (uint8_t)choice;
    }
}

void
uis_background(uint32_t* top, uint32_t* bottom) {
    unsigned f = favorites_flags();
    bg_palette_apply((int)((f & FORK_BG_MASK) >> FORK_BG_SHIFT), (f & FORK_FLAG_DARK) != 0, top, bottom);
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
        if (!rows[r].var || rows[r].sysoff || rows[r].var == &sf_honor_defaults || rows[r].var == &bg_var ||
            rows[r].var == &dark_var) { /* those two live in the favorites file, which is read after this */
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
