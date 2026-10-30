/* iblue_vmu: see iblue_vmu.h. */
#include <string.h>

#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <kos/thread.h>

#include "iblue_vmu.h"

static maple_device_t* dev;

static int
vmu_read(void* ctx, int block, uint8_t* buf) {
    (void)ctx;
    if (!dev) {
        memset(buf, 0, IBLUE_BLOCK_SIZE);
        return 0;
    }
    return vmu_block_read(dev, (uint16_t)block, buf) == MAPLE_EOK ? 0 : -1;
}

static int
vmu_write(void* ctx, int block, const uint8_t* buf) {
    (void)ctx;
    return vmu_block_write(dev, (uint16_t)block, (uint8_t*)buf) == MAPLE_EOK ? 0 : -1;
}

static void
vmu_sleep(void* ctx, int ms) {
    (void)ctx;
    thd_sleep(ms);
}

void
iblue_vmu_io(iblue_io* io) {
    uint8_t buf[IBLUE_BLOCK_SIZE];

    dev = NULL;
    for (int port = 0; port < IBLUE_PORTS && !dev; port++) {
        for (int unit = 1; unit <= 2 && !dev; unit++) {
            maple_device_t* d = maple_enum_dev(port, unit);
            if (d && (d->info.functions & MAPLE_FUNC_MEMCARD) &&
                vmu_block_read(d, IBLUE_BLOCK_FIRST, buf) == MAPLE_EOK && memcmp(buf, "iBLU", 4) == 0) {
                dev = d;
            }
        }
    }
    io->ctx = NULL;
    io->read = vmu_read;
    io->write = vmu_write;
    io->sleep_ms = vmu_sleep;
}
