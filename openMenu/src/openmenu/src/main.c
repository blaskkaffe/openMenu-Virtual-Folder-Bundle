/*
 * File: main.c
 * openMenu with a BIOS-style front end: boot, list the games on the GDEMU SD card,
 * launch one. Everything else the full openMenu does (themes, artwork, online
 * features, music, VMU integration) is left out on purpose.
 *
 * If the console's boot ROM is a revision we know, the menu is built from its own
 * models, textures and scripts (see bios_rom / bios_engine). Otherwise a plain
 * text list is shown.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/video.h>
#include <kos.h>

#include <backend/gd_list.h>
#include <bios_rom.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "ui_bios.h"
#include "video.h"
#include "ui_fallback.h"

int
main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    maple_wait_scan();

    video_init();
    bfont_set_encoding(BFONT_CODE_ISO8859_1);

    /* The boot ROM is memory mapped (uncached) at 0xA0000000. */
    static bios_rom rom;
    char status[64];
    int rom_err = bios_rom_init(&rom, (const void*)0xA0000000, BIOS_ROM_SIZE);
    if (rom_err == BIOS_ROM_OK) {
        snprintf(status, sizeof(status), "BIOS %s", rom.revision);
    } else {
        snprintf(status, sizeof(status), "BIOS ROM not usable (error %d)", rom_err);
    }

    savefile_init();

    if (list_read_default() == 0) {
        list_folder_init();
        list_set_sort_alphabetical();
    }

    if (rom_err == BIOS_ROM_OK) {
        ui_bios_run(&rom); /* only returns if the PVR cannot be set up */
    }
    ui_fallback_run(status);

    savefile_close();
    return 0;
}
