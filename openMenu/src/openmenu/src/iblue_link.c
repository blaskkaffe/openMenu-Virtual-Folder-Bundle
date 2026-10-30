/* iblue_link: see iblue_link.h. */
#include <stddef.h>
#include <string.h>

#include "iblue_link.h"

#define CHUNK 512

/* Generic ids of the default shortcut buttons (BlueRetro's config_default_combo) */
static const uint8_t default_combo[10] = {BR_BTN_LM, BR_BTN_RM, BR_BTN_MM, BR_BTN_RB_UP, BR_BTN_RB_LEFT,
                                          BR_BTN_RB_RIGHT, BR_BTN_RB_DOWN, BR_BTN_LD_UP, BR_BTN_LD_DOWN, BR_BTN_MS};
#define KBM_MAX 108
#define COMBO_BASE 118

void
iblue_default_in_cfg(struct br_in_cfg* in, int port) {
    int j = 0;
    memset(in, 0, sizeof(*in));
    for (; j < KBM_MAX; j++) {
        in->map_cfg[j] = (struct br_map_cfg){(uint8_t)j, (uint8_t)j, (uint8_t)port, 100, 50, 135, 0, 0};
    }
    for (int k = 0; k < 10; k++, j++) {
        in->map_cfg[j] = (struct br_map_cfg){default_combo[k], (uint8_t)(COMBO_BASE + k), (uint8_t)port, 100, 50, 135, 0, 0};
    }
    in->map_size = (uint8_t)j;
}

/* ---- transfers ---------------------------------------------------------------------------- */

static int
read_all(iblue_link* l, uint8_t item, void* dst, uint32_t size) {
    for (uint32_t off = 0; off < size; off += CHUNK) {
        uint32_t n = size - off < CHUNK ? size - off : CHUNK;
        int r = l->io.read(l->io.ctx, item, off, (uint8_t*)dst + off, n);
        if (r) {
            return r;
        }
    }
    return 0;
}

/* Write bytes [off, off+len) of `data` (the struct br_config at `base`) to the adapter. The adapter
 * takes word aligned offsets, so the range grows to whole words. */
static int
push(iblue_link* l, const struct br_config* data, uint32_t off, uint32_t len) {
    uint32_t a = off & ~3u, e = (off + len + 3u) & ~3u;
    if (e > sizeof(*data)) {
        e = sizeof(*data);
    }
    while (a < e) {
        uint32_t n = e - a < CHUNK ? e - a : CHUNK;
        int r = l->io.write(l->io.ctx, BRCTL_ITEM_CONFIG, a, (const uint8_t*)data + a, n);
        if (r) {
            return r;
        }
        a += n;
    }
    return 0;
}

static int
reload(iblue_link* l) {
    int r = read_all(l, BRCTL_ITEM_CONFIG, &l->cfg, sizeof(l->cfg));
    if (r) {
        return r;
    }
    l->orig = l->cfg;
    l->unsaved = 0;
    return 0;
}

/* What the adapter changes on its own is reloaded; the working copy then is not "saved" any more */
static int
reload_unsaved(iblue_link* l) {
    int r = read_all(l, BRCTL_ITEM_CONFIG, &l->cfg, sizeof(l->cfg));
    if (r) {
        return r;
    }
    l->unsaved = memcmp(&l->cfg, &l->orig, sizeof(l->cfg)) != 0;
    return 0;
}

static int
op(iblue_link* l, uint32_t code, uint32_t a0, uint32_t a1, uint32_t a2) {
    int result = 0, r = l->io.op(l->io.ctx, code, a0, a1, a2, &result);
    if (r) {
        return r;
    }
    switch (result) {
        case BRCTL_RES_OK: return 0;
        case BRCTL_RES_EMPTY: return IBLUE_RES_EMPTY;
        case BRCTL_RES_NO_GAME: return IBLUE_RES_NO_GAME;
        case BRCTL_RES_FAILED: return IBLUE_RES_FAILED;
        default: return IBLUE_RES_BAD_ARG;
    }
}

static int
status(iblue_link* l, struct brctl_status* st) {
    int r = l->io.read(l->io.ctx, BRCTL_ITEM_STATUS, 0, st, sizeof(*st));
    if (r) {
        return r;
    }
    return st->magic == BRCTL_STATUS_MAGIC ? 0 : IBLUE_ERR_PROTO;
}

/* ---- open --------------------------------------------------------------------------------- */

int
iblue_link_open(iblue_link* l, const iblue_io* io) {
    struct brctl_info info;
    int r;

    memset(l, 0, sizeof(*l));
    l->io = *io;
    r = io->read(io->ctx, BRCTL_ITEM_INFO, 0, &info, sizeof(info));
    if (r) {
        return r;
    }
    if (info.magic != BRCTL_INFO_MAGIC) {
        return IBLUE_ERR_NO_BOARD;
    }
    if (info.proto_ver != BRCTL_PROTO_VER || info.config_size != sizeof(struct br_config) ||
        info.config_magic != BR_CONFIG_MAGIC) {
        return IBLUE_ERR_PROTO;
    }
    return reload(l);
}

/* ---- ports and mapping -------------------------------------------------------------------- */

int
iblue_link_port_info(iblue_link* l, int port, iblue_port_info* out) {
    struct brctl_status st;
    int r;
    if (port < 0 || port >= IBLUE_PORTS) {
        return IBLUE_RES_BAD_ARG;
    }
    r = status(l, &st);
    if (r) {
        return r;
    }
    memset(out, 0, sizeof(*out));
    out->dev_mode = l->cfg.out_cfg[port].dev_mode;
    out->acc_mode = l->cfg.out_cfg[port].acc_mode;
    out->connected = st.port_bt_id[port] != 0xFF;
    out->plugged = (st.plugged_mask >> port) & 1;
    out->map_size = l->cfg.in_cfg[port].map_size;
    if (out->connected) {
        strcpy(out->name, "Bluetooth controller");
    }
    return 0;
}

int
iblue_link_get_map(iblue_link* l, int port, iblue_map_entry out[IBLUE_MAP_MAX], int* size) {
    int n;
    if (port < 0 || port >= IBLUE_PORTS) {
        return IBLUE_RES_BAD_ARG;
    }
    n = l->cfg.in_cfg[port].map_size;
    if (n > IBLUE_MAP_MAX) {
        n = IBLUE_MAP_MAX;
    }
    memcpy(out, l->cfg.in_cfg[port].map_cfg, (size_t)n * sizeof(iblue_map_entry));
    *size = n;
    return 0;
}

static int
valid_entry(const iblue_map_entry* m) {
    return m->dst_id < IBLUE_PORTS && m->perc_max <= 100 && m->perc_threshold <= 100 && m->algo <= 5;
}

int
iblue_link_set_map(iblue_link* l, int port, int first, int count, const iblue_map_entry* in, int new_size) {
    uint32_t base;
    if (port < 0 || port >= IBLUE_PORTS || first < 0 || count < 0 || first + count > IBLUE_MAP_MAX ||
        (new_size >= 0 && (new_size > IBLUE_MAP_MAX || new_size < first + count))) {
        return IBLUE_RES_BAD_ARG;
    }
    for (int i = 0; i < count; i++) {
        if (!valid_entry(&in[i])) {
            return IBLUE_RES_BAD_ARG;
        }
    }
    memcpy(&l->cfg.in_cfg[port].map_cfg[first], in, (size_t)count * sizeof(*in));
    l->unsaved = 1;
    base = offsetof(struct br_config, in_cfg) + (uint32_t)port * sizeof(struct br_in_cfg);
    if (new_size >= 0 && new_size != l->cfg.in_cfg[port].map_size) {
        int r;
        l->cfg.in_cfg[port].map_size = (uint8_t)new_size;
        r = push(l, &l->cfg, base + offsetof(struct br_in_cfg, map_size), 1);
        if (r) {
            return r;
        }
    }
    return push(l, &l->cfg, base + offsetof(struct br_in_cfg, map_cfg) + (uint32_t)first * sizeof(*in),
                (uint32_t)count * sizeof(*in));
}

int
iblue_link_set_type(iblue_link* l, int port, int dev_mode, int acc_mode) {
    if (port < 0 || port >= IBLUE_PORTS || dev_mode < 0 || dev_mode > 3 || acc_mode < 0 || acc_mode > 3) {
        return IBLUE_RES_BAD_ARG;
    }
    l->cfg.out_cfg[port].dev_mode = (uint8_t)dev_mode;
    l->cfg.out_cfg[port].acc_mode = (uint8_t)acc_mode;
    l->unsaved = 1;
    return push(l, &l->cfg, offsetof(struct br_config, out_cfg) + (uint32_t)port * sizeof(struct br_out_cfg),
                sizeof(struct br_out_cfg));
}

int
iblue_link_reset_map(iblue_link* l, int port) {
    uint32_t base;
    if (port < 0 || port >= IBLUE_PORTS) {
        return IBLUE_RES_BAD_ARG;
    }
    iblue_default_in_cfg(&l->cfg.in_cfg[port], port);
    l->unsaved = 1;
    base = offsetof(struct br_config, in_cfg) + (uint32_t)port * sizeof(struct br_in_cfg);
    return push(l, &l->cfg, base, sizeof(struct br_in_cfg));
}

int
iblue_link_commit(iblue_link* l) {
    int r = op(l, BRCTL_OP_CONFIG_SAVE, 0, 0, 0);
    if (r == IBLUE_RES_BAD_ARG) {
        reload(l); /* the adapter refused and went back to what it had saved */
        return r;
    }
    if (r) {
        return r;
    }
    l->orig = l->cfg;
    l->unsaved = 0;
    return 0;
}

int
iblue_link_revert(iblue_link* l) {
    /* write back what differs from the saved copy, a block at a time */
    for (uint32_t off = 0; off < sizeof(l->cfg); off += CHUNK) {
        uint32_t n = sizeof(l->cfg) - off < CHUNK ? sizeof(l->cfg) - off : CHUNK;
        if (memcmp((const uint8_t*)&l->cfg + off, (const uint8_t*)&l->orig + off, n) != 0) {
            int r = push(l, &l->orig, off, n);
            if (r) {
                return r;
            }
        }
    }
    l->cfg = l->orig;
    l->unsaved = 0;
    return 0;
}

int
iblue_link_live(iblue_link* l, int port, uint32_t words[4], int* connected) {
    struct brctl_status st;
    int r;
    if (port < 0 || port >= IBLUE_PORTS) {
        return IBLUE_RES_BAD_ARG;
    }
    r = status(l, &st);
    if (r) {
        return r;
    }
    *connected = st.port_bt_id[port] != 0xFF;
    words[0] = *connected ? st.live[port] : 0;
    words[1] = words[2] = words[3] = 0;
    return 0;
}

int
iblue_link_get_pairing(iblue_link* l, int* mode) {
    *mode = l->cfg.global_cfg.inquiry_mode;
    return 0;
}

int
iblue_link_set_pairing(iblue_link* l, int mode) {
    if (mode < 0 || mode > 1) {
        return IBLUE_RES_BAD_ARG;
    }
    l->cfg.global_cfg.inquiry_mode = (uint8_t)mode;
    l->unsaved = 1;
    return push(l, &l->cfg, offsetof(struct br_config, global_cfg), sizeof(struct br_global_cfg));
}

/* ---- profiles and games ------------------------------------------------------------------- */

int
iblue_link_profiles(iblue_link* l, iblue_profile out[IBLUE_PROFILES]) {
    struct brctl_profiles p;
    int r = l->io.read(l->io.ctx, BRCTL_ITEM_PROFILES, 0, &p, sizeof(p));
    if (r) {
        return r;
    }
    if (p.magic != BRCTL_PROFILES_MAGIC) {
        return IBLUE_ERR_PROTO;
    }
    for (int i = 0; i < IBLUE_PROFILES; i++) {
        out[i].used = p.used[i];
        out[i].dev_mode = p.dev_mode[i];
        out[i].acc_mode = p.acc_mode[i];
    }
    return 0;
}

int
iblue_link_profile_save(iblue_link* l, int slot, int port) {
    return op(l, BRCTL_OP_PROFILE_SAVE, (uint32_t)slot, (uint32_t)port, 0);
}

int
iblue_link_profile_load(iblue_link* l, int slot, int port) {
    int r = op(l, BRCTL_OP_PROFILE_LOAD, (uint32_t)slot, (uint32_t)port, 0); /* live, not persisted */
    if (r) {
        return r;
    }
    return reload_unsaved(l);
}

int
iblue_link_profile_delete(iblue_link* l, int slot) {
    return op(l, BRCTL_OP_PROFILE_DELETE, (uint32_t)slot, 0, 0);
}

static void
game_id(char out[16], const char* id) {
    int n = 0;
    memset(out, 0, 16);
    /* the id becomes a file name on the adapter: letters, digits, - and _ only */
    for (int i = 0; id[i] && n < IBLUE_GAME_ID_MAX; i++, n++) {
        char c = id[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_';
        out[n] = ok ? c : '_';
    }
    while (n > 0 && out[n - 1] == '_' && id[n - 1] == ' ') {
        out[--n] = '\0'; /* trailing padding */
    }
}

static int
game_apply(const iblue_io* io, const char* product, int* result_flags) {
    struct brctl_game g;
    int result = 0, r;
    memset(&g, 0, sizeof(g));
    game_id(g.id, product);
    r = io->write(io->ctx, BRCTL_ITEM_GAME, 0, &g, sizeof(g));
    if (r) {
        return r;
    }
    r = io->op(io->ctx, BRCTL_OP_GAME_SET, 0, 0, 0, &result);
    if (r) {
        return r;
    }
    if (result_flags) {
        r = io->read(io->ctx, BRCTL_ITEM_GAME, 0, &g, sizeof(g));
        if (r) {
            return r;
        }
        *result_flags = g.flags;
    }
    return result == BRCTL_RES_OK ? 0 : IBLUE_RES_BAD_ARG;
}

int
iblue_link_game_set(iblue_link* l, const char* id, int* has_own) {
    int flags = 0, r = game_apply(&l->io, id, &flags);
    if (r) {
        return r;
    }
    if (has_own) {
        *has_own = (flags & BRCTL_GAME_HAS_OWN) != 0;
    }
    return reload(l); /* the adapter loaded the game's configuration, or the normal one */
}

int
iblue_link_game_save(iblue_link* l) {
    int r = op(l, BRCTL_OP_GAME_SAVE, 0, 0, 0);
    if (r) {
        return r;
    }
    l->orig = l->cfg;
    l->unsaved = 0;
    return 0;
}

int
iblue_link_game_delete(iblue_link* l) {
    int r = op(l, BRCTL_OP_GAME_DELETE, 0, 0, 0);
    if (r) {
        return r;
    }
    return reload(l);
}

int
iblue_game_apply(const iblue_io* io, const char* product) {
    struct brctl_info info;
    int r = io->read(io->ctx, BRCTL_ITEM_INFO, 0, &info, sizeof(info));
    if (r) {
        return r;
    }
    if (info.magic != BRCTL_INFO_MAGIC || info.proto_ver != BRCTL_PROTO_VER || !(info.features & BRCTL_FEAT_PROFILES)) {
        return IBLUE_ERR_NO_BOARD;
    }
    return game_apply(io, product, NULL);
}
