/*
 * gfx: PVR backend for the BIOS-style menu, see gfx.h.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dc/biosfont.h>
#include <dc/pvr.h>
#include <fcntl.h>
#include <kos/fs.h>

#include <bios_menu.h>
#include <bios_list.h>
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
#define PAGE_VALUE_X 250.0f
#define BG_Z 0.001f /* 1/w of the gradient quad: behind everything (the clouds are at ~0.0026) */
#define TEXT_Z_BIAS 0.00002f

typedef struct {
    int kind, a, b;
    pvr_ptr_t ptr;
    int w, h, fmt;
    int valid;
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
static text_tex text_texes[MAX_TEXT_ENTRIES];
static uint32_t frame_no;
static unsigned tri_count;
/* Translucent polygons: 1 = drawn in the order they are submitted (no hardware sorting, much
 * cheaper; the menu submits back to front already), 0 = the PVR sorts them per pixel. */
#ifndef GFX_PRESORT
#define GFX_PRESORT 1
#endif

static struct {
    uint16_t id;
    char text[LABEL_CHARS + 1];
} labels[MAX_LABELS];
static int num_labels;

/* Last polygon header sent in the translucent list; avoids re-sending identical state. */
static pvr_ptr_t last_ptr;
static int last_untextured;
static int hdr_valid;

static uint16_t text_canvas[MAX_TEXT_W * GFX_LINE_H] __attribute__((aligned(32)));

/* ---- Setup -------------------------------------------------------------------- */

int
gfx_init(const bios_rom* rom) {
    g_rom = rom;
    pvr_init_params_t params = {
        /* OP, OP modifier, TR, TR modifier, punch-through */
        {PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0},
        512 * 1024, /* vertex buffer */
        0,          /* no DMA */
        0,          /* no FSAA */
        GFX_PRESORT, /* 1 = translucent autosort disabled */
        1};         /* one extra set of object pointer blocks */
    return pvr_init(&params);
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

/* ---- Game art (ICON.DAT / BOX.DAT on the menu disc) ---------------------------------------- */

#define ART_SLOTS 20
#define ART_CHUNK_MAX (160 * 1024)

typedef struct {
    char key[20]; /* sanitized product id; box art entries start with '#' */
    rom_tex tex;
    int present; /* an entry exists (tex.valid tells if the picture could be loaded) */
    uint32_t last_use;
} art_entry;

static dat_file dat_icon, dat_icon_ex, dat_box, dat_box_ex;
static int dats_loaded;
static art_entry art_cache[ART_SLOTS];
static int art_budget; /* pictures that may still be loaded this frame */
static char row_product[BLIST_MAX_SLOTS][16];
static int row_pal[BLIST_MAX_SLOTS]; /* the game of the row is a PAL release: the blue BIOS disc, not the red one */
static int row_window[BLIST_MAX_SLOTS]; /* title scroll: visible width in px (0 = no scrolling) */
static int row_offset[BLIST_MAX_SLOTS];
static uint8_t* art_buf;

static void
art_load_dats(void) {
    if (dats_loaded) {
        return;
    }
    dats_loaded = 1;
    DAT_init(&dat_icon);
    DAT_init(&dat_icon_ex);
    DAT_init(&dat_box);
    DAT_init(&dat_box_ex);
    DAT_load_parse(&dat_icon, "ICON.DAT");
    DAT_load_parse(&dat_icon_ex, "ICON_EX.DAT");
    DAT_load_parse(&dat_box, "BOX.DAT");
    DAT_load_parse(&dat_box_ex, "BOX_EX.DAT");
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

static int
art_read(const dat_file* d, const char* id, art_entry* e) {
    if (!d->hash || d->chunk_size > ART_CHUNK_MAX || !DAT_get_offset_by_ID(d, id)) {
        return 0;
    }
    if (!art_buf) {
        art_buf = (uint8_t*)memalign(32, ART_CHUNK_MAX);
        if (!art_buf) {
            return 0;
        }
    }
    if (!DAT_read_file_by_ID(d, id, art_buf)) {
        return 0;
    }
    int w, h, fmt;
    uint32_t size = art_parse(art_buf, &w, &h, &fmt);
    if (!size || size + 0x20 > d->chunk_size) {
        size = d->chunk_size > 0x20 ? d->chunk_size - 0x20 : 0; /* VQ: less data than w*h*2 */
    }
    if (!size) {
        return 0;
    }
    uint32_t padded = (size + 31u) & ~31u;
    e->tex.ptr = pvr_mem_malloc(padded);
    if (!e->tex.ptr) {
        return 0;
    }
    pvr_txr_load(art_buf + 0x20, e->tex.ptr, padded);
    e->tex.w = w;
    e->tex.h = h;
    e->tex.fmt = fmt;
    e->tex.valid = 1;
    return 1;
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
    art_entry* oldest = &art_cache[0];
    for (int i = 0; i < ART_SLOTS; i++) {
        art_entry* e = &art_cache[i];
        if (e->present && !strcmp(e->key, key)) {
            e->last_use = frame_no;
            return e;
        }
        if (!e->present) {
            free_slot = free_slot ? free_slot : e;
        } else if (e->last_use < oldest->last_use) {
            oldest = e;
        }
    }
    if (art_budget <= 0) {
        return NULL; /* load it on a later frame */
    }
    art_budget--;
    art_entry* e = free_slot;
    if (!e) {
        e = oldest;
        if (e->last_use + 2 > frame_no) {
            return NULL; /* every slot is in use: wait */
        }
        if (e->tex.valid) {
            pvr_mem_free(e->tex.ptr);
        }
        memset(e, 0, sizeof(*e));
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->present = 1;
    e->last_use = frame_no;
    /* the add-on file wins over the main one */
    if (!art_read(box ? &dat_box_ex : &dat_icon_ex, id, e)) {
        art_read(box ? &dat_box : &dat_icon, id, e);
    }
    return e;
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

static rom_tex*
get_rom_texture(bscene_texref ref) {
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
    int ok = (ref.kind == BSCENE_TEX_GBIX) ? bios_texture_find(g_rom, (uint32_t)ref.a, &t) == 0
                                            : bios_texlist_texture(g_rom, ref.a, ref.b, &t) == 0;
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
        pvr_prim(cmdbuf, cmd_n * (int)sizeof(pvr_vertex_t));
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
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, tex->fmt, tex->w, tex->h, tex->ptr, PVR_FILTER_BILINEAR);
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
}

static void
ensure_header(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w) {
    pvr_ptr_t key = tex ? tex->ptr : text_ptr;
    int untextured = (key == NULL);
    if (hdr_valid && key == last_ptr && untextured == last_untextured) {
        return;
    }
    send_header_tr(tex, text_ptr, text_w);
    last_ptr = key;
    last_untextured = untextured;
    hdr_valid = 1;
}

/* Vertices of the polygon being built (a triangle or a quad strip); sent when the one marked
 * as last arrives, so there is one submission per polygon. */
static pvr_vertex_t* cur_poly;

static void
send_vertex(float x, float y, float z, float u, float v, uint32_t argb, int last) {
    if (!cur_poly) {
        cmd_reserve(4);
        cur_poly = &cmdbuf[cmd_n];
    }
    pvr_vertex_t* vert = &cmdbuf[cmd_n++];
    vert->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = x;
    vert->y = y;
    vert->z = z;
    vert->u = u;
    vert->v = v;
    vert->argb = argb;
    vert->oargb = 0;
    if (last) {
        cur_poly = NULL;
    }
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
    tri_count++;
    ensure_header(tex, NULL, 0);
    for (int i = 0; i < 3; i++) {
        send_vertex(v[i].x, v[i].y, v[i].invw, v[i].u, v[i].v, v[i].argb, i == 2);
    }
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

static void
sink_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    const char* label = find_label(obj->id);
    if (label && obj->id >= BPAGE_TEXT_ID(0) && obj->id <= BPAGE_HELP_ID) {
        /* Settings page: left aligned in the text surface. "label\tvalue" puts the value in a
         * second column, '\n' starts a new line. */
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
        if (slot >= 0 && slot < BLIST_MAX_SLOTS && row_window[slot] > 0 && (int)strlen(label) * GFX_CHAR_W > row_window[slot]) {
            text_windowed(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, row_window[slot], row_offset[slot]);
        } else {
            gfx_text(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
        }
    } else if (label) {
        /* The anchor of a text surface is its centre (checked against the BIOS layout:
         * the caption pills line up with it), so centre the string on it. */
        float w = (float)strlen(label) * GFX_CHAR_W;
        int header = obj->id == BMENU_ID_HEADER; /* dark text on the light header bar, as in the BIOS */
        gfx_text(label, x - w / 2.0f, y - (float)GFX_LINE_H / 2.0f, invw + TEXT_Z_BIAS, header ? 0xFF303030u : 0xFFFFFFFFu,
                 !header);
    }
}

static const bscene_sink the_sink = {NULL, sink_triangle, sink_text};

const bscene_sink*
gfx_sink(void) {
    return &the_sink;
}

unsigned
gfx_triangles(void) {
    return tri_count;
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
    bfont_draw_str_ex(text_canvas, (uint32_t)w, 0xFFFF, 0, 16, 0, victim->str);
    pvr_txr_load(text_canvas, victim->ptr, (size_t)w * GFX_LINE_H * 2);
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

    float w = (float)t->w, h = (float)GFX_LINE_H;
    for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
        float ox = pass == 0 ? 2.0f : 0.0f;
        uint32_t col = pass == 0 ? ((argb >> 24) / 2u) << 24 : argb; /* black, half alpha */
        send_vertex(x + ox, y + ox, z, 0.0f, 0.0f, col, 0);
        send_vertex(x + ox + w, y + ox, z, 1.0f, 0.0f, col, 0);
        send_vertex(x + ox, y + ox + h, z, 0.0f, 1.0f, col, 0);
        send_vertex(x + ox + w, y + ox + h, z, 1.0f, 1.0f, col, 1);
    }
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
    int full = (int)strlen(clipped) * GFX_CHAR_W;
    if (offset < 0) offset = 0;
    if (offset > full - window) offset = full > window ? full - window : 0;
    int win = window < full ? window : full;
    float u0 = (float)offset / (float)t->w, u1 = (float)(offset + win) / (float)t->w;
    ensure_header(NULL, t->ptr, t->w);
    float w = (float)win, h = (float)GFX_LINE_H;
    for (int pass = 0; pass < 2; pass++) {
        float ox = pass == 0 ? 2.0f : 0.0f;
        uint32_t col = pass == 0 ? ((argb >> 24) / 2u) << 24 : argb;
        send_vertex(x + ox, y + ox, z, u0, 0.0f, col, 0);
        send_vertex(x + ox + w, y + ox, z, u1, 0.0f, col, 0);
        send_vertex(x + ox, y + ox + h, z, u0, 1.0f, col, 0);
        send_vertex(x + ox + w, y + ox + h, z, u1, 1.0f, col, 1);
    }
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

/* ---- Frames ------------------------------------------------------------------------------ */

void
gfx_begin_frame(uint32_t top, uint32_t bottom) {
    frame_no++;
    tri_count = 0;
    art_budget = 2; /* DAT reads stall the frame: at most two pictures per frame */
    pvr_wait_ready();
    /* Where the gradient quad does not draw, show a mid blue instead of black. */
    pvr_set_bg_color(0.45f, 0.60f, 0.80f);
    pvr_scene_begin();

    pvr_poly_cxt_t cxt;
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    submit_header(&cxt);
    send_vertex(0.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(640.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(0.0f, 480.0f, BG_Z, 0, 0, bottom, 0);
    send_vertex(640.0f, 480.0f, BG_Z, 0, 0, bottom, 1);
    cmd_flush();
    pvr_list_finish();

    pvr_list_begin(PVR_LIST_TR_POLY);
    hdr_valid = 0;
}

void
gfx_end_frame(void) {
    cmd_flush();
    pvr_list_finish();
    pvr_scene_finish();
}
