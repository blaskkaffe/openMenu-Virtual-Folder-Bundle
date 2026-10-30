/*
 * iBlueControlMod settings channel: wire format and command handler.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable C (no ESP-IDF, no BlueRetro headers) so it is tested on a PC.
 * The board side (iblue_cfg.c) supplies the iblue_cfg_ops. The matching
 * client is openMenu's iblue_link.c. Spec: docs/openmenu-config-channel.md.
 *
 * The channel is a window of 8 blocks of the emulated VMU. The data area
 * starts at byte 24 of the window. All numbers are little endian.
 */

#ifndef _IBLUE_CFG_PROTO_H_
#define _IBLUE_CFG_PROTO_H_

#include <stdint.h>
#include <stddef.h>

#define IBLUE_BLOCK_SIZE   512
#define IBLUE_BLOCK_FIRST  200
#define IBLUE_BLOCK_COUNT  8
#define IBLUE_WINDOW_SIZE  (IBLUE_BLOCK_SIZE * IBLUE_BLOCK_COUNT)
#define IBLUE_HDR_SIZE     24
#define IBLUE_DATA_MAX     (IBLUE_WINDOW_SIZE - IBLUE_HDR_SIZE)
#define IBLUE_VERSION      1

#define IBLUE_PORTS        4
#define IBLUE_MAP_MAX      128
#define IBLUE_MAP_ENTRY    8
#define IBLUE_NAME_LEN     24

/* Raw destination id that no Dreamcast output uses: the source does nothing */
#define IBLUE_DST_NONE     0x40

/* Header offsets */
#define IBLUE_H_MAGIC   0
#define IBLUE_H_VERSION 4
#define IBLUE_H_STATUS  6
#define IBLUE_H_CMD     8
#define IBLUE_H_LEN     10
#define IBLUE_H_SEQ     12
#define IBLUE_H_CRC     16
#define IBLUE_H_RESULT  20

/* Status bits. The client sets PENDING in the request, the board clears it */
#define IBLUE_ST_PENDING  0x0001
#define IBLUE_ST_UNSAVED  0x0002

enum {
    IBLUE_CMD_HELLO     = 0x01,
    IBLUE_CMD_PORT_INFO = 0x02,
    IBLUE_CMD_GET_MAP   = 0x03,
    IBLUE_CMD_SET_MAP   = 0x04,
    IBLUE_CMD_SET_TYPE  = 0x05,
    IBLUE_CMD_RESET_MAP = 0x06,
    IBLUE_CMD_COMMIT    = 0x07,
    IBLUE_CMD_REVERT    = 0x08,
    IBLUE_CMD_LIVE      = 0x09,
    IBLUE_CMD_GET_GLOBAL = 0x0A,
    IBLUE_CMD_SET_GLOBAL = 0x0B,
    IBLUE_CMD_PROFILE_LIST = 0x0C,
    IBLUE_CMD_PROFILE_SAVE = 0x0D,
    IBLUE_CMD_PROFILE_LOAD = 0x0E,
    IBLUE_CMD_PROFILE_DELETE = 0x0F,
    IBLUE_CMD_GAME_SET = 0x10,
    IBLUE_CMD_GAME_INFO = 0x11,
    IBLUE_CMD_GAME_SAVE = 0x12,
    IBLUE_CMD_GAME_DELETE = 0x13,
};

enum {
    IBLUE_RES_OK       = 0,
    IBLUE_RES_BAD_CMD  = 1,
    IBLUE_RES_BAD_ARG  = 2,
    IBLUE_RES_BAD_CRC  = 3,
    IBLUE_RES_LOCKOUT  = 4, /* would remove the VMU of port A: the channel itself */
    IBLUE_RES_FAILED   = 5, /* flash write failed */
    IBLUE_RES_EMPTY    = 6, /* that profile slot has nothing saved */
    IBLUE_RES_NO_GAME  = 7, /* no game is set */
};

#define IBLUE_PROFILES  4
#define IBLUE_GAME_ID_MAX 16 /* characters of a game's product number */

struct iblue_profile_info {
    uint8_t used;
    uint8_t dev_mode;
    uint8_t acc_mode;
};

/* Home plus a face button picks a profile slot for the controller it is pressed on. Generic ids. */
#define IBLUE_HOTKEY_HOME  22
#define IBLUE_HOTKEY_FACES 0x000F0000u /* ids 16..19: left, right, down, up */

struct iblue_map {
    uint8_t src_btn;
    uint8_t dst_btn;
    uint8_t dst_id;
    uint8_t perc_max;
    uint8_t perc_threshold;
    uint8_t perc_deadzone;
    uint8_t turbo;
    uint8_t algo;
};

/* Settings that are not per port. Only what the Dreamcast build uses. */
struct iblue_global {
    uint8_t inquiry_mode; /* 0 auto: look for controllers by itself, 1 manual: only when asked */
};

struct iblue_port {
    uint8_t dev_mode;
    uint8_t acc_mode;
    uint8_t connected; /* a Bluetooth controller is assigned to this port */
    uint8_t plugged;   /* the board senses a physical controller in this port */
    uint16_t map_size;
    uint16_t vid;
    uint16_t pid;
    char name[IBLUE_NAME_LEN];
};

/* What the board offers the handler. ctx is passed back unchanged. */
struct iblue_cfg_ops {
    void (*get_port)(void *ctx, uint8_t port, struct iblue_port *out);
    /* Live (not saved) access to the mapping table of an output port */
    void (*get_map)(void *ctx, uint8_t port, uint16_t first, uint16_t count, struct iblue_map *out);
    void (*set_map)(void *ctx, uint8_t port, uint16_t first, uint16_t count, const struct iblue_map *in,
                    uint16_t new_size);
    void (*set_type)(void *ctx, uint8_t port, uint8_t dev_mode, uint8_t acc_mode);
    void (*reset_map)(void *ctx, uint8_t port);
    int (*commit)(void *ctx);   /* 0 = saved */
    void (*revert)(void *ctx);  /* reload from flash */
    /* Source buttons held right now on the controller of this port: 4 words, bit = generic button id.
     * Returns 1 when a controller is connected. */
    int (*live)(void *ctx, uint8_t port, uint32_t out[4]);
    void (*get_global)(void *ctx, struct iblue_global *out);
    void (*set_global)(void *ctx, const struct iblue_global *in);
    /* Profiles: the type and mapping of one port, kept in flash (4 slots, survive a factory reset) */
    void (*profile_info)(void *ctx, uint8_t slot, struct iblue_profile_info *out);
    int (*profile_save)(void *ctx, uint8_t slot, uint8_t port);   /* 0 ok */
    int (*profile_load)(void *ctx, uint8_t slot, uint8_t port);   /* 0 ok, 1 empty slot */
    int (*profile_delete)(void *ctx, uint8_t slot);
    /* Games: the board switches to the game's own configuration when it has one.
     * id "" detaches (back to the normal configuration). Returns 1 if the game has its own. */
    int (*game_set)(void *ctx, const char *id);
    /* id out: the current game ("" none); returns bit0 game set, bit1 has own file, bit2 own file in use */
    int (*game_info)(void *ctx, char id[IBLUE_GAME_ID_MAX + 1]);
    int (*game_save)(void *ctx);   /* 0 ok, 1 no game set, -1 failed */
    int (*game_delete)(void *ctx); /* back to the normal configuration */
};

/* Home + face button filter, per controller. Feed it the held generic buttons (word 0) of every
 * report: while Home is held the face buttons are taken out of *btns (the game does not see
 * them) and a newly pressed face returns its slot 0..3, else -1. */
struct iblue_hotkey {
    uint32_t prev_faces;
};
int iblue_hotkey_filter(struct iblue_hotkey *st, uint32_t *btns);

struct iblue_cfg {
    const struct iblue_cfg_ops *ops;
    void *ctx;
    uint16_t status;
};

uint32_t iblue_crc32(const uint8_t *data, size_t len);

/* Run the request in window[] (IBLUE_WINDOW_SIZE bytes) and write the answer into it.
 * Does nothing when the header is not a request (no magic, PENDING clear). */
void iblue_cfg_run(struct iblue_cfg *cfg, uint8_t *window);

/* Fill an idle window: magic, version, no request. */
void iblue_cfg_window_init(uint8_t *window);

#endif /* _IBLUE_CFG_PROTO_H_ */
