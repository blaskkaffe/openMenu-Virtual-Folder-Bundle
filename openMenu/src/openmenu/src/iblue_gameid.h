/*
 * iblue_gameid: tell an iBlueControlMod which game is about to start. The adapter makes the product
 * number BlueRetro's game id, so blueretro.io can keep a controller configuration per game and the
 * adapter loads it when the game starts. Layout of the block and the rules: iBlueControlMod
 * `main/system/iblue_gameid.h`.
 *
 * Portable C: the two block calls come in an iblue_gameid_io (iblue_gameid_vmu.c on the console, a
 * fake adapter in the host test).
 */
#pragma once

#include <stdint.h>

#define IBLUE_GAMEID_BLOCK 200
#define IBLUE_GAMEID_BLOCK_SIZE 512

typedef struct iblue_gameid_io {
    void* ctx;
    int (*read)(void* ctx, int block, uint8_t* buf);        /* 512 bytes, 0 = ok */
    int (*write)(void* ctx, int block, const uint8_t* buf); /* 512 bytes, 0 = ok */
} iblue_gameid_io;

/* Build the block that sets `product` as the game ("" = no game: the normal configuration). Letters,
 * digits, - and _ are kept, anything else becomes _ and trailing blanks are dropped. */
void iblue_gameid_block(uint8_t out[IBLUE_GAMEID_BLOCK_SIZE], uint32_t seq, const char* product);

/* 1 if this is what an adapter with the feature answers when the block is read. */
int iblue_gameid_present(const uint8_t block[IBLUE_GAMEID_BLOCK_SIZE]);

/* Send the id and read it back; up to 3 tries (a Maple frame can be lost). Returns 0 when the adapter
 * shows the id and sequence number it was sent, -1 if there is no such adapter, -2 if it did not take it. */
int iblue_gameid_send(const iblue_gameid_io* io, uint32_t seq, const char* product);
