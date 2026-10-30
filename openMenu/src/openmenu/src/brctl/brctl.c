/*
 * brctl - talk to a BlueRetro adapter from Dreamcast software (KallistiOS)
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <dc/maple.h>
#include <kos/genwait.h>
#include <kos/thread.h>
#include "brctl.h"

/* genwait_wait() lost its callback argument in KOS 2.1 */
#if defined(__has_include)
#  if __has_include(<kos/version.h>)
#    include <kos/version.h>
#  endif
#endif
#if defined(KOS_VERSION_MAJOR) && \
    (KOS_VERSION_MAJOR > 2 || (KOS_VERSION_MAJOR == 2 && KOS_VERSION_MINOR >= 1))
#define BRCTL_GENWAIT(obj, msg, ms) genwait_wait((obj), (msg), (ms))
#else
#define BRCTL_GENWAIT(obj, msg, ms) genwait_wait((obj), (msg), (ms), NULL)
#endif

#define BRCTL_TIMEOUT_MS 100
#define BRCTL_RETRIES 3
#define BRCTL_MAX_WORDS (2 + BRCTL_MAX_XFER / 4)

/* The Maple bus sends each 32-bit word MSB first: the adapter sees every word
 * byte-reversed. Swap every payload word so both sides share one byte order. */
static inline uint32_t sw(uint32_t v) {
    return __builtin_bswap32(v);
}

static void brctl_cb(maple_state_t *st, maple_frame_t *frm) {
    (void)st;
    genwait_wake_all(frm);
}

/* One Maple frame. words[] / rsp[] are in adapter (logical) order. */
static int xfer(int port, int cmd, const uint32_t *words, int nwords,
                int *rsp_code, uint32_t *rsp, int *rsp_words, int rsp_max) {
    maple_device_t *dev = maple_enum_dev(port, 0);
    maple_response_t *resp;
    int i, n;

    if (!dev || !dev->valid) {
        return BRCTL_ENODEV;
    }

    /* KOS < 2.1 returns -1 while the frame is busy, newer KOS waits */
    while (maple_frame_lock(&dev->frame) < 0) {
        thd_pass();
    }

    maple_frame_init(&dev->frame);
    for (i = 0; i < nwords; i++) {
        dev->frame.send_buf[i] = sw(words[i]);
    }
    dev->frame.cmd = cmd;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = nwords;
    dev->frame.callback = brctl_cb;
    maple_queue_frame(&dev->frame);

    if (BRCTL_GENWAIT(&dev->frame, "brctl", BRCTL_TIMEOUT_MS) < 0
            && dev->frame.state != MAPLE_FRAME_RESPONDED) {
        dev->frame.state = MAPLE_FRAME_VACANT;
        return BRCTL_ETIMEOUT;
    }
    if (dev->frame.state != MAPLE_FRAME_RESPONDED) {
        dev->frame.state = MAPLE_FRAME_VACANT;
        return BRCTL_ETIMEOUT;
    }

    resp = (maple_response_t *)dev->frame.recv_buf;
    *rsp_code = resp->response;
    n = resp->data_len;
    if (rsp) {
        const uint32_t *data = (const uint32_t *)resp->data;
        for (i = 0; i < n && i < rsp_max; i++) {
            rsp[i] = sw(data[i]);
        }
    }
    if (rsp_words) {
        *rsp_words = n;
    }
    maple_frame_unlock(&dev->frame);

    if (*rsp_code == MAPLE_RESPONSE_NONE) {
        return BRCTL_ETIMEOUT;
    }
    return BRCTL_OK;
}

static int xfer_retry(int port, int cmd, const uint32_t *words, int nwords,
                      int *rsp_code, uint32_t *rsp, int *rsp_words, int rsp_max, int retries) {
    int ret = BRCTL_ETIMEOUT;

    while (retries-- > 0) {
        ret = xfer(port, cmd, words, nwords, rsp_code, rsp, rsp_words, rsp_max);
        if (ret != BRCTL_ETIMEOUT) {
            break;
        }
    }
    return ret;
}

static int read_chunk(int port, uint32_t param, uint8_t *dst, uint32_t len, int retries) {
    uint32_t req[3] = {BRCTL_FUNC, param, len};
    uint32_t rsp[BRCTL_MAX_WORDS];
    int code, words, ret;
    uint32_t got;

    ret = xfer_retry(port, MAPLE_COMMAND_BREAD, req, 3, &code, rsp, &words, BRCTL_MAX_WORDS, retries);
    if (ret) {
        return ret;
    }
    if (code != MAPLE_RESPONSE_DATATRF || words < 2 || rsp[0] != BRCTL_FUNC || rsp[1] != param) {
        return BRCTL_EPROTO;
    }
    got = (uint32_t)(words - 2) * 4;
    if (got < len) {
        return BRCTL_EREJECT; /* past the end of the item */
    }
    memcpy(dst, &rsp[2], len);
    return BRCTL_OK;
}

int brctl_read(int port, uint8_t item, uint32_t offset, void *buf, uint32_t len) {
    uint8_t *p = buf;

    if (offset & 3) {
        return BRCTL_EREJECT;
    }
    while (len) {
        uint32_t chunk = len > BRCTL_MAX_XFER ? BRCTL_MAX_XFER : len;
        uint32_t req_len = (chunk + 3) & ~3u;
        uint32_t tmp[BRCTL_MAX_XFER / 4];
        int ret = read_chunk(port, BRCTL_PARAM(item, offset), (uint8_t *)tmp,
                             req_len, BRCTL_RETRIES);

        if (ret == BRCTL_EREJECT && req_len != chunk) {
            /* item size not a multiple of 4: retry exact */
            ret = read_chunk(port, BRCTL_PARAM(item, offset), (uint8_t *)tmp, chunk, BRCTL_RETRIES);
        }
        if (ret) {
            return ret;
        }
        memcpy(p, tmp, chunk);
        p += chunk;
        offset += chunk;
        len -= chunk;
    }
    return BRCTL_OK;
}

int brctl_write(int port, uint8_t item, uint32_t offset, const void *buf, uint32_t len) {
    const uint8_t *p = buf;

    if ((offset & 3) || (len & 3)) {
        return BRCTL_EREJECT;
    }
    while (len) {
        uint32_t chunk = len > BRCTL_MAX_XFER ? BRCTL_MAX_XFER : len;
        uint32_t req[BRCTL_MAX_WORDS];
        int code, ret;

        req[0] = BRCTL_FUNC;
        req[1] = BRCTL_PARAM(item, offset);
        memcpy(&req[2], p, chunk);
        ret = xfer_retry(port, MAPLE_COMMAND_BWRITE, req, 2 + chunk / 4, &code, NULL, NULL, 0, BRCTL_RETRIES);
        if (ret) {
            return ret;
        }
        if ((code & 0xFF) == BRCTL_RSP_FILE_ERROR) {
            return BRCTL_EREJECT;
        }
        if (code != MAPLE_RESPONSE_OK) {
            return BRCTL_EPROTO;
        }
        p += chunk;
        offset += chunk;
        len -= chunk;
    }
    return BRCTL_OK;
}

int brctl_op(int port, uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    uint32_t req[5] = {BRCTL_FUNC, op, arg0, arg1, arg2};
    int code;
    int ret = xfer_retry(port, MAPLE_COMMAND_SETCOND, req, 5, &code, NULL, NULL, 0, BRCTL_RETRIES);

    if (ret) {
        return ret;
    }
    return (code == MAPLE_RESPONSE_OK) ? BRCTL_OK : BRCTL_EPROTO;
}

int brctl_op_wait(int port, uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2, int *result) {
    struct brctl_status st;
    uint16_t seq;
    int ret = brctl_get_status(port, &st);

    if (ret) {
        return ret;
    }
    seq = st.op_seq;
    ret = brctl_op(port, op, arg0, arg1, arg2);
    if (ret) {
        return ret;
    }
    for (int i = 0; i < 100; i++) {
        thd_sleep(10);
        ret = brctl_get_status(port, &st);
        if (ret == BRCTL_OK && st.op_seq != seq) {
            if (result) {
                *result = st.op_result;
            }
            return BRCTL_OK;
        }
    }
    return BRCTL_ETIMEOUT;
}

int brctl_get_info(int port, struct brctl_info *info) {
    int ret = brctl_read(port, BRCTL_ITEM_INFO, 0, info, sizeof(*info));

    if (ret) {
        return ret;
    }
    if (info->magic != BRCTL_INFO_MAGIC) {
        return BRCTL_EPROTO;
    }
    info->fw_version[sizeof(info->fw_version) - 1] = 0;
    info->fw_name[sizeof(info->fw_name) - 1] = 0;
    return BRCTL_OK;
}

int brctl_get_status(int port, struct brctl_status *status) {
    int ret = brctl_read(port, BRCTL_ITEM_STATUS, 0, status, sizeof(*status));

    if (ret) {
        return ret;
    }
    return (status->magic == BRCTL_STATUS_MAGIC) ? BRCTL_OK : BRCTL_EPROTO;
}

int brctl_find(void) {
    int port;

    for (port = 0; port < MAPLE_PORT_COUNT; port++) {
        maple_device_t *dev = maple_enum_dev(port, 0);
        struct brctl_info info;

        if (!dev || !dev->valid || !(dev->info.functions & MAPLE_FUNC_CONTROLLER)) {
            continue;
        }
        if (read_chunk(port, BRCTL_PARAM(BRCTL_ITEM_INFO, 0), (uint8_t *)&info,
                       sizeof(info), 1) == BRCTL_OK
                && info.magic == BRCTL_INFO_MAGIC
                && info.proto_ver == BRCTL_PROTO_VER) {
            return port;
        }
    }
    return -1;
}

static int config_layout_ok(int port) {
    struct brctl_info info;
    int ret = brctl_get_info(port, &info);

    if (ret) {
        return ret;
    }
    if (info.config_size != sizeof(struct br_config) || info.config_magic != BR_CONFIG_MAGIC) {
        return BRCTL_EPROTO;
    }
    return BRCTL_OK;
}

int brctl_config_load(int port, struct br_config *cfg) {
    int ret = config_layout_ok(port);

    if (ret) {
        return ret;
    }
    return brctl_read(port, BRCTL_ITEM_CONFIG, 0, cfg, sizeof(*cfg));
}

int brctl_config_save(int port, const struct br_config *cfg) {
    int ret = config_layout_ok(port);

    if (ret) {
        return ret;
    }
    ret = brctl_write(port, BRCTL_ITEM_CONFIG, 0, cfg, sizeof(*cfg));
    if (ret) {
        return ret;
    }
    return brctl_op(port, BRCTL_OP_CONFIG_SAVE, 0, 0, 0);
}
