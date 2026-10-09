/* ui_settings: boot related options of the launcher, see ui_settings.h. */
#include <stdio.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "ui_settings.h"

typedef struct {
    const char* label;
    uint8_t** var; /* points at the saved setting (a pointer into the save file buffer) */
    int count;
    const char* const* names;
} option;

static const char* const boot_modes[] = {"Full", "License only", "Animation only", "Fast"};
static const char* const disc_exit[] = {"Standard", "Alternate", "Alternate 3D"};

static const char* const multidisc_modes[] = {"Show all discs", "Compact"};
static const char* const multidisc_groups[] = {"Anywhere", "Same folder only"};

static const option options[] = {
    {"Multi-disc games", &sf_multidisc, MULTIDISC_END + 1, multidisc_modes},
    {"Disc sets from", &sf_multidisc_grouping, MULTIDISC_GROUPING_END + 1, multidisc_groups},
    {"Boot animation", &sf_boot_mode, BOOT_MODE_END + 1, boot_modes},
    {"Exit to BIOS", &sf_bios_3d, BIOS_3D_END + 1, disc_exit},
};

#define NUM_OPTIONS ((int)(sizeof(options) / sizeof(options[0])))

int
uis_count(void) {
    return NUM_OPTIONS;
}

void
uis_text(int i, char* out, size_t size) {
    if (i < 0 || i >= NUM_OPTIONS) {
        snprintf(out, size, "%s", "");
        return;
    }
    const option* o = &options[i];
    int v = (*o->var)[0];
    snprintf(out, size, "%s: %s", o->label, (v >= 0 && v < o->count) ? o->names[v] : "?");
}

void
uis_change(int i, int delta) {
    if (i < 0 || i >= NUM_OPTIONS) {
        return;
    }
    const option* o = &options[i];
    int v = ((*o->var)[0] + delta) % o->count;
    if (v < 0) {
        v += o->count;
    }
    (*o->var)[0] = (uint8_t)v;
}

int
uis_commit(void) {
    return savefile_save() >= 0 ? 0 : -1;
}
