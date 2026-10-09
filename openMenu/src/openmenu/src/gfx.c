/*
 * gfx: PVR backend for the BIOS-style menu, see gfx.h.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <dc/biosfont.h>
#include <dc/pvr.h>
#include <fcntl.h>
#include <kos/fs.h>

#include <bios_menu.h>

#include <bios_menu.h>

#include "gfx.h"

#define MAX_LOGO_FILE (256 * 1024)
#define MAX_TEXTURES 96
#define MAX_TEXT_ENTRIES 48
#define MAX_TEXT_CHARS 42
#define MAX_TEXT_W 512
#define MAX_LABELS 16
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
/* Submission method. 0 (default): pvr_prim(), one call per triangle. 1: direct rendering,
 * writing straight to the store queues; faster in principle, but it broke the translucent
 * list on real hardware in the first try, so it is opt-in (-DGFX_USE_DR=1) until it is
 * understood. */
#ifndef GFX_USE_DR
#define GFX_USE_DR 0
#endif

/* Translucent polygons: 1 = drawn in the order they are submitted (no hardware sorting, much
 * cheaper; the menu submits back to front already), 0 = the PVR sorts them per pixel. */
#ifndef GFX_PRESORT
#define GFX_PRESORT 1
#endif

#if GFX_USE_DR
static pvr_dr_state_t dr_state; /* direct rendering: writes go straight to the store queues */
static int dr_active;

/* pvr_txr_load() uses the store queues too, which direct rendering holds locked: give them
 * back around texture uploads that happen while a frame is being built. */
static void
dr_suspend(void) {
    if (dr_active) {
        pvr_dr_finish();
    }
}

static void
dr_resume(void) {
    if (dr_active) {
        pvr_dr_init(&dr_state);
    }
}
#else
#define dr_suspend() ((void)0)
#define dr_resume() ((void)0)
#endif

static struct {
    uint16_t id;
    char text[MAX_TEXT_CHARS + 1];
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

static rom_tex*
get_rom_texture(bscene_texref ref) {
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

static void
submit_header(pvr_poly_cxt_t* cxt) {
#if GFX_USE_DR
    pvr_poly_hdr_t* target = (pvr_poly_hdr_t*)pvr_dr_target(dr_state);
    pvr_poly_compile(target, cxt);
    pvr_dr_commit(target);
#else
    pvr_poly_hdr_t hdr;
    pvr_poly_compile(&hdr, cxt);
    pvr_prim(&hdr, sizeof(hdr));
#endif
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
static pvr_vertex_t vbuf[4] __attribute__((aligned(32)));
static int vcount;

static void
submit_vertices(int n) {
#if GFX_USE_DR
    for (int i = 0; i < n; i++) {
        pvr_vertex_t* t = pvr_dr_target(dr_state);
        t->flags = vbuf[i].flags;
        t->x = vbuf[i].x;
        t->y = vbuf[i].y;
        t->z = vbuf[i].z;
        t->u = vbuf[i].u;
        t->v = vbuf[i].v;
        t->argb = vbuf[i].argb;
        t->oargb = 0;
        pvr_dr_commit(t);
    }
#else
    pvr_prim(vbuf, n * (int)sizeof(pvr_vertex_t));
#endif
}

static void
send_vertex(float x, float y, float z, float u, float v, uint32_t argb, int last) {
    pvr_vertex_t* vert = &vbuf[vcount++];
    vert->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = x;
    vert->y = y;
    vert->z = z;
    vert->u = u;
    vert->v = v;
    vert->argb = argb;
    vert->oargb = 0;
    if (last) {
        submit_vertices(vcount);
        vcount = 0;
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

static void
sink_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    const char* label = find_label(obj->id);
    if (label) {
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
            strncpy(labels[i].text, text, MAX_TEXT_CHARS);
            labels[i].text[MAX_TEXT_CHARS] = '\0';
            return;
        }
    }
    if (num_labels < MAX_LABELS) {
        labels[num_labels].id = obj_id;
        strncpy(labels[num_labels].text, text, MAX_TEXT_CHARS);
        labels[num_labels].text[MAX_TEXT_CHARS] = '\0';
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
    dr_suspend();
    pvr_txr_load(text_canvas, victim->ptr, (size_t)w * GFX_LINE_H * 2);
    dr_resume();
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

void
gfx_rect(float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(NULL, NULL, 0);
    send_vertex(x, y, z, 0, 0, argb, 0);
    send_vertex(x + w, y, z, 0, 0, argb, 0);
    send_vertex(x, y + h, z, 0, 0, argb, 0);
    send_vertex(x + w, y + h, z, 0, 0, argb, 1);
}

/* ---- Frames ------------------------------------------------------------------------------ */

void
gfx_begin_frame(uint32_t top, uint32_t bottom) {
    frame_no++;
    tri_count = 0;
    pvr_wait_ready();
    /* Where the gradient quad does not draw, show a mid blue instead of black. */
    pvr_set_bg_color(0.45f, 0.60f, 0.80f);
    pvr_scene_begin();

    pvr_poly_cxt_t cxt;
    pvr_list_begin(PVR_LIST_OP_POLY);
#if GFX_USE_DR
    pvr_dr_init(&dr_state);
    dr_active = 1;
#endif
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    submit_header(&cxt);
    send_vertex(0.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(640.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(0.0f, 480.0f, BG_Z, 0, 0, bottom, 0);
    send_vertex(640.0f, 480.0f, BG_Z, 0, 0, bottom, 1);
#if GFX_USE_DR
    pvr_dr_finish();
    dr_active = 0;
#endif
    pvr_list_finish();

    pvr_list_begin(PVR_LIST_TR_POLY);
#if GFX_USE_DR
    pvr_dr_init(&dr_state);
    dr_active = 1;
#endif
    hdr_valid = 0;
}

void
gfx_end_frame(void) {
#if GFX_USE_DR
    pvr_dr_finish();
    dr_active = 0;
#endif
    pvr_list_finish();
    pvr_scene_finish();
}
