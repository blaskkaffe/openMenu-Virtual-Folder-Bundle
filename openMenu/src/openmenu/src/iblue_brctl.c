/* iblue_brctl: see iblue_brctl.h. */
#include "brctl/brctl.h"
#include "iblue_brctl.h"

static int adapter_port = -2; /* -2 not looked for yet, -1 none */

/* brctl's codes to the link's */
static int
map_err(int r) {
    switch (r) {
        case BRCTL_OK: return 0;
        case BRCTL_ENODEV: return IBLUE_ERR_NO_BOARD;
        case BRCTL_ETIMEOUT: return IBLUE_ERR_TIMEOUT;
        case BRCTL_EREJECT: return IBLUE_RES_BAD_ARG;
        default: return IBLUE_ERR_PROTO;
    }
}

static int
io_read(void* ctx, uint8_t item, uint32_t offset, void* buf, uint32_t len) {
    (void)ctx;
    return adapter_port < 0 ? IBLUE_ERR_NO_BOARD : map_err(brctl_read(adapter_port, item, offset, buf, len));
}

static int
io_write(void* ctx, uint8_t item, uint32_t offset, const void* buf, uint32_t len) {
    (void)ctx;
    return adapter_port < 0 ? IBLUE_ERR_NO_BOARD : map_err(brctl_write(adapter_port, item, offset, buf, len));
}

static int
io_op(void* ctx, uint32_t op, uint32_t a0, uint32_t a1, uint32_t a2, int* result) {
    (void)ctx;
    return adapter_port < 0 ? IBLUE_ERR_NO_BOARD : map_err(brctl_op_wait(adapter_port, op, a0, a1, a2, result));
}

void
iblue_brctl_io(iblue_io* io, int probe) {
    if (probe || adapter_port == -2) {
        adapter_port = brctl_find();
    }
    io->ctx = NULL;
    io->read = io_read;
    io->write = io_write;
    io->op = io_op;
}
