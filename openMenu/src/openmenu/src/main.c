/*
 * File: main.c
 * openMenu with a BIOS-style front end: boot, list the games on the GDEMU SD card,
 * launch one. The online features are the full openMenu's (see online.h); its themes and music are left out.
 *
 * If the console's boot ROM is a revision we know, the menu is built from its own
 * models, textures and scripts (see bios_rom / bios_engine). Otherwise a plain
 * text list is shown.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/video.h>
#include <kos.h>

#include <backend/gd_list.h>
#include <bios_rom.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "dbglog.h"
#include "serial_log.h"
#include "online.h"
#include "ui_bios.h"
#include "video.h"
#include "ui_fallback.h"
#include "ui_settings.h"

int
main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    dbglog_boot("main", (unsigned)timer_ms_gettime64());
    serial_log_banner();
    maple_wait_scan();
    dbglog_boot("maple", (unsigned)timer_ms_gettime64());

    video_init();
    dbglog_boot("video", (unsigned)timer_ms_gettime64());
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

    dbglog_boot("rom", (unsigned)timer_ms_gettime64());
    savefile_init();
    uis_apply_card_defaults(); /* the card's [BIOSMENU] defaults (stock Card Manager), before online_boot reads them */
    dbglog_boot("save", (unsigned)timer_ms_gettime64());
    online_boot(); /* Auto-Connect dials on its own thread while the game list loads */

    if (list_read_default() == 0) {
        list_folder_init();
        list_set_folder_root(); /* folders first; sorted as the Sort setting says */
    }
    dbglog_boot("list", (unsigned)timer_ms_gettime64());

    if (rom_err == BIOS_ROM_OK) {
        ui_bios_run(&rom); /* only returns if the PVR cannot be set up */
    }
    ui_fallback_run(status);

    savefile_close();
    return 0;
}
