/*
 * A fake iBlueControlMod for the host test: the real command handler of the firmware
 * (test/fw/iblue_cfg_proto.c, a copy) over an in-memory mapping table, behind the same
 * block interface the Dreamcast sees (8 blocks, written in 128 byte phases, the board
 * answering a little later like its task does).
 */
#include <stdio.h>
#include <string.h>

#include "fw/iblue_cfg_proto.h"
#include "iblue_fake_board.h"

struct fake_board {
    struct iblue_port port[IBLUE_PORTS], saved_port[IBLUE_PORTS];
    struct iblue_map map[IBLUE_PORTS][IBLUE_MAP_MAX], saved_map[IBLUE_PORTS][IBLUE_MAP_MAX];
    uint8_t connected[IBLUE_PORTS], plugged[IBLUE_PORTS];
    struct iblue_global global, saved_global;
    uint32_t held[IBLUE_PORTS][4];
    int commits, reverts, reads, sleeps, present;
    struct iblue_profile_info prof[IBLUE_PROFILES];
    struct iblue_map prof_map[IBLUE_PROFILES][IBLUE_MAP_MAX];
    char game[IBLUE_GAME_ID_MAX + 1], own_game[IBLUE_GAME_ID_MAX + 1]; /* current, the one game that has settings */
    struct iblue_port own_port[IBLUE_PORTS];
    int game_saves, game_deletes;
};
static struct fake_board board;
static struct iblue_cfg cfg;
static uint8_t window[IBLUE_WINDOW_SIZE];
static int pending, delay;

static void
default_map(int p) {
    static const uint8_t combo[10] = {24, 28, 20, 19, 16, 17, 18, 11, 10, 21};
    int j = 0;
    for (; j < 108; j++) {
        board.map[p][j] = (struct iblue_map){(uint8_t)j, (uint8_t)j, (uint8_t)p, 100, 50, 135, 0, 0};
    }
    for (int k = 0; k < 10; k++, j++) {
        board.map[p][j] = (struct iblue_map){combo[k], (uint8_t)(118 + k), (uint8_t)p, 100, 50, 135, 0, 0};
    }
    board.port[p].map_size = 118;
}

static void
snapshot(void) {
    memcpy(board.saved_port, board.port, sizeof(board.port));
    board.saved_global = board.global;
    memcpy(board.saved_map, board.map, sizeof(board.map));
}

static void o_get_port(void* c, uint8_t p, struct iblue_port* o) {
    (void)c;
    *o = board.port[p];
    o->connected = board.connected[p];
    o->plugged = board.plugged[p];
    strcpy(o->name, board.connected[p] ? "Fake Pad" : "");
}
static void o_get_map(void* c, uint8_t p, uint16_t f, uint16_t n, struct iblue_map* o) {
    (void)c;
    memcpy(o, &board.map[p][f], n * sizeof(*o));
}
static void o_set_map(void* c, uint8_t p, uint16_t f, uint16_t n, const struct iblue_map* i, uint16_t ns) {
    (void)c;
    memcpy(&board.map[p][f], i, n * sizeof(*i));
    if (ns != 0xFFFF) board.port[p].map_size = ns;
}
static void o_set_type(void* c, uint8_t p, uint8_t d, uint8_t a) {
    (void)c;
    board.port[p].dev_mode = d;
    board.port[p].acc_mode = a;
}
static void o_reset(void* c, uint8_t p) { (void)c; default_map(p); }
static int o_commit(void* c) { (void)c; board.commits++; snapshot(); return 0; }
static void o_revert(void* c) {
    (void)c;
    board.reverts++;
    memcpy(board.port, board.saved_port, sizeof(board.port));
    board.global = board.saved_global;
    memcpy(board.map, board.saved_map, sizeof(board.map));
}
static int o_live(void* c, uint8_t p, uint32_t o[4]) {
    (void)c;
    memcpy(o, board.held[p], 16);
    return board.connected[p];
}
static void o_get_global(void* c, struct iblue_global* g) { (void)c; *g = board.global; }
static void o_set_global(void* c, const struct iblue_global* g) { (void)c; board.global = *g; }
static void o_prof_info(void* c, uint8_t s, struct iblue_profile_info* o) { (void)c; *o = board.prof[s]; }
static int o_prof_save(void* c, uint8_t s, uint8_t p) {
    (void)c;
    board.prof[s] = (struct iblue_profile_info){1, board.port[p].dev_mode, board.port[p].acc_mode};
    memcpy(board.prof_map[s], board.map[p], sizeof(board.prof_map[s]));
    return 0;
}
static int o_prof_load(void* c, uint8_t s, uint8_t p) {
    (void)c;
    if (!board.prof[s].used) return 1;
    board.port[p].dev_mode = board.prof[s].dev_mode;
    board.port[p].acc_mode = board.prof[s].acc_mode;
    memcpy(board.map[p], board.prof_map[s], sizeof(board.map[p]));
    for (int i = 0; i < 128; i++) board.map[p][i].dst_id = p;
    return 0;
}
static int o_prof_del(void* c, uint8_t s) { (void)c; board.prof[s].used = 0; return 0; }
/* A game with settings has its own copy of port B's type; any other game and "" use the normal one */
static int o_game_set(void* c, const char* id) {
    (void)c;
    snprintf(board.game, sizeof(board.game), "%s", id);
    memcpy(board.port, board.saved_port, sizeof(board.port));
    if (id[0] && strcmp(id, board.own_game) == 0) board.port[1] = board.own_port[1];
    return id[0] && strcmp(id, board.own_game) == 0;
}
static int o_game_info(void* c, char id[IBLUE_GAME_ID_MAX + 1]) { (void)c; strcpy(id, board.game); return board.game[0] ? 1 : 0; }
static int o_game_save(void* c) {
    (void)c;
    if (!board.game[0]) return 1;
    snprintf(board.own_game, sizeof(board.own_game), "%s", board.game);
    memcpy(board.own_port, board.port, sizeof(board.port));
    board.game_saves++;
    return 0;
}
static int o_game_del(void* c) {
    (void)c;
    if (!board.game[0]) return 1;
    board.own_game[0] = '\0';
    board.game_deletes++;
    memcpy(board.port, board.saved_port, sizeof(board.port));
    return 0;
}
static const struct iblue_cfg_ops ops = {o_get_port, o_get_map, o_set_map, o_set_type, o_reset, o_commit, o_revert, o_live, o_get_global, o_set_global,
    o_prof_info, o_prof_save, o_prof_load, o_prof_del, o_game_set, o_game_info, o_game_save, o_game_del};

void
board_reset(void) {
    memset(&board, 0, sizeof(board));
    for (int p = 0; p < IBLUE_PORTS; p++) {
        default_map(p);
        board.port[p].acc_mode = 1;
    }
    board.connected[1] = 1;
    snapshot();
    memset(&cfg, 0, sizeof(cfg));
    cfg.ops = &ops;
    iblue_cfg_window_init(window);
    pending = 0;
    board.present = 1;
}

int
board_read(void* ctx, int block, uint8_t* buf) {
    (void)ctx;
    if (!board.present) {
        memset(buf, 0, IBLUE_BLOCK_SIZE);
        return 0;
    }
    if (block == IBLUE_BLOCK_FIRST && pending && --delay <= 0) {
        iblue_cfg_run(&cfg, window);
        pending = 0;
    }
    board.reads++;
    memcpy(buf, window + (block - IBLUE_BLOCK_FIRST) * IBLUE_BLOCK_SIZE, IBLUE_BLOCK_SIZE);
    return 0;
}

int
board_write(void* ctx, int block, const uint8_t* buf) {
    (void)ctx;
    if (!board.present) {
        return 0;
    }
    for (int phase = 0; phase < 4; phase++) { /* the console writes a block in four phases */
        memcpy(window + (block - IBLUE_BLOCK_FIRST) * IBLUE_BLOCK_SIZE + phase * 128, buf + phase * 128, 128);
        if (block == IBLUE_BLOCK_FIRST && phase == 3) {
            pending = 1;
            delay = 3;
        }
    }
    return 0;
}

void board_sleep(void* ctx, int ms) { (void)ctx; (void)ms; board.sleeps++; }

int
fw_constants_match(const int* cmds, int ncmds, const int* results, int nres) {
    static const int fw_cmds[] = {IBLUE_CMD_HELLO, IBLUE_CMD_PORT_INFO, IBLUE_CMD_GET_MAP, IBLUE_CMD_SET_MAP,
                                  IBLUE_CMD_SET_TYPE, IBLUE_CMD_RESET_MAP, IBLUE_CMD_COMMIT, IBLUE_CMD_REVERT,
                                  IBLUE_CMD_LIVE, IBLUE_CMD_GET_GLOBAL, IBLUE_CMD_SET_GLOBAL, IBLUE_CMD_PROFILE_LIST,
                                  IBLUE_CMD_PROFILE_SAVE, IBLUE_CMD_PROFILE_LOAD, IBLUE_CMD_PROFILE_DELETE, IBLUE_CMD_GAME_SET,
                                  IBLUE_CMD_GAME_INFO, IBLUE_CMD_GAME_SAVE, IBLUE_CMD_GAME_DELETE};
    static const int fw_res[] = {IBLUE_RES_OK, IBLUE_RES_BAD_CMD, IBLUE_RES_BAD_ARG, IBLUE_RES_BAD_CRC,
                                 IBLUE_RES_LOCKOUT, IBLUE_RES_FAILED, IBLUE_RES_EMPTY, IBLUE_RES_NO_GAME};
    if (ncmds != 19 || nres != 8) return 0;
    for (int i = 0; i < ncmds; i++) if (cmds[i] != fw_cmds[i]) return 0;
    for (int i = 0; i < nres; i++) if (results[i] != fw_res[i]) return 0;
    return IBLUE_WINDOW_SIZE == 4096 && IBLUE_HDR_SIZE == 24 && IBLUE_BLOCK_FIRST == 200 && IBLUE_DST_NONE == 0x40 &&
        IBLUE_H_RESULT == 20 && IBLUE_H_CRC == 16 && IBLUE_H_SEQ == 12 && IBLUE_H_LEN == 10 && IBLUE_H_CMD == 8 &&
        IBLUE_H_STATUS == 6 && IBLUE_NAME_LEN == 24 && IBLUE_MAP_MAX == 128;
}

int board_dst(int p, int src) { return board.map[p][src].dst_btn; }
int board_map_size(int p) { return board.port[p].map_size; }
int board_dev(int p) { return board.port[p].dev_mode; }
int board_acc(int p) { return board.port[p].acc_mode; }
void board_set_held(int p, uint32_t w0) { board.held[p][0] = w0; }
void board_set_present(int on) { board.present = on; }
int board_stat(int which) { return which == 0 ? board.commits : which == 1 ? board.reverts : which == 2 ? board.sleeps : board.reads; }
int board_pairing(void) { return board.global.inquiry_mode; }
void board_set_plugged(int p, int on) { board.plugged[p] = (uint8_t)on; }
int board_field(int port, int src, int which) {
    const struct iblue_map* m = &board.map[port][src];
    switch (which) {
        case 0: return m->perc_max;
        case 1: return m->perc_threshold;
        case 2: return m->perc_deadzone;
        case 3: return m->turbo;
        default: return m->algo;
    }
}
int board_profile_used(int slot) { return board.prof[slot].used; }
int board_profile_dst(int slot, int src) { return board.prof_map[slot][src].dst_btn; }
const char* board_game(void) { return board.game; }
int board_game_stat(int which) { return which == 0 ? board.game_saves : which == 1 ? board.game_deletes : (board.own_game[0] != 0); }
void board_set_dst(int port, int src, int dst) { board.map[port][src].dst_btn = (uint8_t)dst; }
int hotkey_slot(unsigned int* btns, unsigned int* prev) {
    struct iblue_hotkey hk;
    int slot;
    hk.prev_faces = *prev;
    slot = iblue_hotkey_filter(&hk, btns);
    *prev = hk.prev_faces;
    return slot;
}
