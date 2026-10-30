/* iblue_gameid_vmu: see iblue_gameid_vmu.h. */
#include <string.h>

#include <arch/timer.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include "iblue_gameid.h"
#include "iblue_gameid_vmu.h"

static maple_device_t* adapter; /* the memory card unit that carries the marker */
static int searched;

static int
vmu_read(void* ctx, int block, uint8_t* buf) {
    (void)ctx;
    return vmu_block_read(adapter, (uint16_t)block, buf) == MAPLE_EOK ? 0 : -1;
}

static int
vmu_write(void* ctx, int block, const uint8_t* buf) {
    (void)ctx;
    return vmu_block_write(adapter, (uint16_t)block, (uint8_t*)buf) == MAPLE_EOK ? 0 : -1;
}

static void
find_adapter(void) {
    uint8_t buf[IBLUE_GAMEID_BLOCK_SIZE];
    for (int port = 0; port < 4 && !adapter; port++) {
        for (int unit = 1; unit <= 2 && !adapter; unit++) {
            maple_device_t* d = maple_enum_dev(port, unit);
            if (d && (d->info.functions & MAPLE_FUNC_MEMCARD) &&
                vmu_block_read(d, IBLUE_GAMEID_BLOCK, buf) == MAPLE_EOK && iblue_gameid_present(buf)) {
                adapter = d;
            }
        }
    }
    searched = 1;
}

int
iblue_gameid_set(const char* product) {
    static uint32_t seq;
    iblue_gameid_io io = {NULL, vmu_read, vmu_write};

    if (!searched) {
        find_adapter();
    }
    if (!adapter) {
        return -1;
    }
    /* a number the adapter has not seen: the time in ms, and never 0 */
    seq = (uint32_t)timer_ms_gettime64() + 1u + seq;
    return iblue_gameid_send(&io, seq, product);
}
