/* iblue_link: see iblue_link.h. */
#include <string.h>

#include "iblue_link.h"

#define POLL_TRIES 60
#define POLL_MS 10

static uint16_t
rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void
wr32(uint8_t* p, uint32_t v) {
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

static uint32_t
crc32(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    while (len-- > 0) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int
blocks_for(int bytes) {
    return (bytes + IBLUE_BLOCK_SIZE - 1) / IBLUE_BLOCK_SIZE;
}

/* One command. `in` is the argument, `out` receives up to out_max bytes of the answer. */
static int
call(iblue_link* l, int cmd, const uint8_t* in, int in_len, uint8_t* out, int out_max, int* out_len) {
    uint8_t win[IBLUE_WINDOW_SIZE];
    uint8_t hdr[IBLUE_BLOCK_SIZE];
    int blocks, i;
    uint32_t seq = ++l->seq;

    if (in_len < 0 || in_len > IBLUE_DATA_MAX) {
        return IBLUE_ERR_PROTO;
    }
    memset(win, 0, sizeof(win));
    if (in_len) {
        memcpy(win + IBLUE_HDR_SIZE, in, (size_t)in_len);
    }
    memcpy(win + IBLUE_H_MAGIC, "iBLU", 4);
    wr16(win + IBLUE_H_VERSION, IBLUE_VERSION);
    wr16(win + IBLUE_H_STATUS, IBLUE_ST_PENDING);
    wr16(win + IBLUE_H_CMD, (uint16_t)cmd);
    wr16(win + IBLUE_H_LEN, (uint16_t)in_len);
    wr32(win + IBLUE_H_SEQ, seq);
    wr32(win + IBLUE_H_CRC, crc32(win + IBLUE_HDR_SIZE, in_len));

    /* The first block starts the command, so it goes last. */
    blocks = blocks_for(IBLUE_HDR_SIZE + in_len);
    for (i = 1; i < blocks; i++) {
        if (l->io.write(l->io.ctx, IBLUE_BLOCK_FIRST + i, win + i * IBLUE_BLOCK_SIZE)) {
            return IBLUE_ERR_IO;
        }
    }
    if (l->io.write(l->io.ctx, IBLUE_BLOCK_FIRST, win)) {
        return IBLUE_ERR_IO;
    }

    for (i = 0;; i++) {
        uint16_t status;
        if (l->io.read(l->io.ctx, IBLUE_BLOCK_FIRST, hdr)) {
            return IBLUE_ERR_IO;
        }
        status = rd16(hdr + IBLUE_H_STATUS);
        if (memcmp(hdr, "iBLU", 4) == 0 && rd32(hdr + IBLUE_H_SEQ) == seq && !(status & IBLUE_ST_PENDING)) {
            break;
        }
        if (i >= POLL_TRIES) {
            return IBLUE_ERR_TIMEOUT;
        }
        if (l->io.sleep_ms) {
            l->io.sleep_ms(l->io.ctx, POLL_MS);
        }
    }

    {
        uint16_t status = rd16(hdr + IBLUE_H_STATUS);
        uint16_t result = rd16(hdr + IBLUE_H_RESULT);
        int len = rd16(hdr + IBLUE_H_LEN);
        uint8_t ans[IBLUE_WINDOW_SIZE];

        l->unsaved = (status & IBLUE_ST_UNSAVED) != 0;
        if (result != IBLUE_RES_OK) {
            return result;
        }
        if (len > IBLUE_DATA_MAX || len > out_max) {
            return IBLUE_ERR_PROTO;
        }
        memcpy(ans, hdr, IBLUE_BLOCK_SIZE);
        blocks = blocks_for(IBLUE_HDR_SIZE + len);
        for (i = 1; i < blocks; i++) {
            if (l->io.read(l->io.ctx, IBLUE_BLOCK_FIRST + i, ans + i * IBLUE_BLOCK_SIZE)) {
                return IBLUE_ERR_IO;
            }
        }
        if (crc32(ans + IBLUE_HDR_SIZE, len) != rd32(hdr + IBLUE_H_CRC)) {
            return IBLUE_ERR_PROTO;
        }
        if (out && len) {
            memcpy(out, ans + IBLUE_HDR_SIZE, (size_t)len);
        }
        if (out_len) {
            *out_len = len;
        }
    }
    return 0;
}

int
iblue_link_open(iblue_link* l, const iblue_io* io) {
    uint8_t hdr[IBLUE_BLOCK_SIZE];
    uint8_t ans[16];
    int len = 0, r;

    memset(l, 0, sizeof(*l));
    l->io = *io;
    if (io->read(io->ctx, IBLUE_BLOCK_FIRST, hdr)) {
        return IBLUE_ERR_IO;
    }
    if (memcmp(hdr, "iBLU", 4) != 0) {
        return IBLUE_ERR_NO_BOARD;
    }
    l->seq = rd32(hdr + IBLUE_H_SEQ);
    r = call(l, IBLUE_CMD_HELLO, NULL, 0, ans, sizeof(ans), &len);
    if (r) {
        return r;
    }
    if (len < 8 || rd16(ans) != IBLUE_VERSION) {
        return IBLUE_ERR_PROTO;
    }
    l->map_max = rd16(ans + 4);
    return 0;
}

int
iblue_link_port_info(iblue_link* l, int port, iblue_port_info* out) {
    uint8_t arg = (uint8_t)port;
    uint8_t ans[64];
    int len = 0, r = call(l, IBLUE_CMD_PORT_INFO, &arg, 1, ans, sizeof(ans), &len);
    if (r) {
        return r;
    }
    if (len < 10 + IBLUE_NAME_LEN) {
        return IBLUE_ERR_PROTO;
    }
    out->dev_mode = ans[0];
    out->acc_mode = ans[1];
    out->connected = ans[2];
    out->map_size = rd16(ans + 4);
    out->vid = rd16(ans + 6);
    out->pid = rd16(ans + 8);
    memcpy(out->name, ans + 10, IBLUE_NAME_LEN);
    out->name[IBLUE_NAME_LEN - 1] = '\0';
    return 0;
}

int
iblue_link_get_map(iblue_link* l, int port, iblue_map_entry out[IBLUE_MAP_MAX], int* size) {
    iblue_port_info info;
    int total, first = 0, r;

    r = iblue_link_port_info(l, port, &info);
    if (r) {
        return r;
    }
    total = info.map_size > IBLUE_MAP_MAX ? IBLUE_MAP_MAX : info.map_size;
    while (first < total) {
        uint8_t arg[5];
        uint8_t ans[IBLUE_DATA_MAX];
        int count = total - first, len = 0;
        if (count > 100) {
            count = 100;
        }
        arg[0] = (uint8_t)port;
        wr16(arg + 1, (uint16_t)first);
        wr16(arg + 3, (uint16_t)count);
        r = call(l, IBLUE_CMD_GET_MAP, arg, 5, ans, sizeof(ans), &len);
        if (r) {
            return r;
        }
        if (len != count * IBLUE_MAP_ENTRY) {
            return IBLUE_ERR_PROTO;
        }
        memcpy(&out[first], ans, (size_t)len);
        first += count;
    }
    *size = total;
    return 0;
}

int
iblue_link_set_map(iblue_link* l, int port, int first, int count, const iblue_map_entry* in, int new_size) {
    uint8_t arg[7 + 100 * IBLUE_MAP_ENTRY];
    if (count < 0 || count > 100) {
        return IBLUE_ERR_PROTO;
    }
    arg[0] = (uint8_t)port;
    wr16(arg + 1, (uint16_t)first);
    wr16(arg + 3, (uint16_t)count);
    wr16(arg + 5, new_size < 0 ? 0xFFFF : (uint16_t)new_size);
    memcpy(arg + 7, in, (size_t)count * IBLUE_MAP_ENTRY);
    return call(l, IBLUE_CMD_SET_MAP, arg, 7 + count * IBLUE_MAP_ENTRY, NULL, 0, NULL);
}

int
iblue_link_set_type(iblue_link* l, int port, int dev_mode, int acc_mode) {
    uint8_t arg[3] = {(uint8_t)port, (uint8_t)dev_mode, (uint8_t)acc_mode};
    return call(l, IBLUE_CMD_SET_TYPE, arg, 3, NULL, 0, NULL);
}

int
iblue_link_reset_map(iblue_link* l, int port) {
    uint8_t arg = (uint8_t)port;
    return call(l, IBLUE_CMD_RESET_MAP, &arg, 1, NULL, 0, NULL);
}

int
iblue_link_commit(iblue_link* l) {
    return call(l, IBLUE_CMD_COMMIT, NULL, 0, NULL, 0, NULL);
}

int
iblue_link_revert(iblue_link* l) {
    return call(l, IBLUE_CMD_REVERT, NULL, 0, NULL, 0, NULL);
}

int
iblue_link_live(iblue_link* l, int port, uint32_t words[4], int* connected) {
    uint8_t arg = (uint8_t)port;
    uint8_t ans[24];
    int len = 0, r = call(l, IBLUE_CMD_LIVE, &arg, 1, ans, sizeof(ans), &len);
    if (r) {
        return r;
    }
    if (len < 20) {
        return IBLUE_ERR_PROTO;
    }
    *connected = ans[0];
    for (int i = 0; i < 4; i++) {
        words[i] = rd32(ans + 4 + i * 4);
    }
    return 0;
}
