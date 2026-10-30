/*
 * iBlueControlMod settings channel, the part openMenu needs: block numbers, header layout,
 * commands. The board side is iBlueControlMod/main/system/iblue_cfg_proto.h (same numbers;
 * test/fw has a copy that the host test checks this file against).
 */
#pragma once

#include <stdint.h>

#define IBLUE_BLOCK_SIZE 512
#define IBLUE_BLOCK_FIRST 200
#define IBLUE_BLOCK_COUNT 8
#define IBLUE_WINDOW_SIZE (IBLUE_BLOCK_SIZE * IBLUE_BLOCK_COUNT)
#define IBLUE_HDR_SIZE 24
#define IBLUE_DATA_MAX (IBLUE_WINDOW_SIZE - IBLUE_HDR_SIZE)
#define IBLUE_VERSION 1

#define IBLUE_PORTS 4
#define IBLUE_MAP_MAX 128
#define IBLUE_MAP_ENTRY 8
#define IBLUE_NAME_LEN 24
#define IBLUE_DST_NONE 0x40

#define IBLUE_H_MAGIC 0
#define IBLUE_H_VERSION 4
#define IBLUE_H_STATUS 6
#define IBLUE_H_CMD 8
#define IBLUE_H_LEN 10
#define IBLUE_H_SEQ 12
#define IBLUE_H_CRC 16
#define IBLUE_H_RESULT 20

#define IBLUE_ST_PENDING 0x0001
#define IBLUE_ST_UNSAVED 0x0002

enum {
    IBLUE_CMD_HELLO = 0x01,
    IBLUE_CMD_PORT_INFO = 0x02,
    IBLUE_CMD_GET_MAP = 0x03,
    IBLUE_CMD_SET_MAP = 0x04,
    IBLUE_CMD_SET_TYPE = 0x05,
    IBLUE_CMD_RESET_MAP = 0x06,
    IBLUE_CMD_COMMIT = 0x07,
    IBLUE_CMD_REVERT = 0x08,
    IBLUE_CMD_LIVE = 0x09,
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
    IBLUE_RES_OK = 0,
    IBLUE_RES_BAD_CMD = 1,
    IBLUE_RES_BAD_ARG = 2,
    IBLUE_RES_BAD_CRC = 3,
    IBLUE_RES_LOCKOUT = 4,
    IBLUE_RES_FAILED = 5,
    IBLUE_RES_EMPTY = 6,
    IBLUE_RES_NO_GAME = 7,
};

#define IBLUE_PROFILES 4
#define IBLUE_GAME_ID_MAX 16
