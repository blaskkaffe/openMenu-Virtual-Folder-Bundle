/*
 * File: launch.c
 * Mount a GDEMU disc image and hand control to the game.
 *
 * This is the launch path of the full openMenu reduced to the essentials: no
 * history, no VM2 ids, no background music, no network teardown.
 */
#include <stdint.h>
#include <string.h>

#include <arch/arch.h>
#include <dc/cdrom.h>
#include <dc/maple.h>
#include <kos.h>
#include <kos/thread.h>

#include <openmenu_settings.h>

#include "backend/gdemu_sdk.h"
#include "backend/gdmenu_binary.h"
#include "bloader.h"
#include "history.h"
#include "launch.h"
#include "sound.h"

/* Wait for the GDEMU to present the newly selected image. */
static void
wait_cd_ready(const gd_item* disc) {
    /* Audio CDs and other non-game images never pass cdrom_reinit(). */
    if (!strcmp(disc->type, "other")) {
        thd_sleep(100);
        return;
    }

    for (int i = 0; i < 500; i++) {
        if (cdrom_reinit() == ERR_OK) {
            return;
        }
        thd_sleep(20);
    }
}

/* Non-game discs: mount the image, then leave through the BIOS. */
static void
launch_other(const gd_item* disc) {
    gdemu_set_img_num((uint16_t)disc->slot_num);
    wait_cd_ready(disc);

    if (sf_bios_3d[0] == BIOS_3D_STANDARD) {
        arch_menu();
    }

    bloader_cfg_t* cfg = (bloader_cfg_t*)&bloader_data[bloader_size - sizeof(bloader_cfg_t)];
    cfg->enable_wide = 0;

    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (cont && !strncmp("Dreamcast Fishing Controller", cont->info.product_name, 28)) {
        cfg->enable_3d = 0;
    } else {
        cfg->enable_3d = (sf_bios_3d[0] == BIOS_3D_ALTERNATE_3D) ? 1 : 0;
    }

    arch_exec_at(bloader_data, bloader_size, 0xacf00000);
}

void
launch_disc(const gd_item* disc) {
    if (!disc) {
        return;
    }

    history_record(disc);
    sound_shutdown(); /* the next program must not inherit a running sound driver */

    if (!strcmp(disc->type, "other")) {
        launch_other(disc);
        return;
    }

    ldr_params_t param;
    memset(&param, 0, sizeof(param));
    param.region_free = 1;
    param.force_vga = 1;
    param.IGR = 1;
    param.boot_intro = (sf_boot_mode[0] == BOOT_MODE_FULL || sf_boot_mode[0] == BOOT_MODE_ANIMATION) ? 1 : 0;
    param.sega_license = (sf_boot_mode[0] == BOOT_MODE_FULL || sf_boot_mode[0] == BOOT_MODE_LICENSE) ? 1 : 0;

    if (!strncmp(disc->region, "JUE", 3)) {
        param.game_region = (int)(((uint8_t*)0x8C000072)[0] & 7);
    } else {
        switch (disc->region[0]) {
            case 'J': param.game_region = 0; break;
            case 'U': param.game_region = 1; break;
            case 'E': param.game_region = 2; break;
            default: param.game_region = (int)(((uint8_t*)0x8C000072)[0] & 7); break;
        }
    }

    gdemu_set_img_num((uint16_t)disc->slot_num);
    wait_cd_ready(disc);

    int status = 0, disc_type = 0;
    cdrom_get_status(&status, &disc_type);
    param.disc_type = disc_type == CD_GDROM;

    param.need_game_fix = (!strncmp(disc->name, "PSO VER.2", 9) || !strncmp(disc->name, "SONIC ADVENTURE 2", 18)) ? 1 : 0;

    /* BIOS revision specific patch of the boot ROM's disc check. */
    if (!strncmp((char*)0x8c0007CC, "1.004", 5)) {
        ((uint32_t*)0xAC000E20)[0] = 0;
    } else if (!strncmp((char*)0x8c0007CC, "1.01d", 5) || !strncmp((char*)0x8c0007CC, "1.01c", 5)) {
        ((uint32_t*)0xAC000E1C)[0] = 0;
    }

    ((uint32_t*)0xAC0000E4)[0] = -3;

    memcpy((void*)0xACCFFF00, &param, 32);

    arch_exec(gdmenu_loader, gdmenu_loader_length);
}
