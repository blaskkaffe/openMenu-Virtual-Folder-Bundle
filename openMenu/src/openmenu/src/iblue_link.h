/*
 * iblue_link: the iBlueControlMod's settings as the Controllers screen sees them, on top of the
 * brctl control channel (brctl/, the firmware's Maple bus protocol). Portable C: the three brctl
 * calls it needs come in an iblue_io (iblue_brctl.c on the console, a fake board in the host test).
 *
 * The link keeps a working copy of the adapter's whole configuration. Edits are written to the
 * adapter at once (so the player can try them) and stay unsaved until iblue_link_commit(); revert
 * writes the copy from the last save/load back. Every call returns 0, a negative IBLUE_ERR_* (the
 * link) or a positive IBLUE_RES_* (the adapter said no).
 */
#pragma once

#include <stdint.h>

#include "brctl/brctl_config.h"
#include "brctl/brctl_proto.h"

#define IBLUE_PORTS BRCTL_PORTS
#define IBLUE_MAP_MAX BR_MAPPING_MAX
#define IBLUE_NAME_LEN 24
#define IBLUE_PROFILES BRCTL_PROFILES
#define IBLUE_GAME_ID_MAX BRCTL_GAME_ID_MAX
/* A destination no Dreamcast output uses: the source does nothing */
#define IBLUE_DST_NONE 0x40

#define IBLUE_ERR_IO -1       /* the adapter did not answer */
#define IBLUE_ERR_NO_BOARD -2 /* no iBlue found (or no Bluetooth controller connected to make it appear) */
#define IBLUE_ERR_TIMEOUT -3
#define IBLUE_ERR_PROTO -4    /* wrong protocol version or config layout */

#define IBLUE_RES_BAD_ARG 2   /* refused; the adapter went back to its saved configuration */
#define IBLUE_RES_FAILED 5
#define IBLUE_RES_EMPTY 6     /* that profile slot is empty */
#define IBLUE_RES_NO_GAME 7

/* Three brctl calls. read/write return 0 or IBLUE_ERR_*; op waits until the adapter has done it and
 * returns the adapter's BRCTL_RES_* in *result. */
typedef struct iblue_io {
    void* ctx;
    int (*read)(void* ctx, uint8_t item, uint32_t offset, void* buf, uint32_t len);
    int (*write)(void* ctx, uint8_t item, uint32_t offset, const void* buf, uint32_t len);
    int (*op)(void* ctx, uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t arg2, int* result);
} iblue_io;

typedef struct br_map_cfg iblue_map_entry;

typedef struct iblue_port_info {
    uint8_t dev_mode; /* 0 controller, 1 dual stick (alt), 2 keyboard, 3 mouse */
    uint8_t acc_mode; /* bit0 VMU, bit1 rumble pack */
    uint8_t connected; /* a Bluetooth controller is assigned to the port */
    uint8_t plugged;   /* the board senses a physical controller in the port */
    uint16_t map_size;
    char name[IBLUE_NAME_LEN];
} iblue_port_info;

typedef struct iblue_profile {
    uint8_t used, dev_mode, acc_mode;
} iblue_profile;

typedef struct iblue_link {
    iblue_io io;
    int unsaved;           /* the adapter has edits that are not saved */
    struct br_config cfg;  /* working copy: what the adapter runs now */
    struct br_config orig; /* what is saved */
} iblue_link;

/* Find the adapter and read its configuration. */
int iblue_link_open(iblue_link* l, const iblue_io* io);

int iblue_link_port_info(iblue_link* l, int port, iblue_port_info* out);
/* The mapping table of a port; *size gets the number of entries. */
int iblue_link_get_map(iblue_link* l, int port, iblue_map_entry out[IBLUE_MAP_MAX], int* size);
/* Writes entries [first, first+count); new_size -1 keeps the size. */
int iblue_link_set_map(iblue_link* l, int port, int first, int count, const iblue_map_entry* in, int new_size);
int iblue_link_set_type(iblue_link* l, int port, int dev_mode, int acc_mode);
int iblue_link_reset_map(iblue_link* l, int port);
int iblue_link_commit(iblue_link* l);
int iblue_link_revert(iblue_link* l);
/* Source buttons held on the controller of the port (bit = generic id, 32 of them). */
int iblue_link_live(iblue_link* l, int port, uint32_t words[4], int* connected);

/* Pairing mode: 0 auto (looks for controllers by itself), 1 manual (only when asked). */
int iblue_link_get_pairing(iblue_link* l, int* mode);
int iblue_link_set_pairing(iblue_link* l, int mode);

/* Profiles: the type and mapping of one port saved in the adapter (slot 0..3). On the controller,
 * Home plus a face button (down, right, left, up) loads slot 0..3 for the controller it is pressed on. */
int iblue_link_profiles(iblue_link* l, iblue_profile out[IBLUE_PROFILES]);
int iblue_link_profile_save(iblue_link* l, int slot, int port);
int iblue_link_profile_load(iblue_link* l, int slot, int port); /* unsaved afterwards; IBLUE_RES_EMPTY if empty */
int iblue_link_profile_delete(iblue_link* l, int slot);

/* Games: the adapter switches to a game's own settings when it has them. id "" detaches. This reloads
 * the working copy (unsaved edits are dropped). */
int iblue_link_game_set(iblue_link* l, const char* id, int* has_own);
int iblue_link_game_save(iblue_link* l);   /* the current settings become the game's */
int iblue_link_game_delete(iblue_link* l); /* the game uses the normal settings again */

/* Before a game starts, without a link: the adapter uses the game's own settings if it has some.
 * 0 or an error; does nothing harmful without an adapter. */
int iblue_game_apply(const iblue_io* io, const char* product);

/* The default table of a port (what a reset or a fresh adapter has). */
void iblue_default_in_cfg(struct br_in_cfg* in, int port);
