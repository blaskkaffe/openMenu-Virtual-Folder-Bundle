/*
 * File: launch.c
 * Mount a GDEMU disc image and hand control to the game.
 *
 * The launch path of the full openMenu (1.7.1): Dreamcast games through the loader, other discs through the BIOS,
 * PlayStation discs through Bleem! or Bloom from the menu disc, and the game's ID sent to a VM2, VMU Pro, USB4Maple or
 * Pico2Maple as its Settings row says. Not here yet: the Serial VMU restore before a game.
 */
#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include <arch/arch.h>
#include <dc/cdrom.h>
#include <dc/maple.h>
#include <kos.h>
#include <kos/thread.h>

#include <openmenu_settings.h>

#include "backend/controls.p1.h"
#include "backend/gdemu_sdk.h"
#include "backend/gdmenu_binary.h"
#include "bloader.h"
#include "history.h"
#include "launch.h"
#include "online.h"
#include "sound.h"
#include "vm2/vm2_api.h"

/* VM2 and friends: the memory cards that switch to a game's own card when they are told its ID (openMenu 1.7.1's
 * vm2_rescan / vm2_send_id_to_all, Settings > VM2 game ID). */
#define VM2_MAX_DEVICES 8

static void
vm2_send_game_id(const gd_item* disc) {
    maple_device_t* found[VM2_MAX_DEVICES];
    int n = 0;
    if (sf_vm2_send_all[0] == VM2_SEND_OFF) {
        return;
    }
    for (int i = 0; i < 8 && n < VM2_MAX_DEVICES; i++) {
        maple_device_t* vmu = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
        if (vmu && vmu->valid && check_vm2_present(vmu)) {
            found[n++] = vmu;
        }
    }
    for (int i = 0; i < n; i++) {
        vm2_set_id(found[i], disc->product, disc->name);
        if (sf_vm2_send_all[0] == VM2_SEND_FIRST) {
            break;
        }
    }
}

/* ---- PlayStation discs: Bleem! (BLEEM.BIN) or Bloom (BLOOM.BIN) from the menu disc, as openMenu 1.7.1 starts them */
static int psx_use_bloom;

int
launch_bloom_available(void) {
    static int known = -1;
    if (known < 0) {
        file_t f = fs_open("/cd/BLOOM.BIN", O_RDONLY);
        known = f != -1;
        if (known) {
            fs_close(f);
        }
    }
    return known;
}

void
launch_set_psx_emulator(int bloom) {
    psx_use_bloom = bloom && launch_bloom_available();
}

int
launch_psx_emulator(void) {
    return psx_use_bloom;
}

/* The emulator read into a 32 byte aligned buffer; NULL if it is not on the menu disc. */
static uint8_t*
load_emulator(const char* path, uint32_t* size) {
    file_t fd = fs_open(path, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }
    *size = (uint32_t)fs_total(fd);
    uint8_t* buf = *size ? memalign(32, *size) : NULL;
    if (buf && fs_read(fd, buf, *size) != (ssize_t)*size) {
        free(buf);
        buf = NULL;
    }
    fs_close(fd);
    return buf;
}

/* Wait for the GDEMU to present the newly selected image. 0 when it is ready, -1 after ten seconds without. */
static int
wait_cd_ready(const gd_item* disc) {
    /* Audio CDs and other non-game images never pass cdrom_reinit(). */
    if (!strcmp(disc->type, "other")) {
        thd_sleep(100);
        return 0;
    }

    for (int i = 0; i < 500; i++) {
        if (cdrom_reinit() == ERR_OK) {
            return 0;
        }
        thd_sleep(20);
    }
    return -1;
}

/* Put the disc's image in the GDEMU and wait until it is there. 0 on success. */
static int
select_image(const gd_item* disc) {
    if (gdemu_set_img_num((uint16_t)disc->slot_num) != 0) {
        return -1;
    }
    return wait_cd_ready(disc);
}

/* Non-game discs (their image already selected): leave through the BIOS. */
static void
launch_other(void) {
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

    int psx = !strcmp(disc->type, "psx");
    uint8_t* emu = NULL;
    uint32_t emu_size = 0;
    if (psx) { /* the emulator first: without it there is nothing to start */
        emu = load_emulator(psx_use_bloom ? "/cd/BLOOM.BIN" : "/cd/BLEEM.BIN", &emu_size);
        if (!emu) {
            return;
        }
    }

    /* the image first: if it cannot be selected or does not come up, the caller says so (no entry in the recently
     * played list, the menu's sound still running; an online connection has been let go) */
    online_shutdown(); /* as the main branch: the modem or adapter is let go before the image changes */
    if (select_image(disc) != 0) {
        free(emu);
        return;
    }
    history_record(disc);
    sound_shutdown(); /* the next program must not inherit a running sound driver */

    if (psx) {
        ((uint16_t*)0xAC000198)[0] = 0xFF86; /* as 1.7.1 */
        if (!psx_use_bloom && emu_size > 0x7079C + (uint32_t)altctrl_size && emu_size > 0x1CA70) {
            for (int i = 0; i < altctrl_size; i++) {
                emu[i + 0x7079C] = altctrl_data[i]; /* Bleem!'s alternative controls */
            }
            emu[0x49E6] = 0x06; /* restart the emulator: A+B+X+Y+down */
            emu[0x49E7] = 0x0E; /* back to the menu: A+B+X+Y+Start */
            emu[0x1CA70] = 1;
        }
        arch_exec(emu, emu_size);
        return;
    }
    if (strcmp(disc->type, "other") != 0) {
        vm2_send_game_id(disc);
    }

    if (!strcmp(disc->type, "other")) {
        launch_other();
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
