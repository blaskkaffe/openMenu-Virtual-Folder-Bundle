/* ui_files: see ui_files.h. The screen itself is the BIOS's (bios_filescr); this file feeds it the memory cards
 * and carries out what it asks for, one memory card access a frame so the screen keeps running. */
#include <stdlib.h>
#include <string.h>

#include <dc/maple.h>
#include <kos/sem.h>
#include <kos/thread.h>

#include <bios_audio.h>
#include <bios_filescr.h>
#include <bios_surface.h>
#include <pad_kind.h>

#include "clock.h"
#include "serial_vmu.h"
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

/* ---- the card reader -------------------------------------------------------------------------------------------------
 * Reading a card is slow: every 512-byte block is a maple transaction of tens of milliseconds, a directory is 13 of them,
 * a file's header 4, an eyecatch a few more. Done in the frame, opening a card stopped the screen for seconds. The BIOS runs
 * these as a state machine in the background (vmu_update, 0x8C023520); here one worker thread does one read at a time and
 * the frame applies what it finished. */
enum { J_CARD = 1, J_HEADER, J_EYE };

static struct {
    volatile int state; /* 0 idle, 1 asked, 2 finished */
    int kind, slot, file;
    unsigned gen;
    vf_file vf;
    int eye_type, eye_icons;
    vf_card_info info;
    int n;
    vf_file list[BFS_MAX_FILES];
    vf_header hdr;
    int rc;
    uint16_t eye[72 * 56];
} job;
static unsigned slot_gen[BFS_SLOTS]; /* bumped when a card changes: an answer for an older state is thrown away */
static semaphore_t job_sem;
static kthread_t* job_thread;
static int eye_slot = -1, eye_file = -1, eye_state; /* the eyecatch asked for / ready (1 ok, -1 failed, 0 not yet) */
static uint16_t eye_px[72 * 56];

static void*
reader(void* param) {
    (void)param;
    for (;;) {
        sem_wait(&job_sem);
        if (job.state != 1) {
            continue;
        }
        if (job.kind == J_CARD) {
            vf_card_info_read(job.slot, &job.info);
            job.n = 0;
            if (job.info.status == VF_CARD_READY) {
                job.n = vf_list(job.slot, job.list, BFS_MAX_FILES);
            }
        } else if (job.kind == J_HEADER) {
            job.rc = vf_file_header(job.slot, &job.vf, &job.hdr);
        } else {
            job.rc = vf_file_eyecatch(job.slot, &job.vf, job.eye_type, job.eye_icons, job.eye);
        }
        job.state = 2;
    }
    return NULL;
}

static void
start_reader(void) {
    if (!job_thread) {
        sem_init(&job_sem, 0);
        job_thread = thd_create(1, reader, NULL);
        if (job_thread) {
            thd_set_prio(job_thread, PRIO_DEFAULT + 4); /* below the menu */
        }
    }
}

static void
ask(int kind, int slot, int file, const vf_file* vf) {
    job.kind = kind;
    job.slot = slot;
    job.file = file;
    job.gen = slot_gen[slot];
    if (vf) {
        job.vf = *vf;
    }
    job.state = 1;
    if (job_thread) {
        sem_signal(&job_sem);
    } else { /* no thread: do it here, as before */
        job.state = 1;
        if (kind == J_CARD) {
            vf_card_info_read(slot, &job.info);
            job.n = job.info.status == VF_CARD_READY ? vf_list(slot, job.list, BFS_MAX_FILES) : 0;
        } else if (kind == J_HEADER) {
            job.rc = vf_file_header(slot, &job.vf, &job.hdr);
        } else {
            job.rc = vf_file_eyecatch(slot, &job.vf, job.eye_type, job.eye_icons, job.eye);
        }
        job.state = 2;
    }
}

/* The eyecatch of a file of the card being browsed: 1 = not read yet (asked for), 0 = in `out`, -1 = none. */
static int
host_eyecatch(void* user, int slot, int file, uint16_t out[72 * 56]) {
    (void)user;
    if (slot < 0 || slot >= BFS_SLOTS || file < 0 || file >= cards[slot].nfiles) {
        return -1;
    }
    if (eye_slot == slot && eye_file == file && eye_state != 0) {
        if (eye_state > 0) {
            memcpy(out, eye_px, sizeof(eye_px));
            return 0;
        }
        return -1;
    }
    if (eye_slot != slot || eye_file != file) {
        eye_slot = slot;
        eye_file = file;
        eye_state = 0;
    }
    return 1;
}

/* A card's root block, picture and directory as the reader brought them. */
static void
apply_card(int slot) {
    bfs_card* c = &cards[slot];
    const vf_card_info* info = &job.info;
    c->status = info->status;
    c->colour = info->colour;
    memcpy(c->icon, info->icon, sizeof(c->icon));
    c->free_blocks = info->free_blocks;
    c->total_blocks = info->total_blocks;
    c->nfiles = 0;
    if (info->status == VF_CARD_READY) {
        int n = job.n;
        if (n < 0) {
            c->status = BFS_CARD_ERROR;
            n = 0;
        }
        for (int i = 0; i < n; i++) {
            bfs_file* f = &files[slot][i];
            const vf_file* v = &job.list[i];
            vfiles[slot][i] = *v;
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
    slot_gen[slot]++;
    if (header_card == slot) {
        header_card = -1; /* the headers have to be read again */
    }
    if (eye_slot == slot) {
        eye_slot = -1;
    }
}

/* Ask for a card to be read again (the card shows its busy animation meanwhile). */
static void
queue_card_read(int slot) {
    cards[slot].status = BFS_CARD_READING;
    cards[slot].serial++;
    slot_gen[slot]++;
    need_read[slot] = 1;
}

/* What the reader finished: put it where the screen looks. */
static void
apply_job(void) {
    int slot = job.slot;
    int fresh = job.gen == slot_gen[slot] && present[slot];
    if (job.kind == J_CARD) {
        if (fresh) {
            apply_card(slot);
        }
    } else if (job.kind == J_HEADER) {
        if (fresh && header_card == slot && job.file < cards[slot].nfiles) {
            bfs_file* f = &files[slot][job.file];
            const vf_header* h = &job.hdr;
            if (job.rc == 0) {
                memcpy(f->vmdesc, h->vmdesc, sizeof(f->vmdesc));
                memcpy(f->desc, h->desc, sizeof(f->desc));
                memcpy(f->app, h->app, sizeof(f->app));
                f->icons = (uint8_t)h->icons;
                f->speed = (int16_t)(h->speed > 0 ? h->speed : 1);
                f->wait = 1;
                f->eyecatch = (uint8_t)h->eyecatch;
                memcpy(f->palette, h->palette, sizeof(f->palette));
                memcpy(bitmaps[job.file], h->bitmaps, sizeof(bitmaps[job.file]));
                f->bitmaps = bitmaps[job.file];
            } else {
                f->icons = 0;
                f->bitmaps = NULL;
            }
            f->header = 2;
            cards[slot].serial++;
        }
    } else if (fresh && eye_slot == slot && eye_file == job.file) {
        if (job.rc == 0) {
            memcpy(eye_px, job.eye, sizeof(eye_px));
        }
        eye_state = job.rc == 0 ? 1 : -1;
        cards[slot].serial++;
        fs.b_dirty_info = 1; /* the info panel asks again and gets it */
    }
    job.state = 0;
}

/* The next read the screen is waiting for, if any: eyecatch of the file in view, card, then the headers one by one. */
static void
next_read(void) {
    if (eye_slot >= 0 && eye_state == 0 && eye_slot < BFS_SLOTS && eye_file < cards[eye_slot].nfiles && eye_file >= 0) {
        const bfs_file* f = &files[eye_slot][eye_file];
        job.eye_type = f->eyecatch;
        job.eye_icons = f->icons;
        ask(J_EYE, eye_slot, eye_file, &vfiles[eye_slot][eye_file]);
        return;
    }
    for (int i = 0; i < BFS_SLOTS; i++) {
        if (need_read[i]) {
            need_read[i] = 0;
            ask(J_CARD, i, 0, NULL);
            return;
        }
    }
    int slot = fs.browse_card;
    if (slot < 0 || slot >= BFS_SLOTS || cards[slot].status != BFS_CARD_READY) {
        return;
    }
    if (header_card != slot) {
        for (int i = 0; i < cards[slot].nfiles; i++) {
            files[slot][i].header = 0;
        }
        header_card = slot;
    }
    for (int i = 0; i < cards[slot].nfiles; i++) {
        if (!files[slot][i].header) {
            files[slot][i].header = 1; /* asked for */
            ask(J_HEADER, slot, i, &vfiles[slot][i]);
            return;
        }
    }
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
            queue_card_read(i);
        } else if (!now && present[i]) {
            cards[i].status = BFS_CARD_NONE;
            cards[i].nfiles = 0;
            cards[i].serial++;
            need_read[i] = 0;
            slot_gen[i]++;
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
    if (op->kind == BFS_OP_EXTEND) {
        op->failed = vf_extend_capacity(op->src) == VF_EXT_ERROR;
        queue_card_read(op->src);
        op->progress = 1000;
        op->done = 1;
        return;
    }
    if (op->kind == BFS_OP_FORMAT || op->kind == BFS_OP_LOOK) {
        vf_card_info info;
        vf_card_info_read(op->src, &info);
        int shape = op->shape >= 0 ? op->shape : (info.status == VF_CARD_READY ? info.shape : 0);
        unsigned int c = op->shape >= 0 ? op->colour : (info.status == VF_CARD_READY ? info.colour : 0xFFFFFFFFu);
        int custom = c != 0xFFFFFFFFu && c != 0;
        unsigned char bgra[4] = {(unsigned char)c, (unsigned char)(c >> 8), (unsigned char)(c >> 16), (unsigned char)(c >> 24)};
        if (op->kind == BFS_OP_LOOK) { /* only the icon and the colour: the files stay */
            op->failed = vf_set_look_raw(op->src, shape, custom, bgra) != 0;
        } else {
            op->failed = vf_format_raw(op->src, shape, custom, bgra) != 0;
        }
        queue_card_read(op->src);
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
    queue_card_read(op->src);
    if (op->kind == BFS_OP_COPY && op->dst >= 0) {
        queue_card_read(op->dst);
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
    start_reader();
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
        case BTN_Y: bfs_key(&fs, BFS_KEY_Y); break;
        case BTN_PAGE_UP: bfs_key(&fs, BFS_KEY_PGUP); break;
        case BTN_PAGE_DOWN: bfs_key(&fs, BFS_KEY_PGDN); break;
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
    if (job.state == 2) {
        apply_job();
    }
    if (job.state == 0) {
        next_read();
    }
}

/* The File screen's card grid shows a pill over each controller that is not a standard one: keyboard, mouse, light gun, fishing
 * rod, arcade stick and so on, made like the main menu's caption pills (body and lighter border, in the colours of the pill of the same position: A as Play, B as
 * File, C as Music, D as Settings). */
static void
draw_pad_labels(void) {
    char kind[4][PAD_KIND_MAX];
    float px[4], py[4], tw[4];
    pad_label_rect r[4];
    for (int p = 0; p < 4; p++) {
        maple_device_t* dev = maple_enum_dev(p, 0);
        char product[sizeof(dev->info.product_name) + 1];
        tw[p] = 0.0f;
        if (!dev || !dev->valid || !bfs_pad_px(&fs, p, &px[p], &py[p])) {
            continue;
        }
        memcpy(product, dev->info.product_name, sizeof(dev->info.product_name));
        product[sizeof(dev->info.product_name)] = '\0';
        if (!pad_kind_name(dev->info.functions, product, kind[p])) {
            continue;
        }
        for (;;) { /* a long name of its own (a controller we do not know) is cut to fit the column */
            pad_label_rect probe;
            pad_label_layout(0.0f, 0.0f, (float)(gfx_text_width(kind[p]) + 3), PAD_LABEL_CENTRED, &probe);
            if (strlen(kind[p]) <= 3 || probe.w <= PAD_LABEL_MAX_W) {
                break;
            }
            kind[p][strlen(kind[p]) - 1] = '\0';
        }
        tw[p] = (float)(gfx_text_width(kind[p]) + 3); /* what gfx_text() draws */
    }
    pad_label_layout_set(4, px, py, tw, PAD_LABEL_CENTRED, r); /* a pill that would touch the one before it goes to the top of its controller */
    for (int p = 0; p < 4; p++) {
        if (tw[p] <= 0.0f) {
            continue;
        }
        uint32_t body, border;
        bmenu_pill_colours(fs.m, p, &body, &border);
        gfx_rrect(r[p].x, r[p].y, r[p].w, r[p].h, r[p].h / 2.0f, 0.5f, border);
        gfx_rrect(r[p].x + PILL_BORDER, r[p].y + PILL_BORDER, r[p].w - 2.0f * PILL_BORDER, r[p].h - 2.0f * PILL_BORDER,
                  r[p].h / 2.0f - PILL_BORDER, 0.505f, (body & 0x00FFFFFFu) | 0xE0000000u);
        gfx_text(kind[p], r[p].x + (r[p].w - tw[p]) / 2.0f, r[p].y + (r[p].h - 26.0f) / 2.0f, 0.51f, 0xFFFFFFFFu, 1);
    }
}

void
uif_draw(void) {
    bfs_set_serial_slot(&fs, svmu_slot()); /* the card of the Serial VMU's slot carries the SD card mark, drawn with it */
    bfs_draw(&fs, gfx_sink());
    if (!left) {
        draw_pad_labels();
    }
}

int
uif_hover(float x, float y) {
    return left ? 0 : bfs_point(&fs, x, y);
}

int
uif_grid_slot(void) {
    if (left || fs.state != 1 || fs.g_state != 2 || fs.layer != 0 || fs.p_state != 0 || fs.m_state != 0 || fs.changed > 0) {
        return -1;
    }
    int c = fs.g_cursor;
    return c >= 0 && c < BFS_SLOTS && cards[c].status == BFS_CARD_READY ? c : -1;
}

int
uif_pages(void) {
    return !left && ((fs.state == 5 && fs.b_state == 2 && fs.layer == 0) || fs.f_state == 3);
}
