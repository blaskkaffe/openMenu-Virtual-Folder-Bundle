/*
 * iBlueControlMod settings channel: command handler. See iblue_cfg_proto.h.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "iblue_cfg_proto.h"

static const uint8_t magic[4] = {'i', 'B', 'L', 'U'};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

uint32_t iblue_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;

    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
    }
    return ~crc;
}

void iblue_cfg_window_init(uint8_t *window) {
    memset(window, 0, IBLUE_WINDOW_SIZE);
    memcpy(window + IBLUE_H_MAGIC, magic, sizeof(magic));
    wr16(window + IBLUE_H_VERSION, IBLUE_VERSION);
}

/* algo: low nibble is the stick scaling (0 linear .. 5 passthrough), the high nibble is not used here */
int iblue_hotkey_filter(struct iblue_hotkey *st, uint32_t *btns) {
    uint32_t faces = *btns & IBLUE_HOTKEY_FACES;
    uint32_t fresh = faces & ~st->prev_faces;
    int slot = -1;

    st->prev_faces = faces;
    if (!(*btns & (1u << IBLUE_HOTKEY_HOME))) {
        return -1;
    }
    *btns &= ~IBLUE_HOTKEY_FACES;
    /* face down (18) = slot 0, right (17) = 1, left (16) = 2, up (19) = 3: A, B, X, Y */
    if (fresh & (1u << 18)) slot = 0;
    else if (fresh & (1u << 17)) slot = 1;
    else if (fresh & (1u << 16)) slot = 2;
    else if (fresh & (1u << 19)) slot = 3;
    return slot;
}

static int map_ok(const struct iblue_map *m) {
    return m->dst_id < IBLUE_PORTS && m->perc_max <= 100 && m->perc_threshold <= 100 && m->algo <= 5;
}

/* Returns the result code; *out_len is the number of answer bytes written to out[] */
static uint16_t run_cmd(struct iblue_cfg *cfg, uint16_t cmd, const uint8_t *in, uint16_t in_len, uint8_t *out,
                        uint16_t *out_len) {
    const struct iblue_cfg_ops *ops = cfg->ops;
    *out_len = 0;

    switch (cmd) {
        case IBLUE_CMD_HELLO:
            wr16(out, IBLUE_VERSION);
            out[2] = IBLUE_PORTS;
            out[3] = 0;
            wr16(out + 4, IBLUE_MAP_MAX);
            wr16(out + 6, IBLUE_DATA_MAX);
            *out_len = 8;
            return IBLUE_RES_OK;

        case IBLUE_CMD_PORT_INFO: {
            struct iblue_port p;

            if (in_len < 1 || in[0] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            memset(&p, 0, sizeof(p));
            ops->get_port(cfg->ctx, in[0], &p);
            out[0] = p.dev_mode;
            out[1] = p.acc_mode;
            out[2] = p.connected;
            out[3] = p.plugged;
            wr16(out + 4, p.map_size);
            wr16(out + 6, p.vid);
            wr16(out + 8, p.pid);
            memset(out + 10, 0, IBLUE_NAME_LEN);
            memcpy(out + 10, p.name, IBLUE_NAME_LEN - 1);
            *out_len = 10 + IBLUE_NAME_LEN;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_GET_MAP: {
            uint16_t first, count;
            struct iblue_map m[IBLUE_MAP_MAX];

            if (in_len < 5 || in[0] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            first = rd16(in + 1);
            count = rd16(in + 3);
            if (first > IBLUE_MAP_MAX || count > IBLUE_MAP_MAX - first || count * IBLUE_MAP_ENTRY > IBLUE_DATA_MAX) {
                return IBLUE_RES_BAD_ARG;
            }
            ops->get_map(cfg->ctx, in[0], first, count, m);
            for (uint16_t i = 0; i < count; i++) {
                memcpy(out + i * IBLUE_MAP_ENTRY, &m[i], IBLUE_MAP_ENTRY);
            }
            *out_len = count * IBLUE_MAP_ENTRY;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_SET_MAP: {
            /* port, first u16, count u16, new_size u16 (0xFFFF = unchanged), entries */
            uint16_t first, count, new_size;
            struct iblue_map m[IBLUE_MAP_MAX];

            if (in_len < 7 || in[0] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            first = rd16(in + 1);
            count = rd16(in + 3);
            new_size = rd16(in + 5);
            if (first > IBLUE_MAP_MAX || count > IBLUE_MAP_MAX - first || in_len != 7 + count * IBLUE_MAP_ENTRY ||
                    (new_size != 0xFFFF && (new_size > IBLUE_MAP_MAX || new_size < first + count))) {
                return IBLUE_RES_BAD_ARG;
            }
            for (uint16_t i = 0; i < count; i++) {
                memcpy(&m[i], in + 7 + i * IBLUE_MAP_ENTRY, IBLUE_MAP_ENTRY);
                if (!map_ok(&m[i])) {
                    return IBLUE_RES_BAD_ARG;
                }
            }
            ops->set_map(cfg->ctx, in[0], first, count, m, new_size);
            cfg->status |= IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_SET_TYPE:
            if (in_len < 3 || in[0] >= IBLUE_PORTS || in[1] > 3 || in[2] > 3) {
                return IBLUE_RES_BAD_ARG;
            }
            /* Port A has to keep its VMU: the channel lives in it */
            if (in[0] == 0 && !(in[2] & 1)) {
                return IBLUE_RES_LOCKOUT;
            }
            ops->set_type(cfg->ctx, in[0], in[1], in[2]);
            cfg->status |= IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;

        case IBLUE_CMD_RESET_MAP:
            if (in_len < 1 || in[0] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            ops->reset_map(cfg->ctx, in[0]);
            cfg->status |= IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;

        case IBLUE_CMD_COMMIT:
            if (ops->commit(cfg->ctx)) {
                return IBLUE_RES_FAILED;
            }
            cfg->status &= ~IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;

        case IBLUE_CMD_REVERT:
            ops->revert(cfg->ctx);
            cfg->status &= ~IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;

        case IBLUE_CMD_LIVE: {
            uint32_t w[4] = {0};

            if (in_len < 1 || in[0] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            out[0] = (uint8_t)ops->live(cfg->ctx, in[0], w);
            out[1] = out[2] = out[3] = 0;
            for (int i = 0; i < 4; i++) {
                wr32(out + 4 + i * 4, w[i]);
            }
            *out_len = 20;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_GET_GLOBAL: {
            struct iblue_global g;

            memset(&g, 0, sizeof(g));
            ops->get_global(cfg->ctx, &g);
            out[0] = g.inquiry_mode;
            *out_len = 8;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_SET_GLOBAL: {
            struct iblue_global g;

            if (in_len < 1 || in[0] > 1) {
                return IBLUE_RES_BAD_ARG;
            }
            g.inquiry_mode = in[0];
            ops->set_global(cfg->ctx, &g);
            cfg->status |= IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_PROFILE_LIST:
            for (int i = 0; i < IBLUE_PROFILES; i++) {
                struct iblue_profile_info pi;

                memset(&pi, 0, sizeof(pi));
                ops->profile_info(cfg->ctx, (uint8_t)i, &pi);
                out[i * 4] = pi.used;
                out[i * 4 + 1] = pi.dev_mode;
                out[i * 4 + 2] = pi.acc_mode;
                out[i * 4 + 3] = 0;
            }
            *out_len = IBLUE_PROFILES * 4;
            return IBLUE_RES_OK;

        case IBLUE_CMD_PROFILE_SAVE:
            if (in_len < 2 || in[0] >= IBLUE_PROFILES || in[1] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            return ops->profile_save(cfg->ctx, in[0], in[1]) ? IBLUE_RES_FAILED : IBLUE_RES_OK;

        case IBLUE_CMD_PROFILE_LOAD: {
            int r;

            if (in_len < 2 || in[0] >= IBLUE_PROFILES || in[1] >= IBLUE_PORTS) {
                return IBLUE_RES_BAD_ARG;
            }
            r = ops->profile_load(cfg->ctx, in[0], in[1]);
            if (r) {
                return IBLUE_RES_EMPTY;
            }
            cfg->status |= IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_PROFILE_DELETE:
            if (in_len < 1 || in[0] >= IBLUE_PROFILES) {
                return IBLUE_RES_BAD_ARG;
            }
            return ops->profile_delete(cfg->ctx, in[0]) ? IBLUE_RES_FAILED : IBLUE_RES_OK;

        case IBLUE_CMD_GAME_SET: {
            char id[IBLUE_GAME_ID_MAX + 1];

            if (in_len > IBLUE_GAME_ID_MAX) {
                return IBLUE_RES_BAD_ARG;
            }
            for (int i = 0; i < in_len; i++) {
                /* letters, digits, - and _ only: the id becomes part of a file name */
                uint8_t ch = in[i];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_')) {
                    return IBLUE_RES_BAD_ARG;
                }
                id[i] = (char)ch;
            }
            id[in_len] = '\0';
            out[0] = (uint8_t)ops->game_set(cfg->ctx, id);
            *out_len = 1;
            cfg->status &= ~IBLUE_ST_UNSAVED; /* the configuration was reloaded */
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_GAME_INFO: {
            char id[IBLUE_GAME_ID_MAX + 1];
            int flags;

            memset(id, 0, sizeof(id));
            flags = ops->game_info(cfg->ctx, id);
            out[0] = (uint8_t)flags;
            memcpy(out + 4, id, sizeof(id));
            *out_len = 4 + sizeof(id);
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_GAME_SAVE: {
            int r = ops->game_save(cfg->ctx);

            if (r > 0) {
                return IBLUE_RES_NO_GAME;
            }
            if (r < 0) {
                return IBLUE_RES_FAILED;
            }
            cfg->status &= ~IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;
        }

        case IBLUE_CMD_GAME_DELETE:
            if (ops->game_delete(cfg->ctx)) {
                return IBLUE_RES_NO_GAME;
            }
            cfg->status &= ~IBLUE_ST_UNSAVED;
            return IBLUE_RES_OK;
    }
    return IBLUE_RES_BAD_CMD;
}

void iblue_cfg_run(struct iblue_cfg *cfg, uint8_t *window) {
    uint16_t status = rd16(window + IBLUE_H_STATUS);
    uint16_t cmd = rd16(window + IBLUE_H_CMD);
    uint16_t len = rd16(window + IBLUE_H_LEN);
    uint8_t *data = window + IBLUE_HDR_SIZE;
    uint8_t in[IBLUE_DATA_MAX];
    uint16_t out_len = 0, result;

    if (memcmp(window + IBLUE_H_MAGIC, magic, sizeof(magic)) != 0 || !(status & IBLUE_ST_PENDING)) {
        return;
    }

    if (len > IBLUE_DATA_MAX) {
        result = IBLUE_RES_BAD_ARG;
    }
    else if (iblue_crc32(data, len) != rd32(window + IBLUE_H_CRC)) {
        result = IBLUE_RES_BAD_CRC;
    }
    else {
        /* Copy first: the answer is written over the request */
        memcpy(in, data, len);
        memset(data, 0, IBLUE_DATA_MAX);
        result = run_cmd(cfg, cmd, in, len, data, &out_len);
    }
    if (result != IBLUE_RES_OK) {
        memset(data, 0, IBLUE_DATA_MAX);
        out_len = 0;
    }

    wr16(window + IBLUE_H_VERSION, IBLUE_VERSION);
    wr16(window + IBLUE_H_LEN, out_len);
    wr32(window + IBLUE_H_CRC, iblue_crc32(data, out_len));
    wr16(window + IBLUE_H_RESULT, result);
    /* seq and cmd stay as sent: that is the echo */
    wr16(window + IBLUE_H_STATUS, cfg->status); /* PENDING clear = answer ready; written last */
}
