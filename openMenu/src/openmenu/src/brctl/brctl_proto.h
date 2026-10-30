/*
 * BlueRetro control channel over the Dreamcast Maple bus ("brctl")
 * Shared between the adapter firmware and the Dreamcast client library.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transport
 * ---------
 * Requests are ordinary Maple frames sent to the *main unit* (controller)
 * of a port where BlueRetro presents a device. The first payload word is a
 * private function code that no game uses, so normal traffic is unaffected.
 *
 *   BLOCK_READ  (cmd 11)  [func, param, nbytes]        -> DATA_TX [func, param, data...]
 *   BLOCK_WRITE (cmd 12)  [func, param, data...]       -> ACK (7) or FILE_ERROR (0xFB)
 *   SET_COND    (cmd 14)  [func, op, arg0, arg1, arg2] -> ACK (7)
 *
 *   param = (item << 24) | byte_offset   (offset multiple of 4)
 *
 * Byte order: the Maple bus transmits every 32-bit word most significant
 * byte first, so the Dreamcast sees each word byte-reversed compared to the
 * adapter. The client library swaps every payload word in both directions;
 * after that both sides see identical byte streams and little-endian
 * integers, and the structs below can be used as-is on both sides.
 */

#ifndef _BRCTL_PROTO_H_
#define _BRCTL_PROTO_H_

#include <stdint.h>

#define BRCTL_PROTO_VER 3

/* Function code in adapter (logical) byte order: bytes 'B' 'R' 'T' 'L' */
#define BRCTL_FUNC 0x4C545242u

#define BRCTL_MAX_XFER 512   /* bytes per read/write request */

#define BRCTL_PARAM(item, off) ((((uint32_t)(item)) << 24) | (((uint32_t)(off)) & 0x00FFFFFFu))
#define BRCTL_PARAM_ITEM(p) ((p) >> 24)
#define BRCTL_PARAM_OFF(p) ((p) & 0x00FFFFFFu)

/* Items */
enum {
    BRCTL_ITEM_INFO = 0x01,     /* RO  struct brctl_info */
    BRCTL_ITEM_STATUS = 0x02,   /* RO  struct brctl_status */
    BRCTL_ITEM_CONFIG = 0x03,   /* RW  raw BlueRetro `struct config` (same as blueretro.io) */
    BRCTL_ITEM_PROFILES = 0x04, /* RO  struct brctl_profiles */
    BRCTL_ITEM_GAME = 0x05,     /* RW  struct brctl_game (write the id, then BRCTL_OP_GAME_SET) */
};

/* SET_COND operations (args are little-endian u32) */
enum {
    BRCTL_OP_NOP = 0x00,
    BRCTL_OP_LED_RGB = 0x01,        /* arg0 = 0x00RRGGBB, arg1 bit0 = save as custom colour */
    BRCTL_OP_LED_PRESET = 0x02,     /* arg0 = preset (0 orange, 1 blue, 2 custom) */
    BRCTL_OP_PAIR_START = 0x03,     /* open pairing window and start pairing */
    BRCTL_OP_PAIR_STOP = 0x04,
    BRCTL_OP_DISCONNECT_ALL = 0x05,
    BRCTL_OP_CONFIG_SAVE = 0x06,    /* apply + persist config written via ITEM_CONFIG */
    BRCTL_OP_CONFIG_DEFAULT = 0x07, /* reset config to defaults */
    BRCTL_OP_ADAPTER_RESET = 0x08,  /* restart the adapter */
    BRCTL_OP_BTN_SET = 0x09,        /* arg0 = BRCTL_GEST_*, arg1 = BRCTL_ACT_*, arg2 bit0 = persist */
    BRCTL_OP_BTN_DEFAULTS = 0x0A,   /* restore default button actions, arg0 bit0 = persist */
    BRCTL_OP_PROFILE_SAVE = 0x0B,   /* arg0 = slot 0..3, arg1 = port: keep the port's type + mapping */
    BRCTL_OP_PROFILE_LOAD = 0x0C,   /* arg0 = slot, arg1 = port, arg2 bit0 = persist */
    BRCTL_OP_PROFILE_DELETE = 0x0D, /* arg0 = slot */
    BRCTL_OP_GAME_SET = 0x0E,       /* use the game id in ITEM_GAME: its own config if it has one */
    BRCTL_OP_GAME_SAVE = 0x0F,      /* the live config becomes the game's */
    BRCTL_OP_GAME_DELETE = 0x10,    /* the game uses the normal config again */
};

/* Result of the last finished operation (status.op_result). Operations run later on the
 * adapter; status.op_seq counts finished ones, so wait for it to change. */
enum {
    BRCTL_RES_OK = 0,
    BRCTL_RES_EMPTY = 1,    /* that profile slot has nothing saved */
    BRCTL_RES_NO_GAME = 2,  /* no game id is set */
    BRCTL_RES_BAD = 3,      /* argument or config rejected (the config is reloaded from flash) */
    BRCTL_RES_FAILED = 4,   /* flash write failed */
};

/* Button / touch sensor gestures (status.btn_last, BRCTL_OP_BTN_SET) */
enum {
    BRCTL_GEST_TAP = 0,         /* press < 3 s */
    BRCTL_GEST_DOUBLE_TAP,      /* tap, then a 2nd quick tap within 2 s */
    BRCTL_GEST_TAP_HOLD,        /* tap, then a 2nd press held > 0.6 s */
    BRCTL_GEST_HOLD_3S,         /* released between 3 and 6 s */
    BRCTL_GEST_HOLD_6S,         /* released between 6 and 10 s */
    BRCTL_GEST_HOLD_10S,        /* released after 10 s (but before 30 s) */
    BRCTL_GEST_MAX,
};
/* Holding 30 s ALWAYS factory-resets and returns to the vendor firmware
 * (recovery path), whatever the mapping. */

/* Actions a gesture can trigger. Every gesture is also reported in
 * status.btn_seq / btn_last so Dreamcast software can react itself. */
enum {
    BRCTL_ACT_DEFAULT = 0,      /* built-in behaviour for that gesture */
    BRCTL_ACT_NONE,             /* report only, adapter does nothing */
    BRCTL_ACT_PAIR,             /* open the pairing window + start pairing */
    BRCTL_ACT_PAIR_STOP_OR_DISCONNECT,
    BRCTL_ACT_DISCONNECT,
    BRCTL_ACT_LED_NEXT,         /* next LED preset */
    BRCTL_ACT_LED_ADJUST,       /* colour sweep while held (TAP_HOLD only) */
    BRCTL_ACT_FACTORY_RESET,    /* erase settings, pairings, VMU */
    BRCTL_ACT_ADAPTER_RESET,    /* restart the adapter */
    BRCTL_ACT_MAX,
};

/* Response codes (Maple) */
#define BRCTL_RSP_ACK 0x07
#define BRCTL_RSP_DATA 0x08
#define BRCTL_RSP_FILE_ERROR 0xFB

#define BRCTL_INFO_MAGIC 0x46495242u    /* 'BRIF' */
#define BRCTL_STATUS_MAGIC 0x54535242u  /* 'BRST' */

struct brctl_info {
    uint32_t magic;
    uint16_t proto_ver;
    uint16_t config_size;       /* sizeof(struct config) on the adapter */
    uint32_t config_magic;
    uint8_t config_version;
    uint8_t wired_max_dev;
    uint8_t mapping_max;
    uint8_t bt_max_dev;
    uint8_t features;           /* bit0 RGB LED, bit1 pairing window */
    uint8_t reserved[3];
    char fw_version[32];
    char fw_name[32];
} __attribute__((packed));

#define BRCTL_FEAT_RGB_LED 0x01
#define BRCTL_FEAT_PAIR_WINDOW 0x02
#define BRCTL_FEAT_PROFILES 0x04

#define BRCTL_PORTS 4

struct brctl_status {
    uint32_t magic;
    uint32_t seq;               /* increments on every adapter-side update */
    uint16_t flags;
    uint16_t pair_window_s;     /* seconds left in the pairing window */
    uint8_t led_rgb[3];
    uint8_t led_preset;
    uint8_t port_bt_id[BRCTL_PORTS];      /* 0xFF = no BT controller on this port */
    uint8_t port_bt_type[BRCTL_PORTS];    /* BlueRetro BT_* type */
    uint8_t port_bt_subtype[BRCTL_PORTS];
    uint16_t btn_seq;           /* +1 on every gesture */
    uint8_t btn_last;           /* last BRCTL_GEST_*, 0xFF = none yet */
    uint8_t btn_stage;          /* live: 0 released, 1 pressed, 2 >3 s, 3 >6 s, 4 >10 s */
    uint8_t btn_map[8];         /* current BRCTL_ACT_* per gesture */
    /* since protocol 3 */
    uint16_t op_seq;            /* operations finished so far */
    uint8_t op_result;          /* BRCTL_RES_* of the last one */
    uint8_t plugged_mask;       /* bit n: the board senses a physical controller in port n */
    uint32_t live[BRCTL_PORTS]; /* buttons held on the Bluetooth controller of the port (generic ids 0..31) */
} __attribute__((packed));

#define BRCTL_PROFILES 4
#define BRCTL_PROFILES_MAGIC 0x46505242u    /* 'BRPF' */
#define BRCTL_GAME_ID_MAX 12                /* characters: letters, digits, - and _ */

struct brctl_profiles {
    uint32_t magic;
    uint8_t used[BRCTL_PROFILES];
    uint8_t dev_mode[BRCTL_PROFILES];
    uint8_t acc_mode[BRCTL_PROFILES];
} __attribute__((packed));

struct brctl_game {
    char id[16];                /* NUL terminated, "" = none */
    uint8_t flags;              /* bit0 id set, bit1 the game has its own config, bit2 it is in use */
    uint8_t reserved[3];
} __attribute__((packed));

#define BRCTL_GAME_SET 0x01
#define BRCTL_GAME_HAS_OWN 0x02
#define BRCTL_GAME_IN_USE 0x04

#define BRCTL_ST_PAIRING 0x0001         /* inquiry (pairing) running */
#define BRCTL_ST_PAIR_ALLOWED 0x0002    /* inside the pairing window */
#define BRCTL_ST_CONSOLE_ON 0x0004

#endif /* _BRCTL_PROTO_H_ */
