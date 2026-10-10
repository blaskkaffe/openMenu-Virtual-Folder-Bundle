/* ui_files: see ui_files.h. The screen itself is the BIOS's (bios_filescr); this file feeds it the memory cards
 * and carries out what it asks for, one memory card access a frame so the screen keeps running. */
#include <stdlib.h>
#include <string.h>

#include <dc/maple.h>

#include <bios_audio.h>
#include <bios_filescr.h>
#include <bios_surface.h>

#include "clock.h"
#include "gfx.h"
#include "sound.h"
#include "ui_files.h"
#include "vmu_files.h"

static bfs fs;
static bfs_card cards[BFS_SLOTS];
static bfs_file files[BFS_SLOTS][BFS_MAX_FILES];
static vf_file vfiles[BFS_SLOTS][BFS_MAX_FILES];
static uint8_t bitmaps[BFS_MAX_FILES][3 * 512]; /* icons of the card being browsed */
static int header_card = -1;
static int need_read[BFS_SLOTS];
static int present[BFS_SLOTS];
static int poll_timer;
static int left;

static void
host_sfx(void* user, int id) {
    (void)user;
    sound_sfx(id);
}

static int
host_eyecatch(void* user, int slot, int file, uint16_t out[72 * 56]) {
    (void)user;
    if (slot < 0 || slot >= BFS_SLOTS || file < 0 || file >= cards[slot].nfiles) {
        return -1;
    }
    return vf_file_eyecatch(slot, &vfiles[slot][file], files[slot][file].eyecatch, files[slot][file].icons, out);
}

/* Root block, picture and directory of a card (several card accesses: done when the card is not in use). */
static void
read_card(int slot) {
    static vf_card_info info;
    bfs_card* c = &cards[slot];
    vf_card_info_read(slot, &info);
    c->status = info.status;
    c->colour = info.colour;
    memcpy(c->icon, info.icon, sizeof(c->icon));
    c->free_blocks = info.free_blocks;
    c->total_blocks = info.total_blocks;
    c->nfiles = 0;
    if (info.status == VF_CARD_READY) {
        int n = vf_list(slot, vfiles[slot], BFS_MAX_FILES);
        if (n < 0) {
            c->status = BFS_CARD_ERROR;
            n = 0;
        }
        for (int i = 0; i < n; i++) {
            bfs_file* f = &files[slot][i];
            const vf_file* v = &vfiles[slot][i];
            memset(f, 0, sizeof(*f));
            memcpy(f->name, v->name, sizeof(f->name));
            memset(f->vmdesc, ' ', 16);
            memset(f->desc, ' ', 32);
            memset(f->app, ' ', 16);
            f->blocks = v->blocks;
            memcpy(f->time, v->time, 8);
            f->protect = v->protect ? 0xFF : 0;
            f->game = (uint8_t)v->is_game;
        }
        c->nfiles = n;
    }
    c->files = files[slot];
    c->serial++;
    if (header_card == slot) {
        header_card = -1; /* the headers have to be read again */
    }
}

/* The VMS header of one file of the card being browsed (the BIOS reads them one after the other too). */
static int
read_one_header(void) {
    int slot = fs.browse_card;
    if (slot < 0 || slot >= BFS_SLOTS || cards[slot].status != BFS_CARD_READY) {
        return 0;
    }
    if (header_card != slot) {
        for (int i = 0; i < cards[slot].nfiles; i++) {
            files[slot][i].header = 0;
        }
        header_card = slot;
    }
    for (int i = 0; i < cards[slot].nfiles; i++) {
        bfs_file* f = &files[slot][i];
        if (f->header) {
            continue;
        }
        static vf_header h;
        if (vf_file_header(slot, &vfiles[slot][i], &h) == 0) {
            memcpy(f->vmdesc, h.vmdesc, sizeof(f->vmdesc));
            memcpy(f->desc, h.desc, sizeof(f->desc));
            memcpy(f->app, h.app, sizeof(f->app));
            f->icons = (uint8_t)h.icons;
            f->speed = (int16_t)(h.speed > 0 ? h.speed : 1);
            f->wait = 1;
            f->eyecatch = (uint8_t)h.eyecatch;
            memcpy(f->palette, h.palette, sizeof(f->palette));
            memcpy(bitmaps[i], h.bitmaps, sizeof(bitmaps[i]));
            f->bitmaps = bitmaps[i];
        } else {
            f->icons = 0;
            f->bitmaps = NULL;
        }
        f->header = 2;
        cards[slot].serial++;
        return 1;
    }
    return 0;
}

static int
controllers(void) {
    int mask = 0;
    for (int p = 0; p < 4; p++) {
        maple_device_t* d = maple_enum_dev(p, 0);
        if (d && d->valid) {
            mask |= 1 << p;
        }
    }
    return mask;
}

/* Cards come and go: what is in the sockets now. */
static void
poll_cards(void) {
    for (int i = 0; i < BFS_SLOTS; i++) {
        int now = vf_present(i);
        if (now && !present[i]) {
            cards[i].status = BFS_CARD_READING;
            cards[i].serial++;
            need_read[i] = 1;
        } else if (!now && present[i]) {
            cards[i].status = BFS_CARD_NONE;
            cards[i].nfiles = 0;
            cards[i].serial++;
            need_read[i] = 0;
            if (header_card == i) {
                header_card = -1;
            }
        }
        present[i] = now;
    }
    fs.ports = controllers();
}

/* The command the screen asked for, a file a frame. */
static void
run_op(bfs_op* op) {
    if (op->kind == BFS_OP_FORMAT) {
        vf_card_info info;
        vf_card_info_read(op->src, &info);
        int shape = op->shape >= 0 ? op->shape : (info.status == VF_CARD_READY ? info.shape : 0);
        unsigned int c = op->shape >= 0 ? op->colour : (info.status == VF_CARD_READY ? info.colour : 0xFFFFFFFFu);
        int custom = c != 0xFFFFFFFFu && c != 0;
        unsigned char bgra[4] = {(unsigned char)c, (unsigned char)(c >> 8), (unsigned char)(c >> 16), (unsigned char)(c >> 24)};
        op->failed = vf_format_raw(op->src, shape, custom, bgra) != 0;
        read_card(op->src);
        op->progress = 1000;
        op->done = 1;
        return;
    }
    if (op->current < op->nfiles) {
        int f = op->files[op->current];
        const vf_file* v = &vfiles[op->src][f];
        int rc;
        if (op->kind == BFS_OP_COPY) {
            rc = vf_copy(op->src, v, op->dst, 1) == VF_OK ? 0 : -1;
        } else {
            rc = vf_delete(op->src, v);
        }
        if (rc != 0) {
            op->failed = 1;
            op->current = op->nfiles;
        } else {
            op->current++;
        }
        op->progress = op->nfiles ? op->current * 1000 / op->nfiles : 1000;
        return;
    }
    /* finished: the cards as they are now */
    read_card(op->src);
    if (op->kind == BFS_OP_COPY && op->dst >= 0) {
        read_card(op->dst);
    }
    op->done = 1;
}

void
uif_set_pal(int pal) {
    fs.pal = pal;
}

void
uif_open(bmenu* m) {
    static const bfs_host host = {NULL, host_sfx, host_eyecatch};
    bfs_init(&fs, m, &host, cards);
    fs.date_order = clock_date_order();
    memset(present, 0, sizeof(present));
    for (int i = 0; i < BFS_SLOTS; i++) {
        cards[i].status = BFS_CARD_NONE;
        cards[i].files = files[i];
        cards[i].nfiles = 0;
        cards[i].serial++;
    }
    header_card = -1;
    poll_cards();
    poll_timer = 0;
    left = 0;
    bfs_open(&fs);
}

int
uif_handle(button_t b) {
    switch (b) {
        case BTN_UP: bfs_key(&fs, BFS_KEY_UP); break;
        case BTN_DOWN: bfs_key(&fs, BFS_KEY_DOWN); break;
        case BTN_LEFT: bfs_key(&fs, BFS_KEY_LEFT); break;
        case BTN_RIGHT: bfs_key(&fs, BFS_KEY_RIGHT); break;
        case BTN_A:
        case BTN_START: bfs_key(&fs, BFS_KEY_A); break;
        case BTN_B: bfs_key(&fs, BFS_KEY_B); break;
        case BTN_X: bfs_key(&fs, BFS_KEY_X); break;
        default: break;
    }
    return 0;
}

int
uif_step(void) {
    if (!left && bfs_update(&fs)) {
        left = 1;
        bsurf_free_all();
        fs.m->scene.fade = 0.0f;
        fs.m->scene.ovr_n = 0;
    }
    return left;
}

void
uif_sync(void) {
    if (left) {
        return;
    }
    bfs_op* op = bfs_pending(&fs);
    if (op) {
        run_op(op);
        return;
    }
    if (++poll_timer >= 30) {
        poll_timer = 0;
        poll_cards();
    }
    for (int i = 0; i < BFS_SLOTS; i++) { /* one card read a frame */
        if (need_read[i]) {
            need_read[i] = 0;
            read_card(i);
            return;
        }
    }
    read_one_header();
}

void
uif_draw(void) {
    bfs_draw(&fs, gfx_sink());
}

void
uif_hover(float x, float y) {
    (void)x;
    (void)y;
}
