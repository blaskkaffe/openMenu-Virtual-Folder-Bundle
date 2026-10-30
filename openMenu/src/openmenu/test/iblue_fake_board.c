/*
 * A fake adapter for the host test. It follows the firmware (main/iblue/brctl.c, profile.c): the live
 * config is what a CONFIG write changes, CONFIG_SAVE checks it and keeps it (in the game's file when a
 * game's configuration is in use), a refused save reloads what was saved.
 */
#include <stdio.h>
#include <string.h>

#include "iblue_fake_board.h"
#include "iblue_link.h"

struct profile {
    int used;
    uint8_t dev_mode, acc_mode;
    uint8_t map_size;
    struct br_map_cfg map[BR_MAPPING_MAX];
};

static struct {
    int present;
    struct br_config live, flash, game_flash;
    int game_has_own, game_in_use;
    char game[BRCTL_GAME_ID_MAX + 1], own_game[BRCTL_GAME_ID_MAX + 1];
    struct brctl_game game_item;
    uint8_t connected[4], plugged[4];
    uint32_t held[4];
    struct profile prof[BRCTL_PROFILES];
    uint16_t op_seq;
    uint8_t op_result;
    int saves, writes, reads, ops, game_saves, game_deletes;
} b;

static void
default_config(struct br_config* c) {
    memset(c, 0, sizeof(*c));
    c->magic = BR_CONFIG_MAGIC;
    for (int p = 0; p < BR_WIRED_MAX_DEV; p++) {
        iblue_default_in_cfg(&c->in_cfg[p], p);
        c->out_cfg[p].acc_mode = BR_ACC_MEM;
    }
}

void
board_reset(void) {
    memset(&b, 0, sizeof(b));
    default_config(&b.flash);
    b.live = b.flash;
    b.connected[1] = 1;
    b.present = 1;
}

void board_set_present(int on) { b.present = on; }

static int
valid(const struct br_config* c) {
    if (c->magic != BR_CONFIG_MAGIC || c->global_cfg.inquiry_mode > 1) return 0;
    for (int i = 0; i < BR_WIRED_MAX_DEV; i++) {
        if (c->out_cfg[i].dev_mode > 3 || c->out_cfg[i].acc_mode > 3 || c->in_cfg[i].map_size > BR_MAPPING_MAX) return 0;
        for (int j = 0; j < c->in_cfg[i].map_size; j++)
            if (c->in_cfg[i].map_cfg[j].dst_id >= BR_WIRED_MAX_DEV) return 0;
    }
    return 1;
}

static void
fill_status(struct brctl_status* s) {
    memset(s, 0, sizeof(*s));
    s->magic = BRCTL_STATUS_MAGIC;
    s->btn_last = 0xFF;
    for (int i = 0; i < 4; i++) {
        s->port_bt_id[i] = b.connected[i] ? (uint8_t)i : 0xFF;
        s->live[i] = b.connected[i] ? b.held[i] : 0;
        if (b.plugged[i]) s->plugged_mask |= (uint8_t)(1 << i);
    }
    s->op_seq = b.op_seq;
    s->op_result = b.op_result;
}

static void
game_flags(struct brctl_game* g) {
    g->flags = 0;
    if (b.game[0]) {
        g->flags = (uint8_t)(BRCTL_GAME_SET | (b.game_has_own && strcmp(b.game, b.own_game) == 0 ? BRCTL_GAME_HAS_OWN : 0) |
                             (b.game_in_use ? BRCTL_GAME_IN_USE : 0));
        snprintf(g->id, sizeof(g->id), "%s", b.game);
    }
}

static int
io_read(void* ctx, uint8_t item, uint32_t off, void* buf, uint32_t len) {
    uint8_t tmp[sizeof(struct br_config)];
    const uint8_t* src = tmp;
    uint32_t size = 0;
    (void)ctx;
    b.reads++;
    if (!b.present) return IBLUE_ERR_NO_BOARD;
    if (off & 3 || len > BRCTL_MAX_XFER) return IBLUE_RES_BAD_ARG;
    switch (item) {
        case BRCTL_ITEM_INFO: {
            struct brctl_info i;
            memset(&i, 0, sizeof(i));
            i.magic = BRCTL_INFO_MAGIC;
            i.proto_ver = BRCTL_PROTO_VER;
            i.config_size = sizeof(struct br_config);
            i.config_magic = BR_CONFIG_MAGIC;
            i.features = BRCTL_FEAT_PROFILES;
            memcpy(tmp, &i, sizeof(i));
            size = sizeof(i);
            break;
        }
        case BRCTL_ITEM_STATUS: {
            struct brctl_status s;
            fill_status(&s);
            memcpy(tmp, &s, sizeof(s));
            size = sizeof(s);
            break;
        }
        case BRCTL_ITEM_CONFIG:
            src = (const uint8_t*)&b.live;
            size = sizeof(b.live);
            break;
        case BRCTL_ITEM_PROFILES: {
            struct brctl_profiles p;
            memset(&p, 0, sizeof(p));
            p.magic = BRCTL_PROFILES_MAGIC;
            for (int i = 0; i < BRCTL_PROFILES; i++) {
                p.used[i] = (uint8_t)b.prof[i].used;
                p.dev_mode[i] = b.prof[i].dev_mode;
                p.acc_mode[i] = b.prof[i].acc_mode;
            }
            memcpy(tmp, &p, sizeof(p));
            size = sizeof(p);
            break;
        }
        case BRCTL_ITEM_GAME: {
            struct brctl_game g = b.game_item;
            game_flags(&g);
            memcpy(tmp, &g, sizeof(g));
            size = sizeof(g);
            break;
        }
        default: return IBLUE_RES_BAD_ARG;
    }
    if (off >= size) return IBLUE_RES_BAD_ARG;
    if (len > size - off) len = size - off;
    memcpy(buf, src + off, len);
    return 0;
}

static int
io_write(void* ctx, uint8_t item, uint32_t off, const void* buf, uint32_t len) {
    (void)ctx;
    b.writes++;
    if (!b.present) return IBLUE_ERR_NO_BOARD;
    if (off & 3 || len > BRCTL_MAX_XFER) return IBLUE_RES_BAD_ARG;
    if (item == BRCTL_ITEM_CONFIG) {
        if (off > sizeof(b.live) || len > sizeof(b.live) - off) return IBLUE_RES_BAD_ARG;
        memcpy((uint8_t*)&b.live + off, buf, len);
        return 0;
    }
    if (item == BRCTL_ITEM_GAME) {
        if (off > sizeof(b.game_item) || len > sizeof(b.game_item) - off) return IBLUE_RES_BAD_ARG;
        memcpy((uint8_t*)&b.game_item + off, buf, len);
        return 0;
    }
    return IBLUE_RES_BAD_ARG;
}

static uint8_t
run_op(uint32_t op, uint32_t a0, uint32_t a1, uint32_t a2) {
    switch (op) {
        case BRCTL_OP_CONFIG_SAVE:
            if (!valid(&b.live)) {
                b.live = b.game_in_use ? b.game_flash : b.flash; /* reload what was saved */
                return BRCTL_RES_BAD;
            }
            if (b.game_in_use) b.game_flash = b.live; else b.flash = b.live;
            b.saves++;
            return BRCTL_RES_OK;
        case BRCTL_OP_PROFILE_SAVE: {
            struct profile* p;
            if (a0 >= BRCTL_PROFILES || a1 >= 4) return BRCTL_RES_BAD;
            p = &b.prof[a0];
            p->used = 1;
            p->dev_mode = b.live.out_cfg[a1].dev_mode;
            p->acc_mode = b.live.out_cfg[a1].acc_mode;
            p->map_size = b.live.in_cfg[a1].map_size;
            memcpy(p->map, b.live.in_cfg[a1].map_cfg, sizeof(p->map));
            return BRCTL_RES_OK;
        }
        case BRCTL_OP_PROFILE_LOAD: {
            struct profile* p;
            if (a0 >= BRCTL_PROFILES || a1 >= 4) return BRCTL_RES_BAD;
            p = &b.prof[a0];
            if (!p->used) return BRCTL_RES_EMPTY;
            b.live.out_cfg[a1].dev_mode = p->dev_mode & 3;
            b.live.out_cfg[a1].acc_mode = p->acc_mode & 3;
            b.live.in_cfg[a1].map_size = p->map_size;
            memcpy(b.live.in_cfg[a1].map_cfg, p->map, sizeof(p->map));
            for (int i = 0; i < p->map_size; i++) b.live.in_cfg[a1].map_cfg[i].dst_id = (uint8_t)a1;
            if (a2 & 1) { if (b.game_in_use) b.game_flash = b.live; else b.flash = b.live; }
            return BRCTL_RES_OK;
        }
        case BRCTL_OP_PROFILE_DELETE:
            if (a0 >= BRCTL_PROFILES) return BRCTL_RES_BAD;
            b.prof[a0].used = 0;
            return BRCTL_RES_OK;
        case BRCTL_OP_GAME_SET: {
            char id[17];
            snprintf(id, sizeof(id), "%s", b.game_item.id);
            if (strlen(id) > BRCTL_GAME_ID_MAX) return BRCTL_RES_BAD;
            for (const char* c = id; *c; c++)
                if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '-' || *c == '_'))
                    return BRCTL_RES_BAD;
            memcpy(b.game, id, sizeof(b.game) - 1);
            b.game[sizeof(b.game) - 1] = 0;
            b.game_in_use = b.game[0] && b.game_has_own && strcmp(b.game, b.own_game) == 0;
            b.live = b.game_in_use ? b.game_flash : b.flash; /* config_init(GAMEID_CFG) */
            return BRCTL_RES_OK;
        }
        case BRCTL_OP_GAME_SAVE:
            if (!b.game[0]) return BRCTL_RES_NO_GAME;
            snprintf(b.own_game, sizeof(b.own_game), "%s", b.game);
            b.game_flash = b.live;
            b.game_has_own = 1;
            b.game_in_use = 1;
            b.game_saves++;
            return BRCTL_RES_OK;
        case BRCTL_OP_GAME_DELETE:
            if (!b.game[0]) return BRCTL_RES_NO_GAME;
            b.game_has_own = 0;
            b.game_in_use = 0;
            b.live = b.flash;
            b.game_deletes++;
            return BRCTL_RES_OK;
        default: return BRCTL_RES_OK;
    }
}

static int
io_op(void* ctx, uint32_t op, uint32_t a0, uint32_t a1, uint32_t a2, int* result) {
    (void)ctx;
    b.ops++;
    if (!b.present) return IBLUE_ERR_NO_BOARD;
    b.op_result = run_op(op, a0, a1, a2);
    b.op_seq++;
    if (result) *result = b.op_result;
    return 0;
}

const iblue_io board_io = {NULL, io_read, io_write, io_op};

int board_dst(int p, int src) { return b.live.in_cfg[p].map_cfg[src].dst_btn; }
int board_map_size(int p) { return b.live.in_cfg[p].map_size; }
int board_dev(int p) { return b.live.out_cfg[p].dev_mode; }
int board_acc(int p) { return b.live.out_cfg[p].acc_mode; }
int board_pairing(void) { return b.live.global_cfg.inquiry_mode; }
int board_field(int p, int src, int which) {
    const struct br_map_cfg* m = &b.live.in_cfg[p].map_cfg[src];
    switch (which) {
        case 0: return m->perc_max;
        case 1: return m->perc_threshold;
        case 2: return m->perc_deadzone;
        case 3: return m->turbo;
        default: return m->algo;
    }
}
void board_set_dst(int p, int src, int dst) { b.live.in_cfg[p].map_cfg[src].dst_btn = (uint8_t)dst; }
void board_set_held(int p, uint32_t w0) { b.held[p] = w0; }
void board_set_plugged(int p, int on) { b.plugged[p] = (uint8_t)on; }
void board_set_connected(int p, int on) { b.connected[p] = (uint8_t)on; }
int board_flash_dst(int p, int src) { return (b.game_in_use ? b.game_flash : b.flash).in_cfg[p].map_cfg[src].dst_btn; }
int board_stat(int which) { return which == 0 ? b.saves : which == 1 ? b.writes : which == 2 ? b.reads : b.ops; }
int board_profile_used(int slot) { return b.prof[slot].used; }
int board_profile_dst(int slot, int src) { return b.prof[slot].map[src].dst_btn; }
const char* board_game(void) { return b.game; }
int board_game_stat(int which) { return which == 0 ? b.game_saves : which == 1 ? b.game_deletes : b.game_has_own; }
void board_corrupt_map_size(int p) { b.live.in_cfg[p].map_size = 200; }
