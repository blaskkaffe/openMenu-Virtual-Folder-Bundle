/*
 * sound: plays the BIOS menu sounds, see sound.h.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/spu.h>
#include <kos/thread.h>

#include <bios_audio.h>

#include "sound.h"

#define DRIVER_START_WAIT_MS 250
#define UI_PLAYER 0
#define UI_PRIORITY 4

static int ready;
static char status[40] = "sound off";

/* spu_memload wants word aligned, word sized data: stage through the heap. */
static int
load_to_spu(uint32_t dst, const uint8_t* src, size_t len) {
    size_t padded = (len + 3) & ~(size_t)3;
    uint8_t* tmp = (uint8_t*)memalign(32, padded ? padded : 4);
    if (!tmp) {
        return -1;
    }
    memset(tmp, 0, padded);
    memcpy(tmp, src, len);
    spu_memload(dst, tmp, padded);
    free(tmp);
    return 0;
}

static void
send_command(const uint8_t slot[16]) {
    uint32_t flag = 0;
    spu_memread(&flag, BAUDIO_STATUS, 4);
    if (flag & 0xFF) {
        return; /* driver still has a command pending: drop this one */
    }
    uint8_t copy[16];
    memcpy(copy, slot, 16);
    spu_memload(BAUDIO_CMD_BUFFER, copy, 16);
    flag = (flag & ~0xFFu) | 1u; /* keep the other status bytes */
    spu_memload(BAUDIO_STATUS, &flag, 4);
}

int
sound_init(const bios_rom* rom) {
    const uint8_t *driver, *banks;
    size_t driver_size, banks_size;

    ready = 0;
    if (!rom || bios_sound_get(rom, BIOS_SOUND_DRIVER, &driver, &driver_size) != 0
        || bios_sound_get(rom, BIOS_SOUND_BANKS, &banks, &banks_size) != 0
        || driver_size < BAUDIO_DRIVER_CODE_OFFSET + BAUDIO_DRIVER_CODE_SIZE) {
        snprintf(status, sizeof(status), "sound: no ROM data");
        return -1;
    }

    baudio_block blocks[BAUDIO_MAX_BLOCKS];
    int count = baudio_parse_banks(banks, banks_size, blocks, BAUDIO_MAX_BLOCKS);
    if (count == 0) {
        snprintf(status, sizeof(status), "sound: no banks");
        return -1;
    }

    /* 1. hold the ARM in reset, 2. copy the driver to sound RAM 0, 3. let it run */
    spu_disable();
    if (load_to_spu(0, driver + BAUDIO_DRIVER_CODE_OFFSET, BAUDIO_DRIVER_CODE_SIZE) != 0) {
        snprintf(status, sizeof(status), "sound: out of memory");
        return -1;
    }
    spu_enable();
    thd_sleep(DRIVER_START_WAIT_MS);

    /* 4. banks into sound RAM, announced in the data map */
    for (int i = 0; i < count; i++) {
        if (blocks[i].size && load_to_spu(blocks[i].ram_addr, banks + blocks[i].offset, blocks[i].size) != 0) {
            snprintf(status, sizeof(status), "sound: out of memory");
            return -1;
        }
        uint32_t map = baudio_datamap_addr(&blocks[i]);
        if (map) {
            uint32_t words[2];
            baudio_datamap_entry(&blocks[i], words);
            spu_memload(map, words, 8);
        }
    }

    ready = 1;
    uint8_t slot[16];
    baudio_cmd_master_volume(slot, 15);
    send_command(slot);
    snprintf(status, sizeof(status), "sound ok (%d banks)", count);
    return 0;
}

void
sound_sfx(int sfx) {
    if (!ready) {
        return;
    }
    uint8_t slot[16];
    baudio_cmd_play(slot, UI_PLAYER, 0, sfx, UI_PRIORITY);
    send_command(slot);
}

void
sound_stereo(int mono) {
    if (!ready) {
        return;
    }
    uint8_t slot[16];
    baudio_cmd_stereo(slot, mono);
    send_command(slot);
}

const char*
sound_status(void) {
    return status;
}

void
sound_shutdown(void) {
    if (ready) {
        spu_disable();
        ready = 0;
    }
}
