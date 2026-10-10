/* bios_filescr: see bios_filescr.h. Function and data names in the comments are those of the boot ROM decompile. */
#include "bios_filescr.h"

#include <stdio.h>
#include <string.h>

#include "bios_surface.h"
#include "bios_text.h"

/* ---- object ids (all as in the BIOS) ---------------------------------------------------------------------- */
#define ID_FRAME 0x1000   /* script 0x0C: the frame of the card grid, with the help line */
#define ID_PAD(p) ((uint16_t)(0x1002 + (p))) /* scripts 0x0E..0x11: the controllers A..D */
#define ID_PLATE 0x1006   /* script 0x15 (grid) / 0x52 (browser): the memory card plate, free blocks */
#define ID_CARD(i) ((uint16_t)(0x1100 + (i))) /* script 0x12 (grid) / 0x51 (browser, i = 0) */
#define ID_BACK 0x1110
#define ID_WINDOW 0x1120  /* script 0x13: the file window (model 38), help text */
#define ID_INFO 0x1140    /* script 0x28: the file information */
#define ID_TILES 0x1180   /* script 0x14: the file tiles */
#define ID_ARROW 0x1188   /* script 0x29: the copy arrow from source to destination */
#define ID_UP 0x1190      /* scripts 0x38 / 0x39: the scroll marks */
#define ID_DOWN 0x1191
#define ID_COPYBOX 0x11A0 /* script 0x3A: "Copying..." */
#define ID_COPYBAR 0x11A1 /* script 0x3B: its progress bar */
#define ID_ZOOM 0x01A0    /* script 7: the card flying between the grid and the browser */
#define ID_POPUP 400      /* script 5: popup panel */
#define ID_POPBTN(i) ((uint16_t)(0x191 + (i))) /* script 4: popup choices */

#define PRIO 0x1000

#define MODEL_CARD(i) (0x3E + (i))
#define MODEL_SOCKET 0x10
#define MODEL_PLATE 18
#define MODEL_WINDOW 38

/* ---- ROM tables ---------------------------------------------------------------------------------------- */

static int
rom_s32(const bfs* fs, uint32_t addr) {
    const uint8_t* p = bios_rom_ptr(fs->rom, addr, 4);
    return p ? (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)) : 0;
}

/* card positions (* 256), index 8 = the card shown in the browser (DAT_8C036F98 / DAT_8C036FBC) */
static int card_x(const bfs* fs, int i) { return rom_s32(fs, 0x8C036F98u + (uint32_t)i * 4u); }
static int card_y(const bfs* fs, int i) { return rom_s32(fs, 0x8C036FBCu + (uint32_t)i * 4u); }

/* Cursor tables (cursor_tables.txt): up, down, left, right */
static const signed char grid_table[9][4] = { /* 0x8C03884C */
    {1, 1, 8, 2}, {0, 0, 8, 3}, {3, 3, 0, 4}, {2, 2, 1, 5}, {5, 5, 2, 6},
    {4, 4, 3, 7}, {7, 7, 4, 8}, {6, 6, 5, 8}, {0, 1, 7, 1},
};
static const signed char browser_table[26][4] = { /* 0x8C0389B8: 0 the card (all files), 1 BACK, 2..25 the tiles */
    {0, 1, 1, 2},     {0, 1, 9, 0},     {2, 10, 0, 3},    {3, 11, 2, 4},    {4, 12, 3, 5},    {5, 13, 4, 6},
    {6, 14, 5, 7},    {7, 15, 6, 8},    {8, 16, 7, 9},    {9, 17, 8, 1},    {2, 18, 0, 11},   {3, 19, 10, 12},
    {4, 20, 11, 13},  {5, 21, 12, 14},  {6, 22, 13, 15},  {7, 23, 14, 16},  {8, 24, 15, 17},  {9, 25, 16, 1},
    {10, 18, 0, 19},  {11, 19, 18, 20}, {12, 20, 19, 21}, {13, 21, 20, 22}, {14, 22, 21, 23}, {15, 23, 22, 24},
    {16, 24, 23, 25}, {17, 25, 24, 1},
};
static const signed char single_table[1][4] = {{0, 0, 0, 0}};
static signed char popup_table[4][4];

/* Popups (DAT_8C038EC4 ...): panel w h, text surface w h, text x y, button offset, spacing, position * 256 */
#define POPUP_Z (-85000)
static const bfs_popup_def pop_card_all = {490, 212, 512, 256, 92, 115, 0, 0, 212, -500, 0, POPUP_Z, 3, 2,
                                           {{0, 0x265}, {0, 0x266}, {0, 0x25F}}};
static const bfs_popup_def pop_card_nocopy = {490, 212, 512, 256, 92, 115, 0, 0, 212, -500, 0, POPUP_Z, 3, 2,
                                              {{1, 0x265}, {0, 0x266}, {0, 0x25F}}};
static const bfs_popup_def pop_files = {256, 212, 256, 256, 72, 115, 0, 0, 212, 0, 0, POPUP_Z, 3, 2,
                                        {{0, 0x25D}, {0, 0x25E}, {0, 0x25F}}};
static const bfs_popup_def pop_files_nocopy = {460, 212, 512, 256, 106, 115, 0, 0, 212, 0, 0, POPUP_Z, 3, 2,
                                               {{1, 0x38D}, {0, 0x25E}, {0, 0x25F}}};
static const bfs_popup_def pop_dest = {350, 212, 256, 256, 30, 116, 0, 0, 212, -256, 0, POPUP_Z, 3, 2,
                                       {{0, 0x260}, {0, 0x261}, {0, 0x25F}}};
static const bfs_popup_def pop_yes_no = {190, 140, 256, 256, 100, 115, 0, 0, 140, 0, 0, POPUP_Z, 2, 1,
                                         {{0, 0x263}, {0, 0x264}}};

int bfs_message_box_(bfs* fs, int id);
int bfs_format_flow_(bfs* fs);

/* ---- helpers --------------------------------------------------------------------------------------------- */

static void
sfx(bfs* fs, int id) {
    if (fs->host.sfx) {
        fs->host.sfx(fs->host.user, id);
    }
}

static bvm_obj*
obj(bfs* fs, uint16_t id) {
    return bvm_find(&fs->m->vm, id);
}

static bvm_obj*
create(bfs* fs, int script, uint16_t id, int prio) {
    return bvm_create(&fs->m->vm, script, id, (int16_t)prio);
}

static void
destroy(bfs* fs, uint16_t id) {
    bvm_kill(&fs->m->vm, id);
    bsurf_free(id);
}

static void
flag17(bfs* fs, uint16_t id, int on) {
    bvm_obj* o = obj(fs, id);
    if (o) {
        o->flags = on ? (o->flags | BVM_F_FLAG17) : (o->flags & ~(uint32_t)BVM_F_FLAG17);
    }
}

static const char*
msg(const bfs* fs, int id, int line) {
    return btext_message(fs->rom, fs->language, id, line);
}

static bsurf*
surf(bfs* fs, uint16_t id, int w, int h) {
    (void)fs;
    bsurf* s = bsurf_find(id);
    if (!s || s->w != w || s->h != h) {
        s = bsurf_get(id, w, h);
    }
    return s;
}

static int
card_usable(const bfs* fs, int i) { /* status 3 or 4 */
    return i >= 0 && i < BFS_SLOTS && (fs->card[i].status == BFS_CARD_READY || fs->card[i].status == BFS_CARD_UNFORMATTED);
}

/* screen_fly_in 0x8C010C9C / screen_fly_out / screen_transition_busy */
static void
fly_in(bfs* fs) {
    fs->fly = 1;
    fs->fly_alpha = 0xFF;
}

static void
fly_out(bfs* fs) {
    fs->fly = -1;
    fs->fly_alpha = 0;
}

static int
transition_busy(bfs* fs) {
    if (fs->fly == -1) {
        fs->fly_alpha += 0x20;
        if (fs->fly_alpha > 0xFE) {
            fs->fly = -2;
            fs->fly_alpha = 0xFF;
        }
    } else if (fs->fly == 1) {
        fs->fly_alpha -= 0x20;
        if (fs->fly_alpha < 1) {
            fs->fly = 2;
            fs->fly_alpha = 0;
        }
    } else if (fs->fly == 2 || fs->fly == -2) {
        fs->fly = 0;
    }
    return fs->fly;
}

/* cursor_install (layer 0) and FUN_8C0108A8 / FUN_8C0108EA (the popup layer) */
static void
cursor_install(bfs* fs, int layer, const signed char (*table)[4], int count, int pos, void (*cb)(bfs*, int, int*)) {
    bfs_cursor* c = &fs->cur[layer];
    c->table = table;
    c->count = count;
    c->pos = pos;
    c->cb = cb;
    c->on = table != NULL;
    if (layer == 1) {
        fs->layer = 1;
    }
}

static void
cursor_off(bfs* fs) {
    fs->cur[0].on = 0;
}

/* cursor_update 0x8C0108F8: a direction moves along the table first, then the callback sees the new position;
 * A (5), B (6), and 7 when nothing was pressed. */
static void
cursor_update(bfs* fs) {
    bfs_cursor* c = &fs->cur[fs->layer];
    int key = fs->key;
    fs->key = 0;
    if (!c->on || fs->fly != 0 || !c->cb) {
        return;
    }
    int pos = c->pos, code = 7;
    if (key >= BFS_KEY_UP && key <= BFS_KEY_RIGHT) {
        code = key;
        if (pos >= 0 && pos < c->count) {
            pos = c->table[pos][key - 1];
        }
    } else if (key == BFS_KEY_A || key == BFS_KEY_B) {
        code = key;
    }
    c->cb(fs, code, &pos);
    c->pos = pos;
}

void
bfs_mono_icon(const uint8_t bits[128], uint16_t fg, uint16_t out[32 * 32]) {
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            out[y * 32 + x] = (bits[y * 4 + x / 8] >> (7 - (x & 7))) & 1 ? fg : 0xFBC6;
        }
    }
}

/* ---- script effects of the card objects (the scenes run them every frame) ------------------------------- */

/* fx_vmu_icon_texture 0x8C0217E0: the card's model (the card, or the empty socket), its colour, and its picture
 * doubled to 64x64 at the top of the card's 64x128 text surface. While the card is read, the VMU's busy pictures
 * 1..4 run instead. */
static void
fx_card(bfs* fs, bvm_obj* o, int sig_slot) {
    int i = o->var[0];
    if (i < 0 || i >= BFS_SLOTS) {
        return;
    }
    const bfs_card* c = &fs->card[i];
    int ready = c->status == BFS_CARD_READY || c->status == BFS_CARD_UNFORMATTED;
    o->model = (uint16_t)(ready || c->status == BFS_CARD_READING ? MODEL_CARD(i) : MODEL_SOCKET);
    bsurf* s = surf(fs, o->id, 64, 128);
    if (!s) {
        return;
    }
    s->hidden = !(ready || c->status == BFS_CARD_READING);
    uint32_t sig = ((c->serial * 8u + (uint32_t)c->status) * 16u + (uint32_t)i) ^
                   (c->status == BFS_CARD_READING ? (uint32_t)fs->g_busy_frame << 28 : 0u);
    if (fs->card_sig[sig_slot] == sig + 1u) {
        return;
    }
    fs->card_sig[sig_slot] = sig + 1u;
    uint16_t busy[32 * 32];
    const uint16_t* src = c->icon;
    if (c->status == BFS_CARD_READING) {
        const uint8_t* font = bsurf_font();
        if (font) {
            bfs_mono_icon(font + BTEXT_VMU_ICONS + (uint32_t)(fs->g_busy_frame + 1) * 128u, 0xF225, busy);
            src = busy;
        }
    }
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            s->px[y * 64 + x] = src[(y / 2) * 32 + x / 2];
        }
    }
    bsurf_touch(s, 0, 63);
}

/* fx_free_blocks_text 0x8C021A00: "A-1", the free blocks and "Free" on the plate, and its gauge (node 2 of model
 * 18 scaled to the used part of the card). */
static void
fx_plate(bfs* fs, bvm_obj* o) {
    int i = o->var[0];
    bsurf* s = surf(fs, o->id, 128, 128);
    if (!s) {
        return;
    }
    int ok = i >= 0 && i < BFS_SLOTS && fs->card[i].status == BFS_CARD_READY;
    int sig = ok ? (int)(fs->card[i].serial * 16u) + i + 1 : -1;
    if (sig == fs->plate_sig) {
        return;
    }
    fs->plate_sig = sig;
    bsurf_clear(s);
    float gauge = 0.0f;
    if (ok) {
        const bfs_card* c = &fs->card[i];
        char label[4] = {(char)('A' + i / 2), '-', (char)('1' + i % 2), '\0'};
        s->advance = 13;
        bsurf_print(s, 0x30, 0x20, label);
        bsurf_print_number(s, 0x10, 0x4B, c->free_blocks, -4);
        s->advance = 11;
        bsurf_print(s, 0x14, 100, msg(fs, 0x275, 0));
        gauge = c->total_blocks ? (float)(c->total_blocks - c->free_blocks) / (float)c->total_blocks : 0.0f;
    }
    nj_node* n = bscene_node(&fs->m->scene, MODEL_PLATE, 2);
    if (n && (!fs->gauge_set || n->scl.x != gauge)) {
        n->scl.x = gauge;
        n->eval &= ~(uint32_t)NJ_EVAL_NO_SCALE;
        fs->gauge_set = 1;
        bscene_cache_clear();
    }
}

/* FUN_8C021780: a controller that is not plugged in is not drawn */
static void
fx_pad(bfs* fs, bvm_obj* o) {
    int port = o->var[0];
    if ((fs->ports >> port) & 1) {
        o->flags &= ~(uint32_t)BVM_F_HIDE;
    } else {
        o->flags |= BVM_F_HIDE;
    }
}

/* FUN_8C021BF4: the file window's "ALL" mark (nodes 4..7 of model 38): letters yellow on blue, inverted while the
 * cursor is on the card (var1). */
static void
window_colours(bfs* fs, bscene* sc) {
    bvm_obj* w = obj(fs, ID_WINDOW);
    if (!w) {
        return;
    }
    uint32_t letters = w->var[1] ? 0xB219197Fu : 0xE0E0E030u, back = w->var[1] ? 0xE0E0E030u : 0xB219197Fu;
    static const int nodes[4] = {4, 6, 7, 5};
    for (int k = 0; k < 4 && sc->ovr_n < BSCENE_OVR_MAX; k++) {
        sc->ovr[sc->ovr_n].model = MODEL_WINDOW;
        sc->ovr[sc->ovr_n].node = nodes[k];
        sc->ovr[sc->ovr_n].poly = 0;
        sc->ovr[sc->ovr_n].argb = k < 3 ? letters : back;
        sc->ovr[sc->ovr_n].lit = 1;
        sc->ovr_n++;
    }
}

static void
run_effects(bfs* fs) {
    bvm* vm = &fs->m->vm;
    fs->g_tick++;
    if (fs->g_tick > 4) { /* the busy picture changes every 5 frames (DAT_8C3814C8 / C6) */
        fs->g_tick = 0;
        fs->g_busy_frame = (fs->g_busy_frame + 1) & 3;
    }
    for (int k = 0; k < vm->count; k++) {
        bvm_obj* o = &vm->objs[vm->order[k]];
        if (!o->active) {
            continue;
        }
        if (o->id >= ID_CARD(0) && o->id <= ID_CARD(7)) {
            fx_card(fs, o, o->id - ID_CARD(0));
        } else if (o->id == ID_PLATE) {
            fx_plate(fs, o);
        } else if (o->id >= ID_PAD(0) && o->id <= ID_PAD(3)) {
            fx_pad(fs, o);
        }
    }
}

/* ---- the card grid: vmu_card_select_grid 0x8C01F742 ------------------------------------------------------ */

/* vmu_help_line 0x8C01F720 */
static void
help_line(bfs* fs, int message, int dest) {
    fs->g_result = 0;
    fs->g_dest = dest;
    fs->g_help = message;
    fs->g_state = 0;
}

/* FUN_8C020600: count the cards that are there; returns 1 when the count changed */
static int
count_cards(bfs* fs) {
    int n = 0;
    for (int i = 0; i < BFS_SLOTS; i++) {
        n += card_usable(fs, i);
    }
    if (n == fs->g_ready) {
        return 0;
    }
    fs->g_ready = n;
    return 1;
}

/* vmu_progress_dialog 0x8C020340 */
static void
progress_dialog(bfs* fs, int what) {
    bsurf* s = surf(fs, ID_FRAME, 512, 64);
    if (!s) {
        return;
    }
    int id;
    if (what == 0) {
        bsurf_clear(s);
        id = fs->g_dest ? 0x67 : (fs->g_ready == 0 ? 0x11 : 0x65);
        bsurf_print(s, -256, 1, msg(fs, id, 0));
        bsurf_print(s, -256, 0x19, msg(fs, id, 1));
    } else if (what == 1) {
        bsurf* b = surf(fs, ID_COPYBOX, 512, 128);
        bsurf_clear(s);
        bsurf_print(s, -256, 1, msg(fs, 0xD0, 0));
        bsurf_print(s, -256, 0x19, msg(fs, 0xD0, 1));
        if (b) {
            bsurf_clear(b);
            if (!fs->protected_shown) {
                bsurf_print(b, 0x50, 10, msg(fs, 0x262, 0));
            } else {
                bsurf_print(b, 0x50, 10, msg(fs, 0x38F, 0));
                bsurf_print(b, 0x50, 0x23, msg(fs, 0x38F, 1));
                bsurf_print(b, 0x50, 0x3C, msg(fs, 0x38F, 2));
            }
        }
    } else if (what == 2) {
        bsurf_clear(s);
        id = fs->mode == 0 ? 0xCC : 0xD4;
        bsurf_print(s, -256, 1, msg(fs, id, 0));
        bsurf_print(s, -256, 0x19, msg(fs, id, 1));
    } else if (what == 3) { /* the name of the file being copied */
        bsurf* b = surf(fs, ID_COPYBOX, 512, 128);
        if (b && fs->op.current >= 0 && fs->op.current < fs->op.nfiles) {
            int f = fs->op.files[fs->op.current];
            bsurf_fill(b, 300, 10, 150, 26, b->bg);
            if (f >= 0 && f < fs->card[fs->op.src].nfiles) {
                bsurf_print(b, 300, 10, fs->card[fs->op.src].files[f].name);
            }
        }
    }
}

/* the arrow from the source card to the destination (cursor callback tail) */
static void
grid_arrow(bfs* fs) {
    bvm_obj* a = obj(fs, ID_ARROW);
    if (!a) {
        return;
    }
    int c = fs->g_cursor, s = fs->src;
    int t = (c >= 0 && c < 8 && s >= 0 && s < 8 && fs->arrow_tab) ? fs->arrow_tab[c + s * 8] : 0;
    if (c == 8 || t == 0 || !fs->g_dest) {
        a->var[4] = 1;
    } else {
        a->var[0] = ID_CARD(s);
        a->var[1] = (signed char)t;
        a->var[2] = rom_s32(fs, 0x8C037164u + (uint32_t)(c * 4 + s * 0x20));
        a->var[3] = rom_s32(fs, 0x8C037264u + (uint32_t)(c * 4 + s * 0x20));
        a->var[4] = 0;
    }
}

static int
grid_pick_ok(bfs* fs, int i) {
    if (i == 8) {
        return 1;
    }
    int st = fs->card[i].status;
    return (st == BFS_CARD_READY && (fs->src != i || !fs->g_dest)) || (st == BFS_CARD_UNFORMATTED && !fs->g_dest);
}

static void
grid_select(bfs* fs) {
    for (int i = 0; i < 8; i++) {
        bvm_obj* c = obj(fs, ID_CARD(i));
        if (c) {
            c->flags &= ~(uint32_t)BVM_F_MOTION;
        }
    }
    bvm_obj* back = obj(fs, ID_BACK);
    if (fs->g_cursor < 8) {
        bvm_obj* c = obj(fs, ID_CARD(fs->g_cursor));
        if (c) {
            c->flags |= BVM_F_MOTION;
            c->motion_tw.cur = 15.0f;
        }
    }
    if (back) {
        back->var[0] = fs->g_cursor > 7;
    }
    bvm_obj* plate = obj(fs, ID_PLATE);
    if (plate) {
        plate->var[0] = fs->g_cursor;
    }
}

/* vmu_card_grid_cursor_cb 0x8C01FE8C */
static void
grid_cb(bfs* fs, int code, int* pos) {
    int dir = -1;
    if (code == BFS_KEY_B) {
        sfx(fs, 2);
        fs->g_state = 3;
        fs->g_last = (int16_t)*pos;
        fs->g_result = -1;
    } else if (code == BFS_KEY_A) {
        fs->g_state = 3;
        fs->g_last = (int16_t)*pos;
        if (*pos == 8) {
            sfx(fs, 2);
            fs->g_result = -1;
        } else {
            sfx(fs, 1);
            fs->g_result = *pos + 1;
        }
    } else if (code >= BFS_KEY_UP && code <= BFS_KEY_RIGHT) {
        dir = code - 1;
    } else if (code == 7 && *pos < 8 && !card_usable(fs, *pos)) {
        dir = 8;
        *pos = 8;
        fs->g_cursor = 8;
    }
    if (dir >= 0) {
        sfx(fs, 0);
        int at = fs->g_cursor * 9, next = 8;
        if (dir != 8 && fs->grid_search) {
            for (int k = 0; k < 9; k++) {
                next = (signed char)fs->grid_search[at + k + dir * 0x51];
                if (next == 8 || (next >= 0 && next < 8 && grid_pick_ok(fs, next))) {
                    break;
                }
            }
        }
        *pos = next;
        fs->g_cursor = next;
        grid_select(fs);
    }
    bvm_obj* back = obj(fs, ID_BACK);
    if (back) {
        back->var[0] = fs->g_cursor >= 8;
    }
    grid_arrow(fs);
}

static void
grid_create(bfs* fs) {
    create(fs, 0xC, ID_FRAME, PRIO);
    for (int p = 0; p < 4; p++) {
        create(fs, 0xE + p, ID_PAD(p), PRIO);
    }
    create(fs, 0x15, ID_PLATE, PRIO);
    create(fs, 6, ID_BACK, PRIO);
    create(fs, 0x29, ID_ARROW, PRIO);
    static const uint16_t faded[] = {ID_FRAME, ID_BACK, ID_PLATE, ID_ARROW, ID_PAD(0), ID_PAD(1), ID_PAD(2), ID_PAD(3)};
    for (unsigned k = 0; k < sizeof(faded) / sizeof(faded[0]); k++) {
        flag17(fs, faded[k], 1);
    }
    bvm_obj* back = obj(fs, ID_BACK);
    if (back) {
        back->pos_tw[0].cur = -21.09375f;
        back->pos_tw[1].cur = -13.67188f;
    }
    for (int i = 0; i < 8; i++) {
        bvm_obj* c = create(fs, 0x12, ID_CARD(i), PRIO);
        flag17(fs, ID_CARD(i), 1);
        if (c) {
            c->pos_tw[0].cur = (float)card_x(fs, i) / 256.0f;
            c->pos_tw[1].cur = (float)card_y(fs, i) / 256.0f;
            c->model = (uint16_t)MODEL_CARD(i);
            c->texlist = c->model;
            c->motion = 0xD;
            c->var[0] = i;
        }
        fs->card_sig[i] = 0;
    }
    fs->plate_sig = -2;
    if (fs->g_last < 0 || fs->g_last > 7) {
        fs->g_last = 0;
    }
    int cursor = 8;
    if (fs->card[fs->g_last].status == BFS_CARD_READY && (fs->src != fs->g_last || !fs->g_dest)) {
        cursor = fs->g_last;
    } else {
        for (int i = 7; i >= 0; i--) {
            if (fs->card[i].status == BFS_CARD_READY && (fs->src != i || !fs->g_dest)) {
                cursor = i;
            }
        }
    }
    cursor_install(fs, 0, grid_table, 9, cursor, grid_cb);
    fs->g_cursor = cursor;
    grid_select(fs);
    grid_arrow(fs);
    fly_in(fs);
}

static void
grid_objects_flag17(bfs* fs, int on) {
    static const uint16_t ids[] = {ID_FRAME, ID_PAD(0), ID_PAD(1), ID_PAD(2), ID_PAD(3), ID_BACK, ID_PLATE, ID_ARROW};
    for (unsigned k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
        flag17(fs, ids[k], on);
    }
    for (int i = 0; i < 8; i++) {
        flag17(fs, ID_CARD(i), on);
    }
}

/* Returns 0 while it runs, -1 (BACK) or the card + 1. param 1 picks the card under the cursor at once. */
static int
card_grid(bfs* fs, int param) {
    switch (fs->g_state) {
        case 0:
            if (fs->changed > 0) {
                fs->g_result = -1;
                cursor_off(fs);
                fs->g_state = 4;
                return fs->g_result;
            }
            grid_create(fs);
            fs->g_state = 1;
            break;
        case 1:
            if (transition_busy(fs) == 0) {
                grid_objects_flag17(fs, 0);
                count_cards(fs);
                progress_dialog(fs, 0);
                fs->g_state = 2;
            }
            break;
        case 2:
            if (fs->changed < 1) {
                if (count_cards(fs)) {
                    progress_dialog(fs, 0);
                }
                if (param == 1) {
                    fs->g_result = fs->g_cursor + 1;
                    fs->g_state = 3;
                }
                return 0;
            }
            cursor_off(fs);
            fs->g_state = 6;
            break;
        case 3:
            cursor_off(fs);
            fs->g_state = 4;
            return fs->g_result;
        case 4:
            if (!(fs->changed & 1)) {
                cursor_install(fs, 0, grid_table, 9, fs->g_cursor, grid_cb);
                count_cards(fs);
                progress_dialog(fs, 0);
                fs->g_state = 2;
                return 0;
            }
            if (fs->changed & 2) {
                fs->g_result = -1;
                cursor_off(fs);
                fs->g_state = 4;
                return fs->g_result;
            }
            cursor_off(fs);
            fs->g_state = 6;
            break;
        case 6: {
            int r = bfs_message_box_(fs, 0xD3);
            if (r == -1) {
                return 0;
            }
            fs->g_result = -1;
            cursor_off(fs);
            fs->g_state = 4;
            return fs->g_result;
        }
        default: break;
    }
    return 0;
}

/* file_screen_leave 0x8C01FD38 */
static int
grid_leave(bfs* fs) {
    if (fs->g_state == 4) {
        fly_out(fs);
        grid_objects_flag17(fs, 1);
        cursor_off(fs);
        fs->g_state = 5;
    } else if (fs->g_state == 5 && transition_busy(fs) == 0) {
        static const uint16_t ids[] = {ID_FRAME, ID_PAD(0), ID_PAD(1), ID_PAD(2), ID_PAD(3), ID_BACK, ID_PLATE, ID_ARROW};
        for (unsigned k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            destroy(fs, ids[k]);
        }
        for (int i = 0; i < 8; i++) {
            destroy(fs, ID_CARD(i));
        }
        return -1;
    }
    return 0;
}

/* file_zoom_card_anim 0x8C018B20: a card outline flies from position `from` to `to` (8 = the browser's card) */
static void
zoom(bfs* fs, int from, int to) {
    if (fs->changed < 1) {
        bvm_obj* z = create(fs, 7, ID_ZOOM, 0x4000);
        if (z) {
            z->var[0] = card_x(fs, from);
            z->var[1] = card_y(fs, from);
            z->var[2] = card_x(fs, to);
            z->var[3] = card_y(fs, to);
        }
    }
}

/* ---- popups: popup_list_choose 0x8C010DB0, message_box 0x8C011560 --------------------------------------- */

static void
popup_mark(bfs* fs, int n, int sel) {
    for (int i = 0; i < n; i++) {
        bvm_obj* b = obj(fs, ID_POPBTN(i));
        if (b) {
            b->var[1] = i == sel;
        }
    }
}

/* popup_list_cursor_cb 0x8C0113A2 */
static void
popup_cb(bfs* fs, int code, int* pos) {
    const bfs_popup_def* d = fs->p_def;
    int moved = 0;
    if (code == BFS_KEY_B) {
        sfx(fs, 2);
        fs->p_result = d->cancel;
        fs->p_state = 3;
    } else if (code == BFS_KEY_A) {
        sfx(fs, *pos == d->count - 1 ? 2 : 1);
        fs->p_result = *pos;
        fs->p_state = 3;
    } else if (code == BFS_KEY_UP) {
        sfx(fs, 0);
        if (d->item[*pos].grey == 1) {
            *pos = *pos == 0 ? d->count - 1 : *pos - 1;
        }
        moved = 1;
    } else if (code == BFS_KEY_DOWN) {
        sfx(fs, 0);
        if (d->item[*pos].grey == 1) {
            *pos = (*pos + 1) % d->count;
        }
        moved = 1;
    } else if (code == BFS_KEY_LEFT || code == BFS_KEY_RIGHT) {
        moved = 1;
    }
    if (moved) {
        popup_mark(fs, d->count, *pos);
    }
}

static void
popup_close_objects(bfs* fs, int n) {
    destroy(fs, ID_POPUP);
    for (int i = 0; i < n; i++) {
        destroy(fs, ID_POPBTN(i));
    }
    fs->layer = 0;
    fs->fly = 0;
    fs->fly_alpha = 0;
}

/* Returns -1 while open, else the choice. `init` is the choice the cursor starts on. */
static int
popup(bfs* fs, const bfs_popup_def* d, int init) {
    if (fs->changed > 0) { /* FUN_8C010DA8: a card was pulled: close at once with "cancel" */
        if (fs->p_state != 0) {
            popup_close_objects(fs, d->count);
        }
        fs->p_state = 0;
        return d->cancel;
    }
    switch (fs->p_state) {
        case 0: {
            fs->p_def = d;
            bvm_obj* pnl = create(fs, 5, ID_POPUP, 0x5010);
            if (!pnl) {
                return -1;
            }
            pnl->flags |= BVM_F_FLAG17;
            pnl->var[0] = d->w;
            pnl->var[1] = d->h;
            pnl->var[2] = d->tw;
            pnl->var[3] = d->th;
            pnl->pos_tw[0].cur = (float)d->px / 256.0f;
            pnl->pos_tw[1].cur = (float)d->py / 256.0f;
            pnl->pos_tw[2].cur = (float)d->pz / 256.0f;
            pnl->text_w = d->tw;
            pnl->text_h = d->th;
            bsurf* s = surf(fs, ID_POPUP, d->tw, d->th);
            if (s) {
                bsurf_clear(s);
                s->advance = 10;
            }
            int n = d->count;
            int step_text = d->spacing * 24 / (n + 1), step_btn = d->spacing * 23 / (n + 1);
            for (int i = 0; i < n; i++) {
                bvm_obj* b = create(fs, 4, ID_POPBTN(i), 0x5000);
                if (b) {
                    b->flags |= BVM_F_FLAG17 | BVM_F_ATTACHED;
                    b->var[0] = 6;
                    b->parent = pnl;
                    b->pos_tw[0].cur = (float)(d->bx + d->w * -10 + 800) / 256.0f + 2.0f;
                    b->pos_tw[1].cur = (float)((d->by - i * step_btn) + ((n - 1) * step_btn) / 2) / 256.0f;
                    b->pos_tw[2].cur = 0.1953125f;
                    b->var[1] = 0;
                }
                popup_table[i][0] = (signed char)((i + n - 1) % n);
                popup_table[i][1] = (signed char)((i + 1) % n);
                popup_table[i][2] = popup_table[i][3] = (signed char)i;
                if (s) {
                    s->colour = d->item[i].grey ? 0xF777 : 0xFCCC;
                    int y = d->ty + (i * step_text - ((n - 1) * step_text) / 2) / 0x15;
                    bsurf_print(s, d->tx, y, msg(fs, d->item[i].msg, 0));
                }
            }
            cursor_install(fs, 1, (const signed char(*)[4])popup_table, n, init, popup_cb);
            popup_mark(fs, n, init);
            fs->p_state = 1;
            fly_in(fs);
            break;
        }
        case 1:
            if (transition_busy(fs) == 0) {
                fs->p_state = 2;
            }
            break;
        case 3:
            fly_out(fs);
            fs->p_state = 4;
            break;
        case 4:
            if (transition_busy(fs) == 0) {
                popup_close_objects(fs, d->count);
                fs->p_state = 0;
                return fs->p_result;
            }
            break;
        default: break;
    }
    return -1;
}

static void
msgbox_cb(bfs* fs, int code, int* pos) {
    if (code == BFS_KEY_A || code == BFS_KEY_B) {
        sfx(fs, 1);
        fs->m_state = 3;
        fs->m_result = *pos;
    }
}

int
bfs_message_box_(bfs* fs, int id) {
    switch (fs->m_state) {
        case 0: {
            bvm_obj* pnl = create(fs, 5, ID_POPUP, 0x5010);
            if (!pnl) {
                return -1;
            }
            pnl->flags |= BVM_F_FLAG17;
            pnl->var[0] = 0x200;
            pnl->var[1] = 200;
            pnl->var[2] = 0x200;
            pnl->var[3] = 0x80;
            pnl->pos_tw[0].cur = 0.0f;
            pnl->pos_tw[1].cur = 0.0f;
            pnl->pos_tw[2].cur = -332.0312f;
            pnl->text_w = 0x200;
            pnl->text_h = 0x80;
            bsurf* s = surf(fs, ID_POPUP, 0x200, 0x80);
            if (s) {
                bsurf_clear(s);
                for (int l = 0; l < 4; l++) {
                    bsurf_print(s, -256, 1 + l * 25, msg(fs, id, l));
                }
            }
            bvm_obj* b = create(fs, 4, ID_POPBTN(0), 0x5000);
            if (b) {
                b->flags |= BVM_F_FLAG17 | BVM_F_ATTACHED;
                b->var[0] = 6;
                b->parent = pnl;
                b->pos_tw[0].cur = 0.0f;
                b->pos_tw[1].cur = -5.078125f;
                b->pos_tw[2].cur = 0.390625f;
                b->var[1] = 1;
            }
            cursor_install(fs, 1, single_table, 1, 0, msgbox_cb);
            fs->m_state = 1;
            fly_in(fs);
            break;
        }
        case 1:
            if (transition_busy(fs) == 0) {
                fs->m_state = 2;
                if (id == 0xD3) {
                    sfx(fs, 6);
                }
            }
            break;
        case 3:
            fly_out(fs);
            fs->m_state = 4;
            break;
        case 4:
            if (transition_busy(fs) == 0) {
                popup_close_objects(fs, 1);
                fs->m_state = 0;
                return fs->m_result;
            }
            break;
        default: break;
    }
    return -1;
}

/* ---- the file browser ----------------------------------------------------------------------------------- */

static bfs_file*
file_at(bfs* fs, int pos) { /* the file at display position `pos` of the browsed card */
    bfs_card* c = &fs->card[fs->b_card];
    if (pos < 0 || pos >= c->nfiles) {
        return NULL;
    }
    return &c->files[fs->order[pos]];
}

static int
bytes_cmp(const char* a, const char* b, int n) { /* FUN_8C01E52A */
    for (int i = 0; i < n; i++) {
        if ((uint8_t)a[i] < (uint8_t)b[i]) return -1;
        if ((uint8_t)a[i] > (uint8_t)b[i]) return 1;
    }
    return 0;
}

/* FUN_8C01E340: by name, then (stable) by the application that made them */
static void
sort_files(bfs* fs) {
    bfs_card* c = &fs->card[fs->b_card];
    int n = c->nfiles > BFS_MAX_FILES ? BFS_MAX_FILES : c->nfiles;
    for (int i = 0; i < n; i++) {
        fs->order[i] = i;
    }
    for (int pass = 0; pass < 2; pass++) {
        int i = 0;
        while (i < n - 1) {
            const bfs_file* a = &c->files[fs->order[i]];
            const bfs_file* b = &c->files[fs->order[i + 1]];
            int r = pass == 0 ? bytes_cmp(a->name, b->name, 12) : bytes_cmp(a->app, b->app, 16);
            if (r < 1) {
                i++;
            } else {
                int t = fs->order[i];
                fs->order[i] = fs->order[i + 1];
                fs->order[i + 1] = t;
                i = i < 1 ? i + 1 : i - 1;
            }
        }
    }
}

static int
browsed_count(bfs* fs) {
    int n = fs->card[fs->b_card].nfiles;
    return n > BFS_MAX_FILES ? BFS_MAX_FILES : n;
}

/* FUN_8C01DD92 / FUN_8C01DCE0: page, and the scroll marks */
static void
set_page(bfs* fs, int page) {
    fs->b_page = page;
    bvm_obj* u = obj(fs, ID_UP);
    bvm_obj* d = obj(fs, ID_DOWN);
    if (u) {
        u->var[0] = page >= 1;
    }
    if (d) {
        d->var[0] = browsed_count(fs) - page * 8 - 24 >= 1;
    }
    fs->b_redraw = 1;
}

static void
clear_marks(bfs* fs) {
    memset(fs->marked, 0, sizeof(fs->marked));
    set_page(fs, fs->b_page);
}

/* format_datetime_string 0x8C01BAE0 */
static void
format_date(const bfs* fs, char* out, size_t size, int y, int mo, int d, int h, int mi) {
    if (fs->language == 0 || fs->date_order == 0) {
        snprintf(out, size, "%04d/%02d/%02d %02d:%02d", y, mo, d, h, mi);
    } else if (fs->language == 2) {
        snprintf(out, size, "%02d.%02d.%04d %02d:%02d", d, mo, y, h, mi);
    } else if (fs->date_order == 2 || fs->language != 1) {
        snprintf(out, size, "%02d/%02d/%04d %02d:%02d", d, mo, y, h, mi);
    } else {
        snprintf(out, size, "%02d/%02d/%04d %02d:%02d", mo, d, y, h, mi);
    }
}

static int
bcd(uint8_t b) {
    return (b >> 4) * 10 + (b & 15);
}

/* vmu_block_totals_draw 0x8C01F060 */
static void
block_totals(bfs* fs, bsurf* s) {
    int total = 0;
    const bfs_file* last = NULL;
    fs->protected_shown = 0;
    for (int i = 0; i < browsed_count(fs); i++) {
        if (fs->marked[i]) {
            last = file_at(fs, i);
            total += last->blocks;
            if (last->protect == 0xFF) {
                fs->protected_shown = 1;
            }
        }
    }
    bsurf_clear(s);
    s->advance = 11;
    s->colour = 0xFCCC;
    if (fs->b_cursor != 0 && last) {
        bsurf_print(s, 0x1C, 0x1E, last->desc);
    }
    s->advance = 12;
    s->colour = 0xFCCC;
    bsurf_print(s, 0x22, 0x5A, msg(fs, 0x38B, 0));
    bsurf_print(s, 0xF8, 0x5A, msg(fs, 0x385, 0));
    bsurf_print_number(s, 0xBE, 0x5A, total, -4);
}

/* vmu_file_info_draw 0x8C01EB7C: the file under the cursor, in the window's lower text surface */
static void
file_info(bfs* fs, int pos) {
    bsurf* s = surf(fs, ID_INFO, 512, 128);
    if (!s) {
        return;
    }
    fs->protected_shown = 0;
    if (fs->b_same != 0 || fs->b_cursor == 0) {
        block_totals(fs, s);
        return;
    }
    const bfs_file* f = file_at(fs, pos);
    if (pos < 0 || !f || fs->card[fs->b_card].status != BFS_CARD_READY) {
        bsurf_clear(s);
        return;
    }
    bsurf_clear(s);
    char text[64];
    s->advance = 11;
    s->colour = 0xFCCC;
    bsurf_print(s, 0x1C, 0x1E, f->desc);
    s->advance = 12;
    bsurf_print(s, 0x1C, 0x3C, f->name);
    bsurf_print(s, 0xC4, 0x3C, f->vmdesc);
    format_date(fs, text, sizeof(text), bcd(f->time[0]) * 100 + bcd(f->time[1]), bcd(f->time[2]), bcd(f->time[3]),
                bcd(f->time[4]), bcd(f->time[5]));
    bsurf_print_number(s, 0xE8, 0x5A, f->blocks, -4);
    bsurf_print(s, 0x1C, 0x5A, text);
    bsurf_print(s, 0x124, 0x5A, msg(fs, 0x385, 0));
    bsurf_print(s, 0x19A, 0x1E, f->game ? "GAME" : "DATA");
    /* the eyecatch, 72x56 at (398, 58) */
    int fi = fs->order[pos];
    if (fs->eye_slot != fs->b_card || fs->eye_file != fi) {
        fs->eye_slot = fs->b_card;
        fs->eye_file = fi;
        fs->eye_ok = f->eyecatch && fs->host.eyecatch && fs->host.eyecatch(fs->host.user, fs->b_card, fi, fs->eye) == 0;
    }
    for (int y = 0; y < 56; y++) {
        for (int x = 0; x < 72; x++) {
            s->px[(58 + y) * 512 + 398 + x] = fs->eye_ok ? fs->eye[y * 72 + x] : 0;
        }
    }
    bsurf_touch(s, 58, 113);
}

/* the tiles of vmu_info_panel(0), redrawn only where something changed */
static uint32_t tile_sig[24];

static void
draw_tiles(bfs* fs) {
    bsurf* s = surf(fs, ID_TILES, 512, 256);
    if (!s) {
        return;
    }
    if (fs->card[fs->b_card].status != BFS_CARD_READY) {
        if (tile_sig[0] != 0xFFFFFFFFu) {
            bsurf_clear(s);
            for (int t = 0; t < 24; t++) {
                tile_sig[t] = 0xFFFFFFFFu;
            }
        }
        return;
    }
    if (fs->b_redraw) {
        for (int t = 0; t < 24; t++) {
            tile_sig[t] = 0xFFFFFFFEu;
        }
        fs->b_redraw = 0;
    }
    int n = browsed_count(fs);
    for (int t = 0; t < 24; t++) {
        int pos = fs->b_page * 8 + t;
        int rx = (t & 7) * 0x30, ry = (t >> 3) * 0x36;
        if (pos >= n) {
            if (tile_sig[t] != 1u) {
                bsurf_fill(s, rx + 0x4C, ry + 0x60, 40, 50, 0x4444);
                tile_sig[t] = 1u;
            }
            continue;
        }
        const bfs_file* f = file_at(fs, pos);
        int sel = fs->marked[pos] && fs->b_blink;
        uint32_t sig = ((uint32_t)fs->order[pos] << 12) ^ ((uint32_t)f->frame << 4) ^ ((uint32_t)sel << 3) ^ ((uint32_t)f->header << 1) ^
                       (fs->card[fs->b_card].serial << 20) ^ 0x80000000u;
        if (tile_sig[t] == sig) {
            continue;
        }
        tile_sig[t] = sig;
        uint16_t bg = 0xF000, fg = 0xFCCC;
        if (f->protect == 0xFF) {
            bg = 0xF600;
        }
        if (f->game) {
            bg = 0xF060;
        }
        if (sel) {
            fg = 0xF000;
            bg = 0xFCC0;
        }
        s->colour = fg;
        bsurf_fill(s, rx + 0x4C, ry + 0x60, 40, 50, bg);
        if (f->header == 2 && f->bitmaps && f->icons) {
            const uint8_t* b = f->bitmaps + (f->frame < f->icons ? f->frame : 0) * 512;
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    uint8_t v = b[(y * 32 + x) / 2];
                    s->px[(ry + 100 + y) * 512 + rx + 0x50 + x] = f->palette[(x & 1) ? (v & 15) : (v >> 4)];
                }
            }
        } else {
            bsurf_fill(s, rx + 0x50, ry + 100, 32, 32, 0);
        }
        s->advance = 11;
        bsurf_tiny_number(s, rx + 0x4E, ry + 0x85, f->blocks, -3);
    }
}

/* vmu_info_panel 0x8C01E556 */
static void
info_panel(bfs* fs, int what) {
    if (what == 0) {
        draw_tiles(fs);
        return;
    }
    bsurf* s = surf(fs, ID_WINDOW, 512, 128);
    if (!s) {
        return;
    }
    int id = -1;
    bsurf_clear(s);
    s->colour = 0xFCCC;
    if (what == 1) {
        if (fs->card[fs->b_card].status == BFS_CARD_UNFORMATTED) {
            id = 0xC9;
        } else {
            file_info(fs, fs->b_cursor < 2 ? -1 : fs->b_page * 8 + fs->b_cursor - 2);
            if (fs->b_mode != 0) {
                id = 0x6E;
            } else if (browsed_count(fs) == 0) {
                bsurf_print(s, -256, 0x3C, msg(fs, 0x386, 0));
                return;
            } else {
                id = 0x66;
            }
        }
    } else if (what == 2) {
        id = fs->mode == 0 ? 0xCA : 0xDC;
    } else if (what == 3) {
        id = 0x38E;
    }
    if (id >= 0) {
        bsurf_print(s, -256, 0x19, msg(fs, id, 0));
        bsurf_print(s, -256, 0x32, msg(fs, id, 1));
        bsurf_print(s, -256, 0x4B, msg(fs, id, 2));
    }
}

static void
br_marks_after_move(bfs* fs, int* pos) {
    int n = browsed_count(fs);
    if (*pos == 0) {
        for (int i = 0; i < n; i++) {
            fs->marked[i] = 1;
        }
    } else {
        memset(fs->marked, 0, sizeof(fs->marked));
        if (*pos > 1) {
            if (n == 0) {
                *pos = 1;
            } else {
                fs->marked[fs->b_page * 8 + *pos - 2] = 1;
            }
        }
    }
    fs->b_blink = 1;
    fs->b_blink_t = 0x10;
    fs->b_same = 0;
    fs->b_redraw = 1;
    fs->b_dirty_info = 1;
}

/* vmu_file_browser_input 0x8C01DE32 (also called with 8 to set up, and 0x309 for an unformatted card) */
static void
browser_cb(bfs* fs, int code, int* pos) {
    int moved = 0;
    int n = browsed_count(fs);
    if (fs->card[fs->b_card].status == BFS_CARD_READING) {
        fs->b_result = -2;
        sfx(fs, 2);
        fs->b_state = 3;
        return;
    }
    if (code == 0x309) {
        fs->b_result = -3;
        fs->b_state = 3;
        return;
    }
    if (code == BFS_KEY_B) {
        fs->b_result = -2;
        sfx(fs, 2);
        fs->b_state = 3;
        return;
    }
    if (code == BFS_KEY_A) {
        fs->b_result = *pos;
        sfx(fs, *pos == 1 ? 2 : 1);
        fs->b_state = 3;
        return;
    }
    if (code == 8) {
        moved = 1;
    } else if (code == BFS_KEY_LEFT) {
        moved = 1;
        sfx(fs, 0);
        if (fs->b_cursor == 1 && n == 0) {
            *pos = 0;
        }
    } else if (code == BFS_KEY_RIGHT) {
        moved = 1;
        sfx(fs, 0);
        if (n > 0) {
            int last = n - fs->b_page * 8 - 1;
            if (fs->b_cursor == (last & 7) + (last >> 3) * 8 + 2) {
                *pos = 1;
            }
        }
    } else if (code == BFS_KEY_UP) {
        sfx(fs, 0);
        if (fs->b_cursor > 1 && fs->b_cursor < 10 && fs->b_page > 0) {
            set_page(fs, fs->b_page - 1);
        }
        moved = 1;
    } else if (code == BFS_KEY_DOWN) {
        sfx(fs, 0);
        if (fs->b_cursor > 0x11 && fs->b_cursor < 0x1A && n - fs->b_page * 8 - 0x18 > 0) {
            set_page(fs, fs->b_page + 1);
        }
        moved = 1;
    }
    if (*pos > 1 && n - fs->b_page * 8 <= *pos - 2) { /* past the last file: onto the last file */
        int last = n - fs->b_page * 8 - 1;
        *pos = last < 0 ? 1 : (last & 7) + (last >> 3) * 8 + 2;
    }
    if (moved) {
        br_marks_after_move(fs, pos);
    }
    fs->b_cursor = *pos;
    bvm_obj* back = obj(fs, ID_BACK);
    if (back) {
        back->var[0] = fs->b_cursor == 1;
    }
    bvm_obj* w = obj(fs, ID_WINDOW);
    if (w) {
        w->var[1] = fs->b_cursor == 0;
    }
}

static void
browser_open(bfs* fs, int mode, int card) {
    fs->b_result = 0;
    fs->b_same = 0;
    fs->b_mode = mode;
    fs->b_card = card;
    fs->b_state = 0;
}

static void
browser_objects_flag17(bfs* fs, int on) {
    static const uint16_t ids[] = {ID_WINDOW, ID_INFO, ID_TILES, ID_UP, ID_DOWN, ID_BACK, ID_PLATE, ID_CARD(0)};
    for (unsigned k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
        flag17(fs, ids[k], on);
    }
}

/* FUN_8C01F200: X / Y marks every file whose name starts with the same 9 characters */
static void
same_game(bfs* fs, int on, int t) {
    int n = browsed_count(fs);
    if (t < 0 || t >= n - fs->b_page * 8) {
        return;
    }
    memset(fs->marked, 0, sizeof(fs->marked));
    int pos = fs->b_page * 8 + t;
    if (!on) {
        fs->marked[pos] = 1;
        return;
    }
    const bfs_file* f = file_at(fs, pos);
    for (int i = 0; i < n; i++) {
        if (!memcmp(file_at(fs, i)->name, f->name, 9)) {
            fs->marked[i] = 1;
        }
    }
}

/* FUN_8C01F5A0: icon animation and the blink of the marked tiles */
static void
animate(bfs* fs) {
    bfs_card* c = &fs->card[fs->b_card];
    for (int i = 0; i < c->nfiles && i < BFS_MAX_FILES; i++) {
        bfs_file* f = &c->files[i];
        if (f->header == 2 && --f->wait < 1) {
            f->wait = f->speed > 0 ? f->speed : 1;
            f->frame++;
            if (f->frame >= f->icons) {
                f->frame = 0;
            }
        }
    }
    if (--fs->b_blink_t < 0) {
        fs->b_blink = 1 - fs->b_blink;
        fs->b_blink_t = 0x10;
    }
}

/* vmu_file_browser_update 0x8C01D5D8: -1 while it runs; -2 left (B or BACK chosen gives 1), -3 unformatted
 * card, 0 the card (all files), 2.. a file */
static int
browser(bfs* fs) {
    switch (fs->b_state) {
        case 0: {
            if (fs->changed > 0) {
                fs->b_result = -2;
                cursor_off(fs);
                fs->b_state = 4;
                return fs->b_result;
            }
            create(fs, 0x13, ID_WINDOW, 0x1010);
            create(fs, 0x28, ID_INFO, PRIO);
            create(fs, 0x14, ID_TILES, PRIO);
            create(fs, 0x38, ID_UP, PRIO);
            create(fs, 0x39, ID_DOWN, PRIO);
            create(fs, 6, ID_BACK, PRIO);
            create(fs, 0x52, ID_PLATE, PRIO);
            create(fs, 0x51, ID_CARD(0), PRIO);
            browser_objects_flag17(fs, 1);
            bvm_obj* back = obj(fs, ID_BACK);
            if (back) {
                back->pos_tw[0].cur = -21.48438f;
                back->pos_tw[1].cur = -13.67188f;
            }
            bvm_obj* c = obj(fs, ID_CARD(0));
            if (c) {
                c->pos_tw[0].cur = (float)card_x(fs, 8) / 256.0f;
                c->pos_tw[1].cur = (float)card_y(fs, 8) / 256.0f;
                c->var[0] = fs->b_card;
            }
            bvm_obj* p = obj(fs, ID_PLATE);
            if (p) {
                p->var[0] = fs->b_card;
            }
            fs->card_sig[0] = 0;
            fs->plate_sig = -2;
            fs->browse_card = fs->b_card;
            sort_files(fs);
            memset(fs->marked, 0, sizeof(fs->marked));
            if (browsed_count(fs) == 0 && fs->b_cursor > 1) {
                fs->b_cursor = 0;
            }
            for (int t = 0; t < 24; t++) {
                tile_sig[t] = 0xFFFFFFFEu;
            }
            fs->eye_slot = fs->eye_file = -1;
            cursor_install(fs, 0, browser_table, 26, fs->b_cursor, browser_cb);
            fly_in(fs);
            fs->b_state = 1;
            break;
        }
        case 1:
            if (transition_busy(fs) == 0) {
                browser_objects_flag17(fs, 0);
                clear_marks(fs);
                if (browsed_count(fs) >= 1) {
                    fs->marked[fs->b_page * 8] = 1;
                }
                info_panel(fs, 0);
                info_panel(fs, 1);
                fs->b_blink = 1;
                fs->b_blink_t = 0x10;
                int pos = fs->b_cursor;
                browser_cb(fs, 8, &pos);
                fs->cur[0].pos = pos;
                fs->b_dirty_info = 1;
                fs->b_state = 2;
                if (fs->card[fs->b_card].status == BFS_CARD_UNFORMATTED) {
                    browser_cb(fs, 0x309, &pos);
                }
            }
            break;
        case 2:
            if (fs->changed < 1) {
                if (browsed_count(fs) <= fs->b_page * 8) {
                    set_page(fs, 0);
                }
                animate(fs);
                int xy = fs->key == BFS_KEY_X || fs->key == BFS_KEY_Y;
                if (xy) {
                    fs->key = 0;
                }
                if (xy && fs->b_cursor > 1) {
                    sfx(fs, 0);
                    fs->b_same = 1 - fs->b_same;
                    same_game(fs, fs->b_same, fs->b_cursor - 2);
                    fs->b_redraw = 1;
                    fs->b_dirty_info = 1;
                }
                info_panel(fs, 0);
                if (fs->b_dirty_info) {
                    info_panel(fs, 1);
                    fs->b_dirty_info = 0;
                }
            } else {
                cursor_off(fs);
                fs->b_state = 6;
            }
            break;
        case 3:
            fs->b_blink = 1;
            fs->b_redraw = 1;
            info_panel(fs, 0);
            info_panel(fs, 1);
            cursor_off(fs);
            fs->b_state = 4;
            return fs->b_result;
        case 4: {
            sort_files(fs);
            clear_marks(fs);
            cursor_install(fs, 0, browser_table, 26, fs->b_cursor, browser_cb);
            int pos = fs->b_cursor;
            browser_cb(fs, 8, &pos);
            fs->cur[0].pos = pos;
            fs->b_dirty_info = 1;
            fs->b_state = 2;
            break;
        }
        case 6:
            if (bfs_message_box_(fs, 0xD3) != -1) {
                fs->changed |= 2;
                fs->b_result = -2;
                cursor_off(fs);
                fs->b_state = 4;
                return fs->b_result;
            }
            break;
        default: break;
    }
    return -1;
}

/* file_return_to_card_grid 0x8C01DBD6 */
static int
browser_leave(bfs* fs) {
    if (fs->b_state == 0) {
        return -1;
    }
    if (fs->b_state == 4) {
        fly_out(fs);
        browser_objects_flag17(fs, 1);
        cursor_off(fs);
        fs->b_state = 5;
    } else if (fs->b_state == 5 && transition_busy(fs) == 0) {
        static const uint16_t ids[] = {ID_WINDOW, ID_INFO, ID_TILES, ID_UP, ID_DOWN, ID_BACK, ID_PLATE, ID_CARD(0)};
        for (unsigned k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            destroy(fs, ids[k]);
        }
        fs->b_state = 0;
        fs->browse_card = -1;
        return -1;
    }
    return 0;
}

/* ---- the lists of files a command works on (FUN_8C018880 / 8C018940 / 8C0189BC / 8C018A40) -------------- */

static void
list_marked(bfs* fs, int with_protected) {
    fs->nlist = 0;
    for (int i = 0; i < browsed_count(fs); i++) {
        if (fs->marked[i] && (with_protected || file_at(fs, i)->protect != 0xFF)) {
            fs->list[fs->nlist++] = fs->order[i];
        }
    }
}

static void
list_all_copyable(bfs* fs) {
    fs->nlist = 0;
    for (int i = 0; i < browsed_count(fs); i++) {
        if (file_at(fs, i)->protect != 0xFF) {
            fs->list[fs->nlist++] = fs->order[i];
        }
    }
}

static int
count_copyable(bfs* fs) {
    int n = 0;
    for (int i = 0; i < browsed_count(fs); i++) {
        n += file_at(fs, i)->protect != 0xFF;
    }
    return n;
}

/* vmu_copy_precheck 0x8C022FA0: 0 fine, 1 nothing to copy, 2 destination not ready, 3 a VMU game on both,
 * 4 not enough room, 5 files of the same name will be overwritten */
static int
copy_precheck(bfs* fs) {
    if (fs->nlist == 0) {
        return 1;
    }
    const bfs_card* s = &fs->card[fs->src];
    const bfs_card* d = &fs->card[fs->dst];
    if (d->status != BFS_CARD_READY) {
        return 2;
    }
    int games = 0, dgames = 0, need = 0, room = d->free_blocks, same = 0;
    for (int k = 0; k < fs->nlist; k++) {
        games += s->files[fs->list[k]].game;
    }
    for (int i = 0; i < d->nfiles; i++) {
        dgames += d->files[i].game;
    }
    if (games && dgames) {
        return 3;
    }
    for (int k = 0; k < fs->nlist; k++) {
        const bfs_file* f = &s->files[fs->list[k]];
        need += f->blocks;
        for (int i = 0; i < d->nfiles; i++) { /* FUN_8C023180: a file of that name gives its blocks back */
            if (!memcmp(d->files[i].name, f->name, 12)) {
                room += d->files[i].blocks;
                same = 1;
            }
        }
    }
    if (room < need) {
        return 4;
    }
    return same ? 5 : 0;
}

static void
start_op(bfs* fs, int kind, int src, int dst) {
    memset(&fs->op, 0, sizeof(fs->op));
    fs->op.kind = kind;
    fs->op.src = src;
    fs->op.dst = dst;
    memcpy(fs->op.files, fs->list, sizeof(int) * (size_t)fs->nlist);
    fs->op.nfiles = fs->nlist;
}

bfs_op*
bfs_pending(bfs* fs) {
    return fs->op.kind != BFS_OP_NONE && !fs->op.done ? &fs->op : NULL;
}

/* ---- file_screen_update 0x8C017A60 ---------------------------------------------------------------------- */

void
bfs_init(bfs* fs, bmenu* m, const bfs_host* host, bfs_card cards[BFS_SLOTS]) {
    memset(fs, 0, sizeof(*fs));
    fs->m = m;
    fs->rom = m->rom;
    if (host) {
        fs->host = *host;
    }
    fs->card = cards;
    fs->language = 1;
    fs->date_order = 1;
    fs->ports = 1;
    fs->grid_search = bios_rom_ptr(fs->rom, 0x8C036FE0u, 4 * 81);
    fs->arrow_tab = bios_rom_ptr(fs->rom, 0x8C037124u, 64);
    bsurf_init(fs->rom);
}

void
bfs_open(bfs* fs) {
    bvm_kill_all(&fs->m->vm);
    bsurf_free_all();
    fs->changed = 0;
    fs->src_watch = fs->dst_watch = -1;
    fs->state = 0;
    fs->src = fs->dst = 0;
    fs->browse_card = -1;
    fs->g_state = 0;
    fs->b_state = 0;
    fs->p_state = fs->m_state = 0;
    fs->fly = 0;
    fs->fly_alpha = 0;
    fs->layer = 0;
    fs->cur[0].on = fs->cur[1].on = 0;
    fs->b_page = 0;
    fs->b_cursor = 2;
    fs->gauge_set = 0;
    fs->g_ready = -1;
    memset(&fs->op, 0, sizeof(fs->op));
    fs->m->scene.object_panel_accent = BMENU_ACCENT_FILES;
}

void
bfs_key(bfs* fs, int key) {
    fs->key = key;
}

static int
watched_gone(const bfs* fs, int i) {
    return i >= 0 && i < BFS_SLOTS && (fs->card[i].status == BFS_CARD_ERROR || fs->card[i].status == BFS_CARD_NONE);
}

int
bfs_update(bfs* fs) {
    int r;
    if (watched_gone(fs, fs->src_watch) || watched_gone(fs, fs->dst_watch)) {
        fs->changed |= 1;
    }
    if (fs->key == BFS_KEY_X || fs->key == BFS_KEY_Y) { /* X and Y are read by the browser itself */
        if (fs->state != 5 || fs->b_state != 2) {
            fs->key = 0;
        }
    } else {
        cursor_update(fs);
    }
    run_effects(fs);
    switch (fs->state) {
        case 0:
            fs->browse_card = -1;
            help_line(fs, 0x65, 0);
            fs->state = 1;
            break;
        case 1:
            fs->src_watch = fs->dst_watch = -1;
            fs->changed = 0;
            fs->browse_card = -1;
            r = card_grid(fs, 0);
            if (r == -1) {
                fs->state = 2;
            } else if (r != 0) {
                fs->src = r - 1;
                fs->src_watch = fs->src;
                zoom(fs, fs->src, 8);
                fs->state = 3;
            }
            break;
        case 2:
            if (grid_leave(fs)) {
                return 1;
            }
            break;
        case 3:
            if (grid_leave(fs)) {
                fs->b_page = 0;
                fs->b_cursor = 2;
                fs->state = 4;
            }
            break;
        case 4:
            fs->dst_watch = -1;
            browser_open(fs, 0, fs->src);
            fs->state = 5;
            break;
        case 5:
            r = browser(fs);
            if (r == -1) {
                break;
            }
            if (r == -3) {
                fs->state = 10;
                sfx(fs, 3);
            } else if (r == -2 || r == 1) {
                fs->state = 6;
            } else if (r == 0) {
                fs->state = fs->card[fs->src].nfiles != 0 && count_copyable(fs) ? 8 : 9;
            } else {
                list_marked(fs, 0);
                if (fs->nlist != 0) {
                    fs->state = 0xB;
                } else {
                    sfx(fs, 3);
                    fs->state = 0xC;
                    info_panel(fs, 3);
                }
            }
            break;
        case 6:
            fs->browse_card = -1;
            zoom(fs, 8, fs->src);
            fs->state = 7;
            break;
        case 7:
            if (browser_leave(fs)) {
                fs->g_last = fs->src;
                fs->state = 0;
            }
            break;
        case 8:
        case 9:
        case 10:
        case 0xB:
        case 0xC: {
            static const bfs_popup_def* const defs[5] = {&pop_card_all, &pop_card_nocopy, &pop_card_nocopy, &pop_files,
                                                         &pop_files_nocopy};
            static const int init[5] = {0, 2, 2, 0, 2};
            int k = fs->state - 8;
            r = popup(fs, defs[k], init[k]);
            if (r == -1) {
                break;
            }
            if (fs->state == 8 && r == 0) {
                fs->state = 0x2D; /* copy all */
            } else if ((fs->state == 0xB) && r == 0) {
                fs->state = 0xD; /* copy */
            } else if (r == 1 && fs->state <= 10) {
                fs->state = 0x30; /* memory reset */
                sfx(fs, 3);
            } else if (r == 1) {
                fs->state = 0x32; /* delete */
                sfx(fs, 3);
            } else if (r == 2) {
                fs->state = fs->state == 10 ? 6 : 5;
            }
            break;
        }
        case 0xD:
            zoom(fs, 8, fs->src);
            fs->state = 0xE;
            break;
        case 0xE:
            fs->browse_card = -1;
            if (browser_leave(fs)) {
                fs->mode = fs->nlist > 1;
                help_line(fs, 0x67, 1);
                fs->state = 0xF;
            }
            break;
        case 0xF:
            fs->dst_watch = -1;
            r = card_grid(fs, 0);
            if (r == -1) {
                fs->state = 0x10;
            } else if (r != 0) {
                fs->dst = r - 1;
                fs->dst_watch = fs->dst;
                fs->state = 0x12;
            }
            break;
        case 0x10:
            zoom(fs, fs->src, 8);
            fs->state = 0x11;
            break;
        case 0x11:
            if (grid_leave(fs)) {
                fs->state = 4;
            }
            break;
        case 0x12:
            r = popup(fs, &pop_dest, 0);
            if (r == 0) {
                fs->state = 0x14;
            } else if (r == 1) {
                fs->state = 0x26;
            } else if (r == 2) {
                fs->state = 0xF;
                fs->g_state = 4;
            }
            break;
        case 0x14:
            r = copy_precheck(fs);
            if (r == 0) {
                fs->state = 0x17;
            } else if (r == 1) {
                fs->state = 0xF;
                fs->g_state = 4;
            } else {
                fs->state = r == 2 ? 0x1D : (r == 3 ? 0x1E : (r == 4 ? 0x1F : 0x16));
                sfx(fs, r == 5 ? 3 : 4);
            }
            break;
        case 0x16:
            progress_dialog(fs, 2);
            r = popup(fs, &pop_yes_no, 1);
            if (r == 0) {
                fs->state = 0x17;
            } else if (r == 1) {
                progress_dialog(fs, 0);
                fs->state = 0xF;
                fs->g_state = 4;
            }
            break;
        case 0x17:
            if (fs->changed < 1) {
                start_op(fs, BFS_OP_COPY, fs->src, fs->dst);
                create(fs, 0x3A, ID_COPYBOX, PRIO);
                create(fs, 0x3B, ID_COPYBAR, PRIO);
                bvm_obj* bar = obj(fs, ID_COPYBAR);
                if (bar) {
                    bar->var[0] = 0;
                }
                flag17(fs, ID_COPYBOX, 1);
                flag17(fs, ID_COPYBAR, 1);
                fly_in(fs);
                fs->state = 0x18;
            } else {
                fs->state = 0x23;
            }
            break;
        case 0x18:
            if (transition_busy(fs) == 0) {
                progress_dialog(fs, 1);
                fs->state = 0x19;
            }
            break;
        case 0x19: {
            progress_dialog(fs, 3);
            bvm_obj* bar = obj(fs, ID_COPYBAR);
            if (bar) {
                bar->var[0] = fs->op.progress * 0xF00 / 1000;
            }
            if (fs->op.done) {
                destroy(fs, ID_COPYBOX);
                destroy(fs, ID_COPYBAR);
                fs->op.kind = BFS_OP_NONE;
                if (!fs->op.failed) {
                    sfx(fs, 1);
                    fs->state = fs->mode == 0 ? 0x1A : (fs->mode == 1 ? 0x1B : 0x1C);
                } else {
                    sfx(fs, 4);
                    if (fs->card[fs->src].status == BFS_CARD_READY && fs->card[fs->dst].status == BFS_CARD_READY) {
                        fs->state = fs->mode == 0 ? 0x20 : (fs->mode == 1 ? 0x21 : 0x22);
                    } else {
                        fs->state = 0x23;
                    }
                }
            }
            break;
        }
        case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        case 0x20: case 0x21: case 0x22: case 0x23: {
            static const int ids[10] = {0x68, 0x390, 0x6D, 0xCF, 0xCD, 0xCE, 0xD1, 0xD8, 0x391, 0xD3};
            if (bfs_message_box_(fs, ids[fs->state - 0x1A]) != -1) {
                if (fs->state >= 0x1D && fs->state <= 0x1F) {
                    fs->state = 0xF;
                    fs->g_state = 4;
                    progress_dialog(fs, 0);
                } else {
                    fs->state = 0x24;
                }
            }
            break;
        }
        case 0x24:
            zoom(fs, fs->src, 8);
            fs->state = 0x25;
            break;
        case 0x25:
            if (grid_leave(fs)) {
                fs->state = 4;
            }
            break;
        case 0x26: /* view the destination's files */
            zoom(fs, fs->dst, 8);
            fs->state = 0x27;
            break;
        case 0x27:
            if (grid_leave(fs)) {
                fs->b_saved_page = fs->b_page;
                fs->b_saved_cursor = fs->b_cursor;
                fs->b_page = 0;
                fs->b_cursor = 2;
                browser_open(fs, 1, fs->dst);
                fs->state = 0x28;
            }
            break;
        case 0x28:
            r = browser(fs);
            if (r != -1) {
                fs->state = 0x29;
            }
            break;
        case 0x29:
            fs->browse_card = -1;
            zoom(fs, 8, fs->dst);
            fs->state = 0x2A;
            break;
        case 0x2A:
            if (browser_leave(fs)) {
                help_line(fs, 0x67, 1);
                fs->b_page = fs->b_saved_page;
                fs->b_cursor = fs->b_saved_cursor;
                fs->g_last = fs->dst;
                fs->state = 0x2B;
            }
            break;
        case 0x2B:
            card_grid(fs, 0);
            if (fs->fly == 0 && fs->g_state == 2) {
                card_grid(fs, 1);
                fs->state = 0x2C;
            }
            break;
        case 0x2C:
            card_grid(fs, 0);
            fs->state = 0x12;
            break;
        case 0x2D: /* copy all */
            zoom(fs, 8, fs->src);
            fs->state = 0x2E;
            break;
        case 0x2E:
            if (browser_leave(fs)) {
                fs->mode = 2;
                list_all_copyable(fs);
                help_line(fs, 0x67, 1);
                fs->state = 0xF;
            }
            break;
        case 0x30: /* memory reset: the icon and colour pickers are not part of this screen yet */
            fs->mode = 2;
            fs->state = 0x31;
            break;
        case 0x31: {
            r = bfs_format_flow_(fs);
            if (r != 0) {
                fs->state = fs->card[fs->src].status == BFS_CARD_UNFORMATTED ? 6 : 5;
                if (fs->state == 5) {
                    fs->b_state = 4;
                }
            }
            break;
        }
        case 0x32:
            fs->mode = 0;
            list_marked(fs, 1);
            if (fs->nlist > 1) {
                fs->mode = 1;
            }
            info_panel(fs, 2);
            r = popup(fs, &pop_yes_no, 1);
            if (r == 0) {
                fs->state = 0x33;
            } else if (r == 1) {
                fs->state = 5;
            }
            break;
        case 0x33:
            if (fs->card[fs->src].status == BFS_CARD_READY) {
                start_op(fs, BFS_OP_DELETE, fs->src, -1);
                fs->state = 0x34;
            } else {
                fs->state = 5;
            }
            break;
        case 0x34:
            if (fs->op.done) {
                fs->op.kind = BFS_OP_NONE;
                if (!fs->op.failed) {
                    sfx(fs, 1);
                    sort_files(fs);
                    fs->state = fs->mode == 0 ? 0x35 : 0x36;
                } else {
                    sfx(fs, 4);
                    fs->state = fs->card[fs->src].status == BFS_CARD_READY ? (fs->mode == 0 ? 0x37 : 0x38) : 0x3A;
                }
            }
            break;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x3A: {
            int id = fs->state == 0x35 ? 0x69 : fs->state == 0x36 ? 0x397 : fs->state == 0x37 ? 0xD2 : fs->state == 0x38 ? 0xD9 : 0xD3;
            if (bfs_message_box_(fs, id) != -1) {
                fs->state = 5;
            }
            break;
        }
        default: break;
    }
    /* whatever fades with the screen transitions */
    fs->m->scene.fade = (float)fs->fly_alpha / 255.0f;
    return 0;
}

/* The memory reset (vmu_format_flow 0x8C01BF80): confirm, then reset with the card's current look. The BIOS also
 * offers its icon and colour pickers here; those follow. */
int
bfs_format_flow_(bfs* fs) {
    static int st;
    int r;
    switch (st) {
        case 0:
            r = popup(fs, &pop_yes_no, 1);
            if (r == 0) {
                st = 1;
            } else if (r == 1) {
                return -1;
            }
            {
                bsurf* s = surf(fs, ID_WINDOW, 512, 128);
                if (s && fs->p_state == 1) {
                    bsurf_clear(s);
                    s->colour = 0xFCCC;
                    for (int l = 0; l < 3; l++) {
                        bsurf_print(s, -256, 0x19 + l * 25, msg(fs, 0xD5, l));
                    }
                }
            }
            break;
        case 1:
            memset(&fs->op, 0, sizeof(fs->op));
            fs->op.kind = BFS_OP_FORMAT;
            fs->op.src = fs->src;
            fs->op.shape = -1;
            st = 2;
            break;
        case 2:
            if (fs->op.done) {
                fs->op.kind = BFS_OP_NONE;
                sfx(fs, fs->op.failed ? 4 : 1);
                st = fs->op.failed ? 4 : 3;
            }
            break;
        case 3:
        case 4:
            if (bfs_message_box_(fs, st == 3 ? 0x6B : 0xD6) != -1) {
                st = 0;
                return -1;
            }
            break;
    }
    return 0;
}

void
bfs_draw(bfs* fs, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = fs->m;
    bvm_obj* back = obj(fs, ID_BACK);
    fs->anim++;
    bmenu_back_style(m, back && back->var[0], fs->anim, fs->pal);
    /* the card colours (fx_vmu_icon_texture writes them into the card models' material) */
    for (int i = 0; i < BFS_SLOTS && m->scene.ovr_n < BSCENE_OVR_MAX; i++) {
        if (fs->card[i].status == BFS_CARD_READY || fs->card[i].status == BFS_CARD_UNFORMATTED) {
            m->scene.ovr[m->scene.ovr_n].model = MODEL_CARD(i);
            m->scene.ovr[m->scene.ovr_n].node = 1;
            m->scene.ovr[m->scene.ovr_n].poly = 0;
            m->scene.ovr[m->scene.ovr_n].argb =
                fs->card[i].status == BFS_CARD_UNFORMATTED ? 0xFF2F5F9Fu : fs->card[i].colour;
            m->scene.ovr[m->scene.ovr_n].lit = 1;
            m->scene.ovr_n++;
        }
    }
    window_colours(fs, &m->scene);
    /* The BIOS lets the PVR sort its translucent polygons by depth (autosort); this screen is drawn in presort
     * mode, so the objects go far to near, each with its text right after its model (a popup in front of the file
     * window covers the window's text too). Equal depths keep the BIOS order. */
    int idx[BVM_MAX_OBJECTS], n = 0;
    for (int i = 0; i < m->vm.count; i++) {
        idx[n++] = m->vm.order[i];
    }
    for (int i = 1; i < n; i++) {
        int k = idx[i], j = i - 1;
        float z = m->vm.objs[k].pos[2];
        while (j >= 0 && m->vm.objs[idx[j]].pos[2] > z) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = k;
    }
    (void)passes;
    m->scene.parts = BSCENE_PART_ALL;
    for (int i = 0; i < n; i++) {
        bscene_draw_object(&m->scene, &m->vm.objs[idx[i]], sink);
    }
    m->scene.parts = BSCENE_PART_ALL;
}
