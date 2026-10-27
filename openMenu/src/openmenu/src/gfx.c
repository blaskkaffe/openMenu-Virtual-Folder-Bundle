/*
 * gfx: PVR backend for the BIOS-style menu, see gfx.h.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/pvr.h>
#include <fcntl.h>
#include <kos/fs.h>
#include <kos/sem.h>
#include <kos/thread.h>

#include <bios_menu.h>
#include <bios_list.h>
#include <bios_text.h>
#include <bios_surface.h>
#include <bios_models.h>
#include <tex_decode.h>
#include <bios_page.h>
#include <backend/dat_format.h>
#include <texture/serial_sanitize.h>

#include <bios_menu.h>

#include "gfx.h"

#define MAX_LOGO_FILE (256 * 1024)
#define MAX_TEXTURES 96
#define MAX_TEXT_ENTRIES 48
#define MAX_TEXT_CHARS 42
#define MAX_TEXT_W 512
#define MAX_LABELS 16
#define LABEL_CHARS 120 /* a label may hold several lines / columns, see sink_text */

/* Settings page text lines (bios_page): left aligned, label then a value column */
#define PAGE_TEXT_PAD 10.0f
#define PAGE_VALUE_X 216.0f
#define BG_Z 0.001f /* 1/w of the gradient quad: behind everything (the clouds are at ~0.0026) */
#define TEXT_Z_BIAS 0.00002f

typedef struct {
    int kind, a, b;
    pvr_ptr_t ptr;
    int w, h, fmt;
    int valid;
    int nearest; /* drawn pixel for pixel: no filtering (the text surfaces) */
} rom_tex;

typedef struct {
    char str[MAX_TEXT_CHARS + 1];
    pvr_ptr_t ptr;
    int w;
    uint32_t last_frame;
} text_tex;

static const bios_rom* g_rom;
static rom_tex rom_texes[MAX_TEXTURES];
static int num_rom_texes;
static rom_tex logo_tex;
static rom_tex logo_dcnet_tex;  /* the BIOS logo cropped to its swirl (mode 2: swirl + the D; CNET.PVR is drawn after it) */
static rom_tex logo_word_tex;   /* the BIOS logo without its swirl (mode 3) */
static rom_tex logo_swirl_tex;  /* the swirl alone, 32x32, turned while connecting (mode 3) */
static float logo_ox, logo_oy; /* top left of the header logo's quad this frame (it slides in with the bar) */
static int logo_seen;
static int logo_mode;           /* see gfx_set_logo_mode */
static int logo_variants_tried;
static text_tex text_texes[MAX_TEXT_ENTRIES];
static uint32_t frame_no;
static unsigned tri_count;
static gfx_stats stats; /* since the last gfx_stats_take() */
/* Translucent polygons: 1 = drawn in the order they are submitted (no hardware sorting, much
 * cheaper; the menu submits back to front already), 0 = the PVR sorts them per pixel. */
#ifndef GFX_PRESORT
#define GFX_PRESORT 1
#endif
static int autosort = !GFX_PRESORT; /* see gfx_set_autosort */

/* Switching the sort mode per frame.
 * KOS 2.1.1's pvr_set_presort_mode() must not be used: it re-runs the whole tile-matrix set-up, which moves the
 * buffer's matrix 0x48 bytes further on each call (the start-up code skips the matrix header that way). Called every
 * frame, the matrix walks through video RAM: tiles in the lower right break into tall stripes and the PVR hangs on
 * the first frame. Later KOS fixed it by rewriting only the control words of the tiles; this is that fix, reaching
 * the buffer through KOS's internal PVR state (pvr_internal.h, found by CMake in the KOS source tree). Without that
 * header the mode stays what pvr_init set (GFX_PRESORT). */
#ifdef GFX_PVR_INTERNAL
#include <pvr_internal.h>
#define TILE_PRESORT (1u << 29)
static int tile_mode[2] = {-1, -1}; /* presort bit currently in each TA buffer's tile matrix, -1 unknown */
static int tile_mode_broken;        /* the matrix did not look as expected: never touch it */

static int
set_tile_presort(int presort) {
    int target = pvr_state.ta_target;
    if (tile_mode_broken || target < 0 || target > 1) {
        return 0;
    }
    if (tile_mode[target] == presort) {
        return 1;
    }
    /* tile_matrix points at the start-up tile; the screen tiles follow, 6 words each, column by column */
    volatile uint32_t* vr = (volatile uint32_t*)PVR_RAM_BASE + (pvr_state.ta_buffers[target].tile_matrix >> 2) + 6;
    const int tw = pvr_state.tw, th = pvr_state.th;
    if ((vr[0] & ~TILE_PRESORT) != 0 || (th > 1 && (vr[6] & ~TILE_PRESORT) != 0x100u)) {
        tile_mode_broken = 1;
        return 0;
    }
    for (int x = 0; x < tw; x++) {
        for (int y = 0; y < th; y++) {
            uint32_t w = ((uint32_t)y << 8) | ((uint32_t)x << 2) | (presort ? TILE_PRESORT : 0u);
            if (x == tw - 1 && y == th - 1) {
                w |= 1u << 31; /* last tile */
            }
            vr[0] = w;
            vr += 6;
        }
    }
    tile_mode[target] = presort;
    return 1;
}
#endif

static struct {
    uint16_t id;
    char text[LABEL_CHARS + 1];
} labels[MAX_LABELS];
static int num_labels;

/* Last polygon header sent in the translucent list; avoids re-sending identical state. */
static pvr_ptr_t last_ptr;
static int last_untextured;
static int last_uv;  /* NJ_TEX_* flip / clamp bits of the current header */
static int hdr_uv;   /* the ones the next header gets (set by sink_triangle, 0 for everything else) */
static int hdr_valid;

static uint16_t text_canvas[MAX_TEXT_W * GFX_LINE_H] __attribute__((aligned(32)));

#ifndef GFX_VERTEX_DMA
#define GFX_VERTEX_DMA 1
#endif
#define OP_DMA_BYTES (128 * 1024)  /* the gradient quad only */
#define TR_DMA_BYTES (1024 * 1024) /* two halves of 512 KB: 16000 vertices a frame */
static void *op_dma_buf, *tr_dma_buf;
static int cur_list_op = 1; /* the list pvr_prim() sends to now: 1 = OP, 0 = TR */
static unsigned frame_bytes[2]; /* sent so far this frame, per list: never past a buffer half (KOS only asserts) */

/* ---- Setup -------------------------------------------------------------------- */

int
gfx_init(const bios_rom* rom) {
    g_rom = rom;
    bsurf_init(rom);
    pvr_init_params_t params = {
        /* OP, OP modifier, TR, TR modifier, punch-through */
        {PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0},
        512 * 1024, /* vertex buffer */
        GFX_VERTEX_DMA, /* the BIOS builds each list in RAM and sends it to the TA by DMA (0x8C0DA614): the CPU is free meanwhile */
        0,          /* no FSAA */
        GFX_PRESORT, /* 1 = translucent autosort disabled */
        1};         /* one extra set of object pointer blocks */
    int r = pvr_init(&params);
#if GFX_VERTEX_DMA
    if (r == 0) {
        /* KOS's vertex DMA mode: pvr_prim() copies into these (each buffer is split in two, one half per frame in flight) */
        op_dma_buf = memalign(32, OP_DMA_BYTES);
        tr_dma_buf = memalign(32, TR_DMA_BYTES);
        if (op_dma_buf && tr_dma_buf) {
            pvr_set_vertbuf(PVR_LIST_OP_POLY, op_dma_buf, OP_DMA_BYTES);
            pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_dma_buf, TR_DMA_BYTES);
        } else {
            free(op_dma_buf);
            free(tr_dma_buf);
            op_dma_buf = tr_dma_buf = NULL;
            pvr_shutdown();
            params.dma_enabled = 0; /* no memory for the buffers: the store queues as before */
            r = pvr_init(&params);
        }
    }
#endif
    return r;
}

/* ---- ROM textures ----------------------------------------------------------------- */

static int
pvr_format(const bios_texture* t) {
    int fmt = 0;
    switch (t->pixel_format) {
        case BIOS_PVR_ARGB1555: fmt = PVR_TXRFMT_ARGB1555; break;
        case BIOS_PVR_RGB565: fmt = PVR_TXRFMT_RGB565; break;
        default: fmt = PVR_TXRFMT_ARGB4444; break;
    }
    if (t->data_type == BIOS_PVR_RECTANGLE) {
        fmt |= PVR_TXRFMT_NONTWIDDLED;
    } else {
        fmt |= PVR_TXRFMT_TWIDDLED;
    }
    if (t->data_type == BIOS_PVR_VQ) {
        fmt |= PVR_TXRFMT_VQ_ENABLE;
    }
    return fmt;
}

/* Copy a texture payload to video memory and fill in `g`. */
static void
upload_texture(const bios_texture* t, rom_tex* g) {
    size_t size = (t->data_size + 31) & ~(size_t)31;
    void* staging = memalign(32, size);
    if (!staging) {
        return;
    }
    memset(staging, 0, size);
    memcpy(staging, t->data, t->data_size);

    g->ptr = pvr_mem_malloc(size);
    if (g->ptr) {
        pvr_txr_load(staging, g->ptr, size);
        g->w = t->width;
        g->h = t->height;
        g->fmt = pvr_format(t);
        g->valid = 1;
    }
    free(staging);
}

/* ---- Game art (DISC.DAT / ICON.DAT / BOX.DAT on the menu disc) ---------------------------------- */

#define ART_SLOTS 20
#define ART_CHUNK_MAX (160 * 1024)

typedef struct {
    char key[20]; /* sanitized product id; box art entries start with '#' */
    rom_tex tex;
    int present; /* an entry exists (tex.valid tells if the picture could be loaded) */
    int loading; /* a job of the loader thread is reading it: do not evict */
    uint32_t last_use;
    uint32_t retry_at; /* the disc could not be read: ask again from this frame on (0: nothing to retry) */
} art_entry;

/* Pictures are read from the DATs by a thread of their own (reading the disc takes tens of
 * milliseconds and used to stall the frame); the main thread only uploads a finished one to video
 * memory, at most ART_UPLOADS_PER_FRAME per frame. */
#define ART_JOBS 3
#define ART_UPLOADS_PER_FRAME 1
enum { JOB_FREE = 0, JOB_QUEUED, JOB_READING, JOB_DONE, JOB_FAILED, JOB_ERROR };
#define ART_RETRY_FRAMES 60       /* a read error: try that picture again a second later */
#define DAT_RETRY_FRAMES 300      /* a DAT that could not be opened: try again five seconds later ... */
#define DAT_RETRIES 3             /* ... at most this often (a DAT that is not on the disc stays missing) */
typedef struct {
    volatile int state;
    char id[20];
    int box;
    int entry;
    uint8_t* buf;
    uint32_t size; /* bytes of pixel data after the 0x20 byte header */
    int w, h, fmt;
    uint32_t us; /* time the thread needed */
} art_job;
static art_job art_jobs[ART_JOBS];
static semaphore_t art_sem;
static kthread_t* art_thread;
static int art_started;

static dat_file dat_icon, dat_icon_ex, dat_box, dat_box_ex;
static dat_file dat_disc; /* DISC.DAT: the games' own disc labels (their 0GDTEX.PVR), collected by the card manager */
static int dats_loaded;
static int dat_ok[5], dat_tries[5];
static uint32_t dat_retry_at;
static art_entry art_cache[ART_SLOTS];
static char row_product[BLIST_MAX_SLOTS][16];
static int row_pal[BLIST_MAX_SLOTS]; /* the game of the row is a PAL release: the blue BIOS disc, not the red one */
static int row_window[BLIST_MAX_SLOTS]; /* title scroll: visible width in px (0 = no scrolling) */
static int row_offset[BLIST_MAX_SLOTS];

static int art_jobs_idle(void);

/* The DATs, parsed once; one that could not be opened or parsed is tried again a few times (the disc may not have
 * been ready), but only while the loader thread is not reading from them. */
static void
art_load_dats(void) {
    static dat_file* const dats[5] = {&dat_disc, &dat_icon, &dat_icon_ex, &dat_box, &dat_box_ex};
    static const char* const names[5] = {"DISC.DAT", "ICON.DAT", "ICON_EX.DAT", "BOX.DAT", "BOX_EX.DAT"};
    if (dats_loaded == 2 || (dats_loaded && (frame_no < dat_retry_at || !art_jobs_idle()))) {
        return;
    }
    int pending = 0;
    for (int i = 0; i < 5; i++) {
        if (dat_ok[i] || dat_tries[i] >= DAT_RETRIES) {
            continue;
        }
        DAT_init(dats[i]);
        dat_ok[i] = DAT_load_parse(dats[i], names[i]) == 0;
        if (!dat_ok[i]) {
            DAT_init(dats[i]); /* nothing half set up: the readers see no table */
            pending |= ++dat_tries[i] < DAT_RETRIES;
        }
    }
    dats_loaded = pending ? 1 : 2;
    dat_retry_at = frame_no + DAT_RETRY_FRAMES;
}

/* The PVR files in the DATs have a fixed 0x20 byte header (GBIX + PVRT): colour format, layout,
 * then width and height. Returns the size of the pixel data, 0 if unusable. */
static uint32_t
art_parse(const uint8_t* b, int* w, int* h, int* fmt) {
    int color = b[0x18], layout = b[0x19];
    *w = b[0x1C] | (b[0x1D] << 8);
    *h = b[0x1E] | (b[0x1F] << 8);
    int f = 0;
    switch (color) {
        case 0: f = PVR_TXRFMT_ARGB1555; break;
        case 2: f = PVR_TXRFMT_ARGB4444; break;
        default: f = PVR_TXRFMT_RGB565; break;
    }
    switch (layout) {
        case 0x01: f |= PVR_TXRFMT_TWIDDLED; break;
        case 0x03: f |= PVR_TXRFMT_VQ_ENABLE; break;
        case 0x09: f |= PVR_TXRFMT_NONTWIDDLED; break;
        case 0x0D: f |= PVR_TXRFMT_TWIDDLED; break;
        case 0x10: f |= PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED; break;
        default: return 0;
    }
    if (*w <= 0 || *h <= 0 || *w > 1024 || *h > 1024) {
        return 0;
    }
    *fmt = f;
    return (uint32_t)(*w) * (uint32_t)(*h) * 2u;
}

/* Runs in the loader thread: read the picture `id` of the DAT into the job's buffer. 1 when it is there, 0 when the
 * DAT does not have it (or it is unusable), -1 when the disc could not be read. */
static int
art_read_job(const dat_file* d, art_job* j) {
    if (!d->hash || d->chunk_size > ART_CHUNK_MAX || d->chunk_size <= 0x20 || !DAT_get_offset_by_ID(d, j->id)) {
        return 0;
    }
    if (!DAT_read_file_by_ID(d, j->id, j->buf)) {
        return -1;
    }
    int w, h, fmt;
    uint32_t size = art_parse(j->buf, &w, &h, &fmt);
    if (!size) {
        return 0; /* not a picture the PVR can take: no art rather than garbage */
    }
    if (fmt & PVR_TXRFMT_VQ_ENABLE) {
        size = 2048u + (uint32_t)w * (uint32_t)h / 4u; /* the code book, then one byte per 2x2 texels */
    }
    if (size + 0x20 > d->chunk_size) {
        return 0; /* the chunk cannot hold that much */
    }
    j->w = w;
    j->h = h;
    j->fmt = fmt;
    j->size = size;
    return 1;
}

static void*
art_worker(void* param) {
    (void)param;
    for (;;) {
        sem_wait(&art_sem);
        for (int i = 0; i < ART_JOBS; i++) {
            art_job* j = &art_jobs[i];
            if (j->state != JOB_QUEUED) {
                continue;
            }
            j->state = JOB_READING;
            uint64_t t0 = timer_us_gettime64();
            /* disc labels: the game's own label (DISC.DAT) first; then the add-on file wins over the main one */
            const dat_file* order[3] = {j->box ? NULL : &dat_disc, j->box ? &dat_box_ex : &dat_icon_ex,
                                        j->box ? &dat_box : &dat_icon};
            int got = 0, error = 0;
            for (int k = 0; k < 3 && got != 1; k++) {
                if (order[k]) {
                    got = art_read_job(order[k], j);
                    error |= got < 0;
                }
            }
            j->us = (uint32_t)(timer_us_gettime64() - t0);
            j->state = got == 1 ? JOB_DONE : (error ? JOB_ERROR : JOB_FAILED);
        }
    }
    return NULL;
}

/* The loader thread and its buffers; if they cannot be had now, it is tried again on a later request. */
static int
art_start(void) {
    if (art_started) {
        return 1;
    }
    for (int i = 0; i < ART_JOBS; i++) {
        if (!art_jobs[i].buf) {
            art_jobs[i].buf = (uint8_t*)memalign(32, ART_CHUNK_MAX);
        }
        if (!art_jobs[i].buf) {
            return 0; /* the buffers already had are kept for the next try */
        }
    }
    static int sem_ready;
    if (!sem_ready) {
        sem_init(&art_sem, 0);
        sem_ready = 1;
    }
    art_thread = thd_create(1, art_worker, NULL);
    if (!art_thread) {
        return 0;
    }
    thd_set_prio(art_thread, PRIO_DEFAULT + 4); /* lower than the menu */
    art_started = 1;
    return 1;
}

static int
art_jobs_idle(void) {
    for (int i = 0; i < ART_JOBS; i++) {
        if (art_jobs[i].state == JOB_QUEUED || art_jobs[i].state == JOB_READING) {
            return 0;
        }
    }
    return 1;
}

/* Video memory is full: give back the picture used longest ago (not `keep`, not one being loaded). 1 if one went. */
static int
art_evict_one(const art_entry* keep) {
    art_entry* oldest = NULL;
    for (int i = 0; i < ART_SLOTS; i++) {
        art_entry* e = &art_cache[i];
        if (e != keep && e->present && !e->loading && e->tex.valid && (!oldest || e->last_use < oldest->last_use)) {
            oldest = e;
        }
    }
    if (!oldest) {
        return 0;
    }
    pvr_mem_free(oldest->tex.ptr);
    memset(oldest, 0, sizeof(*oldest)); /* asked for again, it is read again */
    return 1;
}

/* Main thread, once per frame: upload the pictures the loader has finished. */
static void
art_poll(void) {
    int uploads = 0;
    for (int i = 0; i < ART_JOBS; i++) {
        art_job* j = &art_jobs[i];
        if (j->state == JOB_FAILED || j->state == JOB_ERROR) {
            art_entry* e = &art_cache[j->entry];
            e->loading = 0;
            /* not in any DAT: no art, asked no more. The disc could not be read: ask again a second later */
            e->retry_at = j->state == JOB_ERROR ? frame_no + ART_RETRY_FRAMES : 0;
            j->state = JOB_FREE;
        } else if (j->state == JOB_DONE && uploads < ART_UPLOADS_PER_FRAME) {
            art_entry* e = &art_cache[j->entry];
            uint32_t padded = (j->size + 31u) & ~31u;
            e->tex.ptr = pvr_mem_malloc(padded);
            if (!e->tex.ptr) {
                /* video memory is full: make room and upload it on the next frame; with nothing to give back, it
                 * stays without art (and is read again when asked for later) */
                if (!art_evict_one(e)) {
                    memset(e, 0, sizeof(*e));
                    j->state = JOB_FREE;
                }
                continue;
            }
            pvr_txr_load(j->buf + 0x20, e->tex.ptr, padded);
            e->tex.w = j->w;
            e->tex.h = j->h;
            e->tex.fmt = j->fmt;
            e->tex.valid = 1;
            uploads++;
            stats.art_loads++;
            stats.art_us += j->us;
            e->loading = 0;
            j->state = JOB_FREE;
        }
    }
}

static art_entry*
art_get(const char* product, int box) {
    char key[20];
    if (!product || !product[0]) {
        return NULL;
    }
    art_load_dats();
    const char* id = serial_santize_art(product);
    snprintf(key, sizeof(key), "%s%.17s", box ? "#" : "", id);
    art_entry* free_slot = NULL;
    art_entry* oldest = NULL;
    for (int i = 0; i < ART_SLOTS; i++) {
        art_entry* e = &art_cache[i];
        if (e->present && !strcmp(e->key, key)) {
            e->last_use = frame_no;
            if (!e->retry_at || e->loading || frame_no < e->retry_at) {
                return e;
            }
            memset(e, 0, sizeof(*e)); /* a read error earlier: read it again */
            free_slot = e;
            break;
        }
        if (!e->present) {
            free_slot = free_slot ? free_slot : e;
        } else if (!e->loading && (!oldest || e->last_use < oldest->last_use)) {
            oldest = e;
        }
    }
    if (!art_start()) {
        return NULL;
    }
    art_job* job = NULL;
    for (int i = 0; i < ART_JOBS && !job; i++) {
        job = art_jobs[i].state == JOB_FREE ? &art_jobs[i] : NULL;
    }
    if (!job) {
        return NULL; /* the loader is busy: ask again on a later frame */
    }
    art_entry* e = free_slot;
    if (!e) {
        e = oldest;
        if (!e || e->last_use + 2 > frame_no) {
            return NULL; /* every slot is in use: wait */
        }
        if (e->tex.valid) {
            pvr_mem_free(e->tex.ptr);
        }
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->present = 1;
    e->loading = 1;
    e->last_use = frame_no;
    strncpy(job->id, id, sizeof(job->id) - 1);
    job->id[sizeof(job->id) - 1] = '\0';
    job->box = box;
    job->entry = (int)(e - art_cache);
    job->state = JOB_QUEUED;
    sem_signal(&art_sem);
    return e;
}

int
gfx_art_prefetch(const char* product, int box) {
    if (!product || !product[0]) {
        return 1;
    }
    art_entry* e = art_get(product, box);
    if (!e) {
        return 0; /* loader busy or cache full this frame: not there yet */
    }
    return !e->loading;
}

void
gfx_art_bind_row(int slot, const char* product, int pal) {
    if (slot >= 0 && slot < BLIST_MAX_SLOTS) {
        row_pal[slot] = pal != 0;
        strncpy(row_product[slot], product ? product : "", sizeof(row_product[slot]) - 1);
        row_product[slot][sizeof(row_product[slot]) - 1] = '\0';
    }
}

/* The BIOS has two pictures for the face of a disc, both 256x256 with the GBIX 0: the first in the
 * ROM is blue (what the BIOS shows on European consoles), the second red (Japan and America). */
static rom_tex*
disc_face(int pal) {
    static rom_tex face[2];
    static int tried[2];
    int i = pal ? 1 : 0;
    if (!tried[i] && g_rom) {
        tried[i] = 1;
        int found = 0;
        for (int n = 0; n < bios_texture_count(g_rom); n++) {
            bios_texture t;
            if (bios_texture_get(g_rom, n, &t) == 0 && t.gbix == 0 && t.width == 256 && t.height == 256) {
                if (found == (pal ? 0 : 1)) {
                    upload_texture(&t, &face[i]);
                    break;
                }
                found++;
            }
        }
    }
    return face[i].valid ? &face[i] : NULL;
}

/* Plain white picture for the texture slots of the built-in models (bios_models.h) that have no art. */
static rom_tex*
white_tex(void) {
    static rom_tex white;
    static int tried;
    if (!tried) {
        tried = 1;
        unsigned short px[8 * 8];
        for (int i = 0; i < 64; i++) {
            px[i] = 0xFFFF;
        }
        white.ptr = pvr_mem_malloc(sizeof(px));
        if (white.ptr) {
            pvr_txr_load(px, white.ptr, sizeof(px));
            white.w = white.h = 8;
            white.fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
            white.valid = 1;
        }
    }
    return white.valid ? &white : NULL;
}

static char model_front_product[16];
static int memo_ok;

void
gfx_model_bind_front(const char* product) {
    strncpy(model_front_product, product ? product : "", sizeof(model_front_product) - 1);
    model_front_product[sizeof(model_front_product) - 1] = '\0';
    memo_ok = 0; /* the same texture reference now means another picture */
}

/* The last texture looked up: a model draws many triangles with the same one, and finding a game picture
 * costs a string search. Forgotten whenever pictures come or go. */
static bscene_texref memo_ref;
static rom_tex* memo_tex;

static rom_tex* get_rom_texture_uncached(bscene_texref ref);

/* ---- The header logo and the online mode ---- */

#define LOGO_W 128
#define LOGO_H 32
#define LOGO_SWIRL_W 29 /* columns of the logo texture that are the swirl; the wordmark starts at 30 */
#define LOGO_D_END 46 /* the wordmark's D ends here; CNET.PVR's letters start (the D is kept from the BIOS logo) */
#define LOGO_SWIRL_CX 14.0f /* the swirl's centre in the logo texture */
#define LOGO_SWIRL_CY 16.0f

static void
upload_argb4444(const unsigned short* px, int w, int h, rom_tex* g) {
    size_t size = ((size_t)w * (size_t)h * 2u + 31u) & ~(size_t)31u;
    void* staging = memalign(32, size);
    pvr_ptr_t ptr = staging ? pvr_mem_malloc(size) : NULL;
    if (!ptr) {
        free(staging);
        return;
    }
    memset(staging, 0, size);
    memcpy(staging, px, (size_t)w * (size_t)h * 2u);
    pvr_txr_load(staging, ptr, size);
    free(staging);
    g->ptr = ptr;
    g->w = w;
    g->h = h;
    g->fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
    g->valid = 1;
}

/* A .PVR file of the menu disc into `g` (the same way as LOGO.PVR). */
static void
load_pvr_file(const char* path, rom_tex* g) {
    file_t fd = fs_open(path, O_RDONLY);
    if (fd == FILEHND_INVALID) {
        return;
    }
    ssize_t size = fs_total(fd);
    uint8_t* buf = (size > 0 && size <= MAX_LOGO_FILE) ? (uint8_t*)malloc((size_t)size) : NULL;
    if (buf && fs_read(fd, buf, (size_t)size) != size) {
        free(buf);
        buf = NULL;
    }
    fs_close(fd);
    if (!buf) {
        return;
    }
    bios_texture t;
    if (bios_texture_parse(buf, (size_t)size, &t) == 0) {
        upload_texture(&t, g);
    }
    free(buf);
}

/* The letters of the online logos: CNET.PVR (grey, drawn after the BIOS logo's own D) and NOWO.PVR / NOWB.PVR (orange / blue "Now!"),
 * next to LOGO.PVR. They are drawn as they are (the soft edges are baked in) and hold nothing of the BIOS logo.
 * Without them the logo stays as it is in the online modes. */
static rom_tex logo_now_tex[2]; /* [0] orange, [1] blue */
static rom_tex logo_dcnet_text_tex;
static unsigned logo_swirl_rgb = 0xFFFFFF; /* the swirl's colour, 0xRRGGBB: picks the orange or the blue "Now!" */

void
gfx_load_logo_texts(void) {
    if (!logo_now_tex[0].valid) {
        load_pvr_file("/cd/NOWO.PVR", &logo_now_tex[0]);
    }
    if (!logo_now_tex[1].valid) {
        load_pvr_file("/cd/NOWB.PVR", &logo_now_tex[1]);
    }
    if (!logo_dcnet_text_tex.valid) {
        load_pvr_file("/cd/CNET.PVR", &logo_dcnet_text_tex);
    }
    printf("OM logo: NOWO %d NOWB %d CNET %d (/cd/*.PVR loaded)\n", logo_now_tex[0].valid, logo_now_tex[1].valid, logo_dcnet_text_tex.valid);
}

int
gfx_logo_texts_loaded(void) {
    return logo_now_tex[0].valid | logo_now_tex[1].valid << 1 | logo_dcnet_text_tex.valid << 2;
}

/* Pictures made from the ROM's logo while the menu runs (nothing of it is kept anywhere): the swirl with the wordmark's D, the
 * wordmark alone, and the swirl on its own for turning. Nothing is made when a LOGO.PVR of the user's own is in use. */
static void
build_logo_variants(void) {
    if (logo_variants_tried || logo_tex.valid || !g_rom) {
        return;
    }
    logo_variants_tried = 1;
    bios_texture t;
    uint32_t* argb = malloc(sizeof(uint32_t) * LOGO_W * LOGO_H);
    unsigned short* only_swirl = calloc((size_t)LOGO_W * LOGO_H, sizeof(unsigned short));
    unsigned short* word = calloc((size_t)LOGO_W * LOGO_H, sizeof(unsigned short));
    unsigned short* swirl = calloc((size_t)32 * LOGO_H, sizeof(unsigned short));
    if (argb && only_swirl && word && swirl && bios_header_logo(g_rom, *(volatile uint8_t*)0x8C000072 & 0xF, &t) == 0 && t.width == LOGO_W
        && t.height == LOGO_H && bios_texture_decode(&t, argb) == 0) {
        int best = -1;
        for (int y = 0; y < LOGO_H; y++) {
            for (int x = 0; x < LOGO_W; x++) {
                uint32_t c = argb[y * LOGO_W + x];
                unsigned short v = (unsigned short)((((c >> 24) & 0xFF) >> 4) << 12 | (((c >> 16) & 0xFF) >> 4) << 8 | (((c >> 8) & 0xFF) >> 4) << 4 | ((c & 0xFF) >> 4));
                if (x < LOGO_D_END) { /* the swirl and the D */
                    only_swirl[y * LOGO_W + x] = v;
                }
                if (x < LOGO_SWIRL_W) { /* the swirl */
                    swirl[y * 32 + x] = v;
                    /* its colour: the most saturated opaque pixel (orange on NTSC and PAL-J, blue on PAL) */
                    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                    int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    if ((c >> 24) >= 0xF0 && hi - lo > best) {
                        best = hi - lo;
                        logo_swirl_rgb = (unsigned)(((r + 8) / 17) * 17) << 16 | (unsigned)(((g + 8) / 17) * 17) << 8 | (unsigned)(((b + 8) / 17) * 17);
                    }
                } else { /* the wordmark */
                    word[y * LOGO_W + x] = v;
                }
            }
        }
        upload_argb4444(only_swirl, LOGO_W, LOGO_H, &logo_dcnet_tex);
        upload_argb4444(word, LOGO_W, LOGO_H, &logo_word_tex);
        upload_argb4444(swirl, 32, LOGO_H, &logo_swirl_tex);
    }
    free(argb);
    free(only_swirl);
    free(word);
    free(swirl);
}

void
gfx_set_logo_mode(int mode) {
    if (mode != logo_mode) {
        logo_mode = mode;
        memo_ok = 0; /* the header's texture may have been remembered */
    }
}


static rom_tex*
get_rom_texture(bscene_texref ref) {
    if (memo_ok && memo_ref.kind == ref.kind && memo_ref.a == ref.a && memo_ref.b == ref.b) {
        return memo_tex;
    }
    rom_tex* t = get_rom_texture_uncached(ref);
    memo_ok = 1;
    memo_ref = ref;
    memo_tex = t;
    return t;
}

static rom_tex*
get_rom_texture_uncached(bscene_texref ref) {
    /* built-in models: slot 0 shows the box art of the bound game, everything else is plain white */
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BMODEL_BASE && ref.a < BMODEL_END) {
        if (ref.b == BMODEL_TEX_FRONT) {
            art_entry* e = art_get(model_front_product, 1);
            if (e && e->tex.valid) {
                return &e->tex;
            }
        }
        return white_tex();
    }
    /* A row disc: texture 0 is the label (the game's art); the rest is the BIOS disc, texlist 61 */
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BLIST_TEXLIST_BASE) {
        int slot = ref.a - BLIST_TEXLIST_BASE;
        if (ref.b == 0 && slot < BLIST_MAX_SLOTS) {
            art_entry* e = art_get(row_product[slot], 0);
            if (e && e->tex.valid) {
                return &e->tex;
            }
            rom_tex* face = disc_face(row_pal[slot]); /* no picture: the BIOS disc of the game's region */
            if (face) {
                return face;
            }
        }
        ref.a = BLIST_DISC_MODEL;
    }
    if (logo_tex.valid && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        return &logo_tex;
    }
    if ((logo_mode == 2 || logo_mode == 3) && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        build_logo_variants();
        rom_tex* v = logo_mode == 2 ? &logo_dcnet_tex : &logo_word_tex; /* 3: the swirl is drawn turning by gfx_logo_badge */
        if (v->valid && (logo_mode == 2 ? logo_dcnet_text_tex.valid : logo_swirl_tex.valid)) {
            return v;
        }
    }
    for (int i = 0; i < num_rom_texes; i++) {
        if (rom_texes[i].kind == ref.kind && rom_texes[i].a == ref.a && rom_texes[i].b == ref.b) {
            return rom_texes[i].valid ? &rom_texes[i] : NULL;
        }
    }
    if (num_rom_texes >= MAX_TEXTURES || !g_rom) {
        return NULL;
    }

    rom_tex* g = &rom_texes[num_rom_texes++];
    memset(g, 0, sizeof(*g));
    g->kind = ref.kind;
    g->a = ref.a;
    g->b = ref.b;

    bios_texture t;
    int ok;
    if (ref.kind == BSCENE_TEX_GBIX) {
        ok = bios_texture_find(g_rom, (uint32_t)ref.a, &t) == 0;
    } else if (ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        /* the logo with the swirl, or the plain one on a European console, as the BIOS picks it */
        ok = bios_header_logo(g_rom, *(volatile uint8_t*)0x8C000072 & 0xF, &t) == 0;
    } else {
        ok = bios_texlist_texture(g_rom, ref.a, ref.b, &t) == 0;
    }
    if (!ok || t.data_size == 0) {
        return NULL;
    }

    upload_texture(&t, g);
    return g->valid ? g : NULL;
}

int
gfx_load_logo(const char* path) {
    if (logo_tex.valid) {
        return 0;
    }
    file_t fd = fs_open(path, O_RDONLY);
    if (fd == FILEHND_INVALID) {
        return -1;
    }
    ssize_t size = fs_total(fd);
    uint8_t* buf = (size > 0 && size <= MAX_LOGO_FILE) ? (uint8_t*)malloc((size_t)size) : NULL;
    if (buf && fs_read(fd, buf, (size_t)size) != size) {
        free(buf);
        buf = NULL;
    }
    fs_close(fd);
    if (!buf) {
        return -1;
    }

    bios_texture t;
    if (bios_texture_parse(buf, (size_t)size, &t) == 0) {
        upload_texture(&t, &logo_tex);
    }
    free(buf);
    return logo_tex.valid ? 0 : -1;
}

/* ---- Submission --------------------------------------------------------------------- */

/* Headers and vertices are collected here and sent with a few large pvr_prim() calls: each call
 * locks the store queues, which cost more than the triangles themselves when done per polygon. */
#define CMD_SLOTS 2048
static pvr_vertex_t cmdbuf[CMD_SLOTS] __attribute__((aligned(32)));
static int cmd_n;

static void
cmd_flush(void) {
    if (cmd_n) {
        uint64_t t0 = timer_us_gettime64();
#if GFX_VERTEX_DMA
        if (tr_dma_buf) {
            unsigned bytes = (unsigned)cmd_n * (unsigned)sizeof(pvr_vertex_t);
            int op = cur_list_op;
            unsigned room = op ? OP_DMA_BYTES / 2 : TR_DMA_BYTES / 2;
            if (frame_bytes[op] + bytes + 128u > room) {
                cmd_n = 0; /* this frame's list is full: drop the rest rather than run over the buffer */
                return;
            }
            frame_bytes[op] += bytes;
        }
#endif
        pvr_prim(cmdbuf, cmd_n * (int)sizeof(pvr_vertex_t));
        stats.ta_us += (unsigned)(timer_us_gettime64() - t0);
        stats.vertices += (unsigned)cmd_n;
        cmd_n = 0;
    }
}

static void
cmd_reserve(int slots) {
    if (cmd_n + slots > CMD_SLOTS) {
        cmd_flush();
    }
}

static void
submit_header(pvr_poly_cxt_t* cxt) {
    cmd_reserve(1);
    pvr_poly_compile((pvr_poly_hdr_t*)&cmdbuf[cmd_n++], cxt);
}

static void
send_header_tr(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w) {
    pvr_poly_cxt_t cxt;

    if (tex) {
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, tex->fmt, tex->w, tex->h, tex->ptr,
                         tex->nearest ? PVR_FILTER_NONE : PVR_FILTER_BILINEAR);
        cxt.gen.specular = PVR_SPECULAR_ENABLE; /* the offset colour: the BIOS' specular on textured strips */
        /* the model's texture chunk: mirror or hold u and v past 0..1 (the disc's hub is a quarter, mirrored) */
        cxt.txr.uv_flip = ((hdr_uv & NJ_TEX_FLIP_U) ? PVR_UVFLIP_U : 0) | ((hdr_uv & NJ_TEX_FLIP_V) ? PVR_UVFLIP_V : 0);
        cxt.txr.uv_clamp = ((hdr_uv & NJ_TEX_CLAMP_U) ? PVR_UVCLAMP_U : 0) | ((hdr_uv & NJ_TEX_CLAMP_V) ? PVR_UVCLAMP_V : 0);
    } else if (text_ptr) {
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED, text_w, GFX_LINE_H, text_ptr,
                         PVR_FILTER_NONE);
    } else {
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    }
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    if (text_ptr || !tex) {
        /* Text and flat panels: their transparent parts must not hide what is drawn later. */
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    submit_header(&cxt);
    stats.headers++;
}

static void strip_close(void);

static void
ensure_header(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w) {
    strip_close();
    pvr_ptr_t key = tex ? tex->ptr : text_ptr;
    int untextured = (key == NULL);
    int uv = tex ? hdr_uv : 0;
    hdr_uv = 0;
    if (hdr_valid && key == last_ptr && untextured == last_untextured && uv == last_uv) {
        return;
    }
    hdr_uv = uv;
    send_header_tr(tex, text_ptr, text_w);
    hdr_uv = 0;
    last_ptr = key;
    last_untextured = untextured;
    last_uv = uv;
    hdr_valid = 1;
}

/* Vertices of the polygon being built (a triangle or a quad strip); sent when the one marked
 * as last arrives, so there is one submission per polygon. */
static pvr_vertex_t* cur_poly;
static uint32_t vtx_offset; /* the PVR offset colour of the vertices being sent (specular of textured strips) */

/* 16:9: the picture is squeezed to 3/4 of its width around the centre; a wide TV stretches it back. */
static float aspect_x = 1.0f;
static int aspect_bypass; /* the background gradient always fills the screen */

void
gfx_set_aspect(int wide) {
    aspect_x = wide ? 0.75f : 1.0f;
}

static void
put_vertex(float x, float y, float z, float u, float v, uint32_t argb, int last) {
    if (!aspect_bypass) {
        x = 320.0f + (x - 320.0f) * aspect_x;
    }
    if (!cur_poly) {
        cmd_reserve(4);
        cur_poly = &cmdbuf[cmd_n];
    } else {
        cmd_reserve(1); /* a long strip: the buffer may fill up in its middle (the TA takes it in pieces) */
    }
    pvr_vertex_t* vert = &cmdbuf[cmd_n++];
    vert->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = x;
    vert->y = y;
    vert->z = z;
    vert->u = u;
    vert->v = v;
    vert->argb = argb;
    vert->oargb = vtx_offset;
    if (last) {
        cur_poly = NULL;
    }
}

/* The scene sends separate triangles, but most come from the models' triangle strips: a triangle that shares the
 * last two vertices sent is sent as one more vertex of the same strip (culling is off, so the order does not
 * matter). A third of the vertices for the TA, the store queues and the PVR's object lists. The strip's last vertex
 * waits here until it is known whether the strip goes on. */
static bscene_vtx strip_l1, strip_l2;
static const rom_tex* strip_tex;
static int strip_uv;
static int strip_pending;

/* the same corner of the same strip: position, colours and picture coordinates equal (word compares, no memcmp call) */
static inline int
vtx_same(const bscene_vtx* a, const bscene_vtx* b) {
    return a->x == b->x && a->y == b->y && a->argb == b->argb && a->u == b->u && a->v == b->v && a->invw == b->invw
           && a->oargb == b->oargb;
}

static void
strip_vertex(const bscene_vtx* p, const rom_tex* tex, int last) {
    vtx_offset = tex ? p->oargb : 0;
    put_vertex(p->x, p->y, p->invw, p->u, p->v, p->argb, last);
    vtx_offset = 0;
}

static void
strip_close(void) {
    if (strip_pending) {
        strip_pending = 0;
        strip_vertex(&strip_l2, strip_tex, 1);
    }
}

static void
send_vertex(float x, float y, float z, float u, float v, uint32_t argb, int last) {
    strip_close();
    put_vertex(x, y, z, u, v, argb, last);
}

static void
sink_triangle(void* user, const bscene_vtx v[3], bscene_texref ref) {
    (void)user;
    if (!(v[0].argb >> 24) && !(v[1].argb >> 24) && !(v[2].argb >> 24)) {
        return; /* fully transparent */
    }
    rom_tex* tex = NULL;
    if (ref.kind != BSCENE_TEX_NONE) {
        tex = get_rom_texture(ref);
        if (!tex) {
            return; /* texture missing from this ROM: draw nothing rather than garbage */
        }
    }
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        for (int i = 0; i < 3; i++) {
            if (!logo_seen || v[i].x < logo_ox) logo_ox = v[i].x;
            if (!logo_seen || v[i].y < logo_oy) logo_oy = v[i].y;
            logo_seen = 1;
        }
    }
    tri_count++;
    if (strip_pending && tex == strip_tex && ref.uv == strip_uv) {
        int a = -1, b = -1;
        for (int i = 0; i < 3; i++) {
            if (a < 0 && vtx_same(&v[i], &strip_l1)) {
                a = i;
            } else if (b < 0 && vtx_same(&v[i], &strip_l2)) {
                b = i;
            }
        }
        if (a >= 0 && b >= 0) {
            strip_vertex(&strip_l2, tex, 0); /* the strip goes on */
            strip_l1 = strip_l2;
            strip_l2 = v[3 - a - b];
            return;
        }
    }
    hdr_uv = ref.uv;
    ensure_header(tex, NULL, 0); /* also ends a pending strip */
    strip_uv = ref.uv;
    strip_vertex(&v[0], tex, 0);
    strip_vertex(&v[1], tex, 0);
    strip_l1 = v[1];
    strip_l2 = v[2];
    strip_tex = tex;
    strip_pending = 1;
}

static const char*
find_label(uint16_t id) {
    for (int i = 0; i < num_labels; i++) {
        if (labels[i].id == id) {
            return labels[i].text;
        }
    }
    return NULL;
}

static void text_windowed(const char* str, float x, float y, float z, uint32_t argb, int window, int offset);

/* ---- Text surfaces (bios_surface): the pictures the screens print into, kept in video memory ---------------- */

typedef struct {
    uint16_t id;
    rom_tex tex;
    uint32_t serial;
} surf_tex;
static surf_tex surf_texes[BSURF_MAX];

static rom_tex*
surface_texture(bsurf* sf) {
    surf_tex* st = NULL;
    for (int i = 0; i < BSURF_MAX; i++) {
        if (surf_texes[i].tex.valid && surf_texes[i].id == sf->id) {
            st = &surf_texes[i];
            break;
        }
    }
    if (st && (st->tex.w != sf->w || st->tex.h != sf->h)) {
        pvr_mem_free(st->tex.ptr);
        memset(st, 0, sizeof(*st));
        st = NULL;
    }
    if (!st) {
        for (int i = 0; i < BSURF_MAX && !st; i++) {
            if (!surf_texes[i].tex.valid) {
                st = &surf_texes[i];
            }
        }
        if (!st) {
            return NULL;
        }
        st->tex.ptr = pvr_mem_malloc((size_t)sf->w * (size_t)sf->h * 2u);
        if (!st->tex.ptr) {
            return NULL;
        }
        st->id = sf->id;
        st->tex.w = sf->w;
        st->tex.h = sf->h;
        st->tex.fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
        st->tex.nearest = 1;
        st->tex.valid = 1;
        bsurf_touch(sf, 0, sf->h - 1); /* all of it the first time */
    }
    int y0, y1;
    if (bsurf_take_dirty(sf, &y0, &y1)) {
        size_t row = (size_t)sf->w * 2u;
        pvr_txr_load(sf->px + (size_t)y0 * sf->w, (uint8_t*)st->tex.ptr + (size_t)y0 * row, (size_t)(y1 - y0 + 1) * row);
        stats.text_uploads++;
    }
    return &st->tex;
}

/* Free the video memory of surfaces that no longer exist (after a screen freed them). */
static void
surface_gc(void) {
    for (int i = 0; i < BSURF_MAX; i++) {
        if (surf_texes[i].tex.valid && !bsurf_find(surf_texes[i].id)) {
            pvr_mem_free(surf_texes[i].tex.ptr);
            memset(&surf_texes[i], 0, sizeof(surf_texes[i]));
        }
    }
}

static int
draw_surface(const bvm_obj* obj, float x, float y, float invw) {
    bsurf* sf = bsurf_find(obj->id);
    if (!sf) {
        return 0;
    }
    if (sf->hidden) {
        return 1;
    }
    rom_tex* t = surface_texture(sf);
    if (!t) {
        return 1;
    }
    float a = bscene_text_alpha(obj);
    if (a <= 0.0f) {
        return 1;
    }
    uint32_t argb = ((uint32_t)(a * 255.0f) << 24) | 0x00FFFFFFu;
    float x0 = x - (float)sf->w / 2.0f, y0 = y - (float)sf->h / 2.0f, z = invw + TEXT_Z_BIAS;
    ensure_header(t, NULL, 0);
    send_vertex(x0, y0, z, 0.0f, 0.0f, argb, 0);
    send_vertex(x0 + (float)sf->w, y0, z, 1.0f, 0.0f, argb, 0);
    send_vertex(x0, y0 + (float)sf->h, z, 0.0f, 1.0f, argb, 0);
    send_vertex(x0 + (float)sf->w, y0 + (float)sf->h, z, 1.0f, 1.0f, argb, 1);
    return 1;
}

static void
sink_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    if (draw_surface(obj, x, y, invw)) {
        return;
    }
    const char* label = find_label(obj->id);
    if (label && obj->id >= BPAGE_TEXT_ID(0) && obj->id <= BPAGE_TEXT_ID(BPAGE_SLOTS - 1)) {
        /* Settings page: left aligned in the text surface. "label\tvalue" puts the value in a
         * second column. (The information under the box is written by ui_bios.c.) */
        float x0 = x - (float)obj->text_w / 2.0f;
        float y0 = y - (float)obj->text_h / 2.0f;
        char part[LABEL_CHARS + 1];
        const char* p = label;
        for (int line = 0; *p && line < 3; line++) {
            size_t n = strcspn(p, "\n");
            if (n > LABEL_CHARS) {
                n = LABEL_CHARS;
            }
            memcpy(part, p, n);
            part[n] = '\0';
            char* tab = strchr(part, '\t');
            float ly = y0 + (float)(line * GFX_LINE_H);
            if (tab) {
                *tab = '\0';
                gfx_text(tab + 1, x0 + PAGE_VALUE_X, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
            }
            gfx_text(part, x0 + PAGE_TEXT_PAD, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
            p += n;
            if (*p == '\n') {
                p++;
            }
        }
    } else if (label && obj->id >= BLIST_TEXT_FIRST && obj->id <= BLIST_TEXT_LAST) {
        /* Game list row: one line, left aligned in the text surface; a long selected title scrolls */
        int slot = obj->id - BLIST_TEXT_FIRST;
        float lx = x - (float)obj->text_w / 2.0f, ly = y - (float)obj->text_h / 2.0f;
        if (slot >= 0 && slot < BLIST_MAX_SLOTS && row_window[slot] > 0 && gfx_text_width(label) > row_window[slot]) {
            text_windowed(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, row_window[slot], row_offset[slot]);
        } else {
            gfx_text(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
        }
    } else if (label) {
        /* The anchor of a text surface is its centre (checked against the BIOS layout:
         * the caption pills line up with it), so centre the string on it. */
        float w = (float)gfx_text_width(label);
        int header = obj->id == BMENU_ID_HEADER; /* dark text on the light header bar, as in the BIOS */
        gfx_text(label, x - w / 2.0f, y - (float)GFX_LINE_H / 2.0f, invw + TEXT_Z_BIAS, header ? 0xFF303030u : 0xFFFFFFFFu,
                 !header);
    }
}

/* A strip from the scene (the BIOS' strip handlers send these too): one header if the picture changes, then the vertices. */
static void
sink_strip(void* user, const bscene_vtx* v, int n, bscene_texref ref) {
    (void)user;
    int any = 0;
    for (int i = 0; i < n; i++) {
        any |= v[i].argb >> 24 ? 1 : 0;
    }
    if (!any) {
        return; /* fully transparent */
    }
    rom_tex* tex = NULL;
    if (ref.kind != BSCENE_TEX_NONE) {
        tex = get_rom_texture(ref);
        if (!tex) {
            return; /* texture missing from this ROM: draw nothing rather than garbage */
        }
    }
    tri_count += (unsigned)(n - 2);
    hdr_uv = ref.uv;
    ensure_header(tex, NULL, 0); /* also ends a pending strip of sink_triangle() */
    for (int i = 0; i < n; i++) {
        vtx_offset = tex ? v[i].oargb : 0;
        put_vertex(v[i].x, v[i].y, v[i].invw, v[i].u, v[i].v, v[i].argb, i == n - 1);
    }
    vtx_offset = 0;
}

static const bscene_sink the_sink = {NULL, sink_triangle, sink_text, sink_strip};

const bscene_sink*
gfx_sink(void) {
    return &the_sink;
}

unsigned
gfx_triangles(void) {
    return tri_count;
}

void
gfx_stats_take(gfx_stats* out) {
    *out = stats;
    memset(&stats, 0, sizeof(stats));
}

void
gfx_set_label(uint16_t obj_id, const char* text) {
    for (int i = 0; i < num_labels; i++) {
        if (labels[i].id == obj_id) {
            strncpy(labels[i].text, text, LABEL_CHARS);
            labels[i].text[LABEL_CHARS] = '\0';
            return;
        }
    }
    if (num_labels < MAX_LABELS) {
        labels[num_labels].id = obj_id;
        strncpy(labels[num_labels].text, text, LABEL_CHARS);
        labels[num_labels].text[LABEL_CHARS] = '\0';
        num_labels++;
    }
}

/* ---- Text ------------------------------------------------------------------------------- */

#define BIOS_FONT_ROM ((const uint8_t*)0xA0100020u) /* what syscall_font_address() returns */

int
gfx_text_width(const char* str) {
    return btext_width(str);
}

static int
pow2_at_least(int v) {
    int p = 32;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

static text_tex*
get_text_texture(const char* str) {
    text_tex* victim = NULL;
    for (int i = 0; i < MAX_TEXT_ENTRIES; i++) {
        if (text_texes[i].ptr && !strcmp(text_texes[i].str, str)) {
            text_texes[i].last_frame = frame_no;
            return &text_texes[i];
        }
    }

    /* Reuse an empty slot, else the least recently used one. A slot only becomes
     * available two frames after its last use: the PVR may still be rendering the
     * previous frame with its texture. */
    for (int i = 0; i < MAX_TEXT_ENTRIES; i++) {
        text_tex* t = &text_texes[i];
        if (!t->ptr) {
            victim = t;
            break;
        }
        if (t->last_frame + 2 <= frame_no && (!victim || t->last_frame < victim->last_frame)) {
            victim = t;
        }
    }
    if (!victim) {
        return NULL;
    }

    int w = pow2_at_least((int)strlen(str) * GFX_CHAR_W);
    if (w > MAX_TEXT_W) {
        w = MAX_TEXT_W;
    }
    if (victim->ptr && victim->w != w) {
        pvr_mem_free(victim->ptr);
        victim->ptr = NULL;
    }
    if (!victim->ptr) {
        victim->ptr = pvr_mem_malloc((size_t)w * GFX_LINE_H * 2);
        if (!victim->ptr) {
            return NULL;
        }
    }
    victim->w = w;
    strncpy(victim->str, str, MAX_TEXT_CHARS);
    victim->str[MAX_TEXT_CHARS] = '\0';
    victim->last_frame = frame_no;

    memset(text_canvas, 0, (size_t)w * GFX_LINE_H * 2);
    /* the BIOS way: glyphs drawn with double thickness and a built-in shade (no drop shadow needed) */
    btext_draw(text_canvas, w, GFX_LINE_H, 0, 0, BIOS_FONT_ROM, victim->str, 0xFFFF);
    pvr_txr_load(text_canvas, victim->ptr, (size_t)w * GFX_LINE_H * 2);
    stats.text_uploads++;
    return victim;
}

void
gfx_text(const char* str, float x, float y, float z, uint32_t argb, int shadow) {
    if (!str || !str[0]) {
        return;
    }
    char clipped[MAX_TEXT_CHARS + 1];
    strncpy(clipped, str, MAX_TEXT_CHARS);
    clipped[MAX_TEXT_CHARS] = '\0';

    text_tex* t = get_text_texture(clipped);
    if (!t) {
        return;
    }
    ensure_header(NULL, t->ptr, t->w);

    /* only the part of the picture that holds text: less to blend, and nothing sticks out over its neighbours */
    int used = btext_width(clipped) + 3;
    if (used > t->w) used = t->w;
    float w = (float)used, h = 26.0f;
    float u1 = (float)used / (float)t->w, v1 = 26.0f / (float)GFX_LINE_H;
    (void)shadow; /* the glyphs carry their own shade, see btext_blit() */
    send_vertex(x, y, z, 0.0f, 0.0f, argb, 0);
    send_vertex(x + w, y, z, u1, 0.0f, argb, 0);
    send_vertex(x, y + h, z, 0.0f, v1, argb, 0);
    send_vertex(x + w, y + h, z, u1, v1, argb, 1);
}

void
gfx_text_scaled(const char* str, float x, float y, float z, uint32_t argb, float scale) {
    if (!str || !str[0]) {
        return;
    }
    char clipped[MAX_TEXT_CHARS + 1];
    strncpy(clipped, str, MAX_TEXT_CHARS);
    clipped[MAX_TEXT_CHARS] = '\0';
    text_tex* t = get_text_texture(clipped);
    if (!t) {
        return;
    }
    ensure_header(NULL, t->ptr, t->w);
    int used = btext_width(clipped) + 3;
    if (used > t->w) used = t->w;
    float w = (float)used * scale, h = 26.0f * scale;
    float u1 = (float)used / (float)t->w, v1 = 26.0f / (float)GFX_LINE_H;
    send_vertex(x, y, z, 0.0f, 0.0f, argb, 0);
    send_vertex(x + w, y, z, u1, 0.0f, argb, 0);
    send_vertex(x, y + h, z, 0.0f, v1, argb, 0);
    send_vertex(x + w, y + h, z, u1, v1, argb, 1);
}

/* A string shown through a window of `window` pixels, moved `offset` pixels to the left. */
static void
text_windowed(const char* str, float x, float y, float z, uint32_t argb, int window, int offset) {
    char clipped[MAX_TEXT_CHARS + 1];
    strncpy(clipped, str, MAX_TEXT_CHARS);
    clipped[MAX_TEXT_CHARS] = '\0';
    text_tex* t = get_text_texture(clipped);
    if (!t) {
        return;
    }
    int full = gfx_text_width(clipped);
    if (offset < 0) offset = 0;
    if (offset > full - window) offset = full > window ? full - window : 0;
    int win = window < full ? window : full;
    float u0 = (float)offset / (float)t->w, u1 = (float)(offset + win) / (float)t->w;
    ensure_header(NULL, t->ptr, t->w);
    float w = (float)win, h = (float)GFX_LINE_H;
    send_vertex(x, y, z, u0, 0.0f, argb, 0);
    send_vertex(x + w, y, z, u1, 0.0f, argb, 0);
    send_vertex(x, y + h, z, u0, 1.0f, argb, 0);
    send_vertex(x + w, y + h, z, u1, 1.0f, argb, 1);
}

void
gfx_set_row_scroll(int slot, int window_px, int offset_px) {
    if (slot >= 0 && slot < BLIST_MAX_SLOTS) {
        row_window[slot] = window_px;
        row_offset[slot] = offset_px;
    }
}

void
gfx_rect(float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(NULL, NULL, 0);
    send_vertex(x, y, z, 0, 0, argb, 0);
    send_vertex(x + w, y, z, 0, 0, argb, 0);
    send_vertex(x, y + h, z, 0, 0, argb, 0);
    send_vertex(x + w, y + h, z, 0, 0, argb, 1);
}

/* ---- Small pictures made at run time (file icons of the memory cards) ----------------------------- */

#define DYN_MAX 64
static rom_tex dyn_tex[DYN_MAX];

int
gfx_dyn_create(const unsigned short* argb4444, int w, int h) {
    for (int i = 0; i < DYN_MAX; i++) {
        if (dyn_tex[i].valid) {
            continue;
        }
        size_t size = ((size_t)w * (size_t)h * 2u + 31u) & ~(size_t)31u;
        void* staging = memalign(32, size);
        pvr_ptr_t ptr = staging ? pvr_mem_malloc(size) : NULL;
        if (!ptr) {
            free(staging);
            return -1;
        }
        memset(staging, 0, size);
        memcpy(staging, argb4444, (size_t)w * (size_t)h * 2u);
        pvr_txr_load(staging, ptr, size);
        free(staging);
        dyn_tex[i].ptr = ptr;
        dyn_tex[i].w = w;
        dyn_tex[i].h = h;
        dyn_tex[i].fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
        dyn_tex[i].valid = 1;
        return i;
    }
    return -1;
}

void
gfx_dyn_free(int id) {
    if (id >= 0 && id < DYN_MAX && dyn_tex[id].valid) {
        pvr_mem_free(dyn_tex[id].ptr);
        memset(&dyn_tex[id], 0, sizeof(dyn_tex[id]));
    }
}

void
gfx_dyn_free_all(void) {
    for (int i = 0; i < DYN_MAX; i++) {
        gfx_dyn_free(i);
    }
}

void
gfx_image(int id, float x, float y, float w, float h, float z) {
    if (id < 0 || id >= DYN_MAX || !dyn_tex[id].valid) {
        return;
    }
    ensure_header(&dyn_tex[id], NULL, 0);
    send_vertex(x, y, z, 0.0f, 0.0f, 0xFFFFFFFFu, 0);
    send_vertex(x + w, y, z, 1.0f, 0.0f, 0xFFFFFFFFu, 0);
    send_vertex(x, y + h, z, 0.0f, 1.0f, 0xFFFFFFFFu, 0);
    send_vertex(x + w, y + h, z, 1.0f, 1.0f, 0xFFFFFFFFu, 1);
}

/* Where the header logo is on the screen (measured on the preview: the 128x32 texture covers x 27..165, y 28..61). */
void
gfx_logo_badge(void) {
    const float sx = 139.0f / 128.0f, sy = 34.0f / 32.0f;
    const float ox = logo_seen ? logo_ox : 27.0f, oy = logo_seen ? logo_oy : 28.0f; /* follows the header bar while it slides */
    if (logo_mode == 3) { /* connecting: the swirl turns, the wordmark stays */
        static unsigned turn;
        build_logo_variants();
        if (!logo_swirl_tex.valid || !logo_word_tex.valid) {
            return;
        }
        const float s = 1.074f; /* one scale both ways keeps the swirl round while it turns */
        float a = (float)(turn++ % 72u) * (6.2831853f / 72.0f); /* one turn in 72 frames */
        float ca = cosf(a), sa = sinf(a);
        float cx = ox + LOGO_SWIRL_CX * sx, cy = oy + LOGO_SWIRL_CY * sy;
        ensure_header(&logo_swirl_tex, NULL, 0);
        for (int i = 0; i < 4; i++) {
            float tx = (i & 1) ? 32.0f : 0.0f, ty = (i & 2) ? (float)LOGO_H : 0.0f;
            float dx = (tx - LOGO_SWIRL_CX) * s, dy = (ty - LOGO_SWIRL_CY) * s;
            send_vertex(cx + dx * ca - dy * sa, cy + dx * sa + dy * ca, 0.5f, tx / 32.0f, ty / (float)LOGO_H, 0xFFFFFFFFu, i == 3);
        }
        return;
    }
    /* "Now!" after the logo, or "CNET" after the D of the logo (cropped to swirl + D then): pictures with the edges baked in */
    build_logo_variants();
    int blue = (logo_swirl_rgb & 0xFF) > ((logo_swirl_rgb >> 16) & 0xFF);
    rom_tex* text = logo_mode == 1 ? &logo_now_tex[blue] : logo_mode == 2 ? &logo_dcnet_text_tex : NULL;
    if (!text || !text->valid) {
        return;
    }
    const uint32_t tint = 0xFFFFFFFFu;
    float x = logo_mode == 1 ? ox + 127.0f * sx - 2.0f : ox + (float)LOGO_D_END * sx, y = oy;
    float w = (float)text->w * sx, h = (float)LOGO_H * sy;
    ensure_header(text, NULL, 0);
    send_vertex(x, y, 0.5f, 0.0f, 0.0f, tint, 0);
    send_vertex(x + w, y, 0.5f, 1.0f, 0.0f, tint, 0);
    send_vertex(x, y + h, 0.5f, 0.0f, 1.0f, tint, 0);
    send_vertex(x + w, y + h, 0.5f, 1.0f, 1.0f, tint, 1);
}

void
gfx_art_box(const char* product, float x, float y, float w, float h, float z) {
    art_entry* e = art_get(product, 1);
    if (e && e->tex.valid) {
        ensure_header(&e->tex, NULL, 0);
        send_vertex(x, y, z, 0.0f, 0.0f, 0xFFFFFFFFu, 0);
        send_vertex(x + w, y, z, 1.0f, 0.0f, 0xFFFFFFFFu, 0);
        send_vertex(x, y + h, z, 0.0f, 1.0f, 0xFFFFFFFFu, 0);
        send_vertex(x + w, y + h, z, 1.0f, 1.0f, 0xFFFFFFFFu, 1);
    } else {
        gfx_rect(x, y, w, h, z, 0x40FFFFFFu); /* no picture (yet) */
    }
}

/* Flat translucent rectangle with rounded corners of radius r: three bands and four corner fans
 * that do not overlap, so the translucent colour is even. */
void
gfx_rrect(float x, float y, float w, float h, float r, float z, uint32_t argb) {
    enum { SEGMENTS = 4 };
    static const float cs[SEGMENTS + 1] = {1.0f, 0.9239f, 0.7071f, 0.3827f, 0.0f}; /* cos of 0, 22.5, 45, 67.5, 90 degrees */
    static const float sn[SEGMENTS + 1] = {0.0f, 0.3827f, 0.7071f, 0.9239f, 1.0f};
    if (r * 2.0f > w) r = w / 2.0f;
    if (r * 2.0f > h) r = h / 2.0f;
    ensure_header(NULL, NULL, 0);
    /* top, middle and bottom bands */
    float xs[3] = {x + r, x, x + r};
    float ys[3] = {y, y + r, y + h - r};
    float ws[3] = {w - 2.0f * r, w, w - 2.0f * r};
    float hs[3] = {r, h - 2.0f * r, r};
    for (int i = 0; i < 3; i++) {
        send_vertex(xs[i], ys[i], z, 0, 0, argb, 0);
        send_vertex(xs[i] + ws[i], ys[i], z, 0, 0, argb, 0);
        send_vertex(xs[i], ys[i] + hs[i], z, 0, 0, argb, 0);
        send_vertex(xs[i] + ws[i], ys[i] + hs[i], z, 0, 0, argb, 1);
    }
    /* corners: centre of each arc and the direction of its quarter */
    float cx[4] = {x + w - r, x + r, x + r, x + w - r};
    float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    float dx[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    float dy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < SEGMENTS; i++) {
            send_vertex(cx[c], cy[c], z, 0, 0, argb, 0);
            send_vertex(cx[c] + dx[c] * r * cs[i], cy[c] + dy[c] * r * sn[i], z, 0, 0, argb, 0);
            send_vertex(cx[c] + dx[c] * r * cs[i + 1], cy[c] + dy[c] * r * sn[i + 1], z, 0, 0, argb, 1);
        }
    }
}

/* The app's standard panel: flat, the colour of the row bars (BSCENE_PANE_ARGB), and corners shaped exactly as the
 * right end of a row bar (model 33 as the Settings screen shows it, traced pixel by pixel): about 9 px, a little
 * squarer than a circle. */
static const float pane_corner[][2] = { /* inset from the side, distance from the top/bottom edge */
    {9.5f, 0.0f}, {6.5f, 1.0f}, {4.5f, 2.0f}, {3.5f, 3.0f}, {2.5f, 4.0f}, {1.5f, 5.0f}, {0.8f, 6.5f}, {0.3f, 8.0f}, {0.0f, 9.0f}};
#define PANE_CORNER_N ((int)(sizeof(pane_corner) / sizeof(pane_corner[0])))
#define PANE_R 9.5f /* width of the corner */
#define PANE_RV 9.0f /* height of the corner */

void
gfx_pane(float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(NULL, NULL, 0);
    /* top and bottom bands between the corners, and the middle band */
    float xs[3] = {x + PANE_R, x, x + PANE_R};
    float ys[3] = {y, y + PANE_RV, y + h - PANE_RV};
    float ws[3] = {w - 2.0f * PANE_R, w, w - 2.0f * PANE_R};
    float hs[3] = {PANE_RV, h - 2.0f * PANE_RV, PANE_RV};
    for (int i = 0; i < 3; i++) {
        send_vertex(xs[i], ys[i], z, 0, 0, argb, 0);
        send_vertex(xs[i] + ws[i], ys[i], z, 0, 0, argb, 0);
        send_vertex(xs[i], ys[i] + hs[i], z, 0, 0, argb, 0);
        send_vertex(xs[i] + ws[i], ys[i] + hs[i], z, 0, 0, argb, 1);
    }
    /* each corner: a fan from its inner point (x side + PANE_R, y side + PANE_RV) */
    for (int c = 0; c < 4; c++) {
        int right = c == 0 || c == 3, bottom = c >= 2;
        float ex = right ? x + w : x, ey = bottom ? y + h : y; /* the box corner */
        float sx = right ? -1.0f : 1.0f, sy = bottom ? -1.0f : 1.0f;
        float ix = ex + sx * PANE_R, iy = ey + sy * PANE_RV;
        for (int i = 0; i + 1 < PANE_CORNER_N; i++) {
            send_vertex(ix, iy, z, 0, 0, argb, 0);
            send_vertex(ex + sx * pane_corner[i][0], ey + sy * pane_corner[i][1], z, 0, 0, argb, 0);
            send_vertex(ex + sx * pane_corner[i + 1][0], ey + sy * pane_corner[i + 1][1], z, 0, 0, argb, 1);
        }
    }
}

/* ---- Frames ------------------------------------------------------------------------------ */

void
gfx_begin_frame(uint32_t top, uint32_t bottom) {
    frame_no++;
    tri_count = 0;
    logo_seen = 0;
    {
        uint64_t t0 = timer_us_gettime64();
        pvr_wait_ready();
        stats.wait_us += (unsigned)(timer_us_gettime64() - t0);
        pvr_stats_t ps;
        if (pvr_get_stats(&ps) == 0) {
            stats.render_us += (unsigned)(ps.rnd_last_time / 1000u);
        }
    }
    art_poll();
    surface_gc();
    memo_ok = 0;
    /* The tile matrix of the buffer the TA fills next carries the sort mode (see set_tile_presort). */
#ifdef GFX_PVR_INTERNAL
    if (!set_tile_presort(!autosort)) {
        autosort = !GFX_PRESORT;
    }
#endif
    /* Where the gradient quad does not draw, show a mid blue instead of black. */
    pvr_set_bg_color(0.45f, 0.60f, 0.80f);
    pvr_scene_begin();

    pvr_poly_cxt_t cxt;
    frame_bytes[0] = frame_bytes[1] = 0;
    cur_list_op = 1;
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    submit_header(&cxt);
    aspect_bypass = 1;
    send_vertex(0.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(640.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(0.0f, 480.0f, BG_Z, 0, 0, bottom, 0);
    send_vertex(640.0f, 480.0f, BG_Z, 0, 0, bottom, 1);
    aspect_bypass = 0;
    cmd_flush();
    pvr_list_finish();

    cur_list_op = 0;
    pvr_list_begin(PVR_LIST_TR_POLY);
    hdr_valid = 0;
}

int
gfx_set_autosort(int on) {
#ifdef GFX_PVR_INTERNAL
    autosort = on ? 1 : 0;
#else
    (void)on; /* cannot switch safely: keep the mode pvr_init set */
#endif
    return autosort;
}

int
gfx_autosort(void) {
    return autosort;
}

void
gfx_end_frame(void) {
    strip_close();
    cmd_flush();
    pvr_list_finish();
    pvr_scene_finish();
}
