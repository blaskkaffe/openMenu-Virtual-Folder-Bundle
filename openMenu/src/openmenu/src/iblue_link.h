/*
 * iblue_link: talks to the iBlueControlMod's settings channel (a window of 8 blocks in its
 * emulated VMU). Portable C: the block access is supplied by the caller (iblue_vmu.c on the
 * console, a fake board in the host test). Every call is blocking and returns 0, or a negative
 * IBLUE_ERR_* (link problems) or the board's result code as a positive number (IBLUE_RES_*).
 */
#pragma once

#include <stdint.h>

#include "iblue_proto.h"

#define IBLUE_ERR_IO -1       /* block read/write failed */
#define IBLUE_ERR_NO_BOARD -2 /* no magic: not an iBlue, or old firmware */
#define IBLUE_ERR_TIMEOUT -3  /* the board did not answer */
#define IBLUE_ERR_PROTO -4    /* bad answer (crc, length, sequence) */

typedef struct iblue_io {
    void* ctx;
    int (*read)(void* ctx, int block, uint8_t* buf);        /* 512 bytes, 0 = ok */
    int (*write)(void* ctx, int block, const uint8_t* buf); /* 512 bytes, 0 = ok */
    void (*sleep_ms)(void* ctx, int ms);
} iblue_io;

typedef struct iblue_map_entry {
    uint8_t src_btn, dst_btn, dst_id, perc_max, perc_threshold, perc_deadzone, turbo, algo;
} iblue_map_entry;

typedef struct iblue_port_info {
    uint8_t dev_mode; /* 0 controller, 1 controller (alt), 2 keyboard, 3 mouse */
    uint8_t acc_mode; /* 0 none, 1 VMU, 2 rumble pack, 3 both */
    uint8_t connected; /* a Bluetooth controller is assigned to the port */
    uint8_t plugged;   /* the board senses a physical controller in the port */
    uint16_t map_size;
    uint16_t vid, pid;
    char name[IBLUE_NAME_LEN];
} iblue_port_info;

typedef struct iblue_link {
    iblue_io io;
    uint32_t seq;
    int unsaved;  /* the board has changes that are not saved (from the last answer) */
    int map_max;  /* from HELLO */
} iblue_link;

/* Check for the board and say hello. */
int iblue_link_open(iblue_link* l, const iblue_io* io);

int iblue_link_port_info(iblue_link* l, int port, iblue_port_info* out);
/* Reads the whole table of a port; *size gets the number of entries. */
int iblue_link_get_map(iblue_link* l, int port, iblue_map_entry out[IBLUE_MAP_MAX], int* size);
/* Writes entries [first, first+count); new_size -1 keeps the size. Applies at once, not saved. */
int iblue_link_set_map(iblue_link* l, int port, int first, int count, const iblue_map_entry* in, int new_size);
int iblue_link_set_type(iblue_link* l, int port, int dev_mode, int acc_mode);
int iblue_link_reset_map(iblue_link* l, int port);
int iblue_link_commit(iblue_link* l);
int iblue_link_revert(iblue_link* l);
/* Source buttons held on the controller of the port (bit = generic id, word 0 is the pad). */
/* Pairing mode: 0 auto (looks for controllers by itself), 1 manual (only when asked). */
int iblue_link_get_pairing(iblue_link* l, int* mode);
int iblue_link_set_pairing(iblue_link* l, int mode);
int iblue_link_live(iblue_link* l, int port, uint32_t words[4], int* connected);
