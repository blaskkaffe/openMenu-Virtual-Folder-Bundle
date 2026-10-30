/*
 * BlueRetro configuration layout (CONFIG_VERSION 3), as read/written with
 * BRCTL_ITEM_CONFIG. Identical to the struct blueretro.io edits.
 * Always compare brctl_info.config_size / config_magic before using it.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BRCTL_CONFIG_H_
#define _BRCTL_CONFIG_H_

#include <stdint.h>

#define BR_CONFIG_MAGIC     0xAB89C69Au
#define BR_CONFIG_VERSION   3
#define BR_WIRED_MAX_DEV    12
#define BR_MAPPING_MAX      128

struct br_map_cfg {
    uint8_t src_btn;        /* BR_BTN_* of the Bluetooth controller */
    uint8_t dst_btn;        /* BR_BTN_* on the Dreamcast side */
    uint8_t dst_id;         /* output port index */
    uint8_t perc_max;
    uint8_t perc_threshold;
    uint8_t perc_deadzone;
    uint8_t turbo;
    uint8_t algo;           /* scaling / diagonal algorithm */
} __attribute__((packed));

struct br_global_cfg {
    uint8_t system_cfg;     /* BR_SYS_* (BR_SYS_AUTO = hardcoded build default) */
    uint8_t multitap_cfg;   /* BR_MT_* */
    uint8_t inquiry_mode;   /* BR_INQ_* */
    uint8_t banksel;        /* memory card bank, >= 4 disables storage */
} __attribute__((packed));

struct br_out_cfg {
    uint8_t dev_mode;       /* BR_DEV_* */
    uint8_t acc_mode;       /* BR_ACC_* (VMU / rumble pack) */
} __attribute__((packed));

struct br_in_cfg {
    uint8_t bt_dev_id;
    uint8_t bt_subdev_id;
    uint8_t map_size;
    struct br_map_cfg map_cfg[BR_MAPPING_MAX];
} __attribute__((packed));

struct br_config {
    uint32_t magic;
    struct br_global_cfg global_cfg;
    struct br_out_cfg out_cfg[BR_WIRED_MAX_DEV];
    struct br_in_cfg in_cfg[BR_WIRED_MAX_DEV];
} __attribute__((packed));

_Static_assert(sizeof(struct br_config) == 12356, "BlueRetro config layout changed");

enum {
    BR_SYS_AUTO = 0, BR_SYS_PARALLEL_1P, BR_SYS_PARALLEL_2P, BR_SYS_NES, BR_SYS_PCE,
    BR_SYS_GENESIS, BR_SYS_SNES, BR_SYS_CDI, BR_SYS_CD32, BR_SYS_3DO, BR_SYS_JAGUAR,
    BR_SYS_PSX, BR_SYS_SATURN, BR_SYS_PCFX, BR_SYS_JVS, BR_SYS_N64, BR_SYS_DC,
    BR_SYS_PS2, BR_SYS_GC, BR_SYS_WII_EXT, BR_SYS_VBOY, BR_SYS_PARALLEL_1P_OD,
    BR_SYS_PARALLEL_2P_OD, BR_SYS_SEA_BOARD,
};

enum { BR_DEV_PAD = 0, BR_DEV_PAD_ALT, BR_DEV_KB, BR_DEV_MOUSE };
enum { BR_ACC_NONE = 0, BR_ACC_MEM, BR_ACC_RUMBLE, BR_ACC_BOTH };
enum { BR_MT_NONE = 0, BR_MT_SLOT_1, BR_MT_SLOT_2, BR_MT_DUAL, BR_MT_ALT };
enum { BR_INQ_AUTO = 0, BR_INQ_MANUAL };

/* Bluetooth controller type reported in brctl_status.port_bt_type */
enum { BR_BT_HID_GENERIC = 0, BR_BT_PS3, BR_BT_WII, BR_BT_PS, BR_BT_SW, BR_BT_SW2 };

/* Generic buttons used in br_map_cfg.src_btn / dst_btn */
enum {
    BR_BTN_LX_LEFT = 0, BR_BTN_LX_RIGHT, BR_BTN_LY_DOWN, BR_BTN_LY_UP,
    BR_BTN_RX_LEFT, BR_BTN_RX_RIGHT, BR_BTN_RY_DOWN, BR_BTN_RY_UP,
    BR_BTN_LD_LEFT, BR_BTN_LD_RIGHT, BR_BTN_LD_DOWN, BR_BTN_LD_UP,
    BR_BTN_RD_LEFT, BR_BTN_RD_RIGHT, BR_BTN_RD_DOWN, BR_BTN_RD_UP,
    BR_BTN_RB_LEFT, BR_BTN_RB_RIGHT, BR_BTN_RB_DOWN, BR_BTN_RB_UP,
    BR_BTN_MM, BR_BTN_MS, BR_BTN_MT, BR_BTN_MQ,
    BR_BTN_LM, BR_BTN_LS, BR_BTN_LT, BR_BTN_LJ,
    BR_BTN_RM, BR_BTN_RS, BR_BTN_RT, BR_BTN_RJ,
};

#endif /* _BRCTL_CONFIG_H_ */
