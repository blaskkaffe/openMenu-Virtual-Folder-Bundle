/*
 * bios_scene: BIOS menu state to screen-space triangles, see bios_scene.h.
 */
#include "bios_scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR_Z (-1.0f) /* anything closer than this to the camera plane is dropped */
#define MAX_MESH_VERTS 4096 /* nj_model never produces more */

/* Lighting, as the BIOS sets it up (gui_main 0x8C0105D4): one directional light along the view axis,
 * njSetLightIntensity(light, spc 1.4, dif 0.3, amb 0), njSetAmbient(0.5, 0.5, 0.5). See bios_lit(). */
#define LIGHT_AMBIENT 0.5f
#define LIGHT_DIFFUSE 0.3f
#define LIGHT_SPECULAR 1.4f
#define STRIP_IGNORE_LIGHT 0x01
#define STRIP_IGNORE_SPECULAR 0x02
#define STRIP_IGNORE_AMBIENT 0x04
#define STRIP_DOUBLE_SIDED 0x10
#define STRIP_ENV 0x40 /* environment mapping: u, v come from the vertex normal, not from the file */

/* Per-object scratch for projected vertices (the engine is single threaded). */
static float scratch_x[MAX_MESH_VERTS], scratch_y[MAX_MESH_VERTS], scratch_w[MAX_MESH_VERTS];
static float scratch_d[MAX_MESH_VERTS]; /* d = max(0, -(L.N)) per vertex (1 when the vertex has no normal) */
static float scratch_eu[MAX_MESH_VERTS], scratch_ev[MAX_MESH_VERTS]; /* environment map u, v per vertex */
static uint8_t scratch_ok[MAX_MESH_VERTS];
/* Lit colours of the strip being drawn, per vertex: a vertex is shared by up to three triangles of a strip
 * (and by neighbouring strips of the same material), so its colour is worked out once. */
static uint32_t lit_stamp[MAX_MESH_VERTS], lit_argb[MAX_MESH_VERTS], lit_oargb[MAX_MESH_VERTS];
static uint32_t lit_now;

void
bscene_init(bscene* s, const bios_rom* rom) {
    bscene_cache_clear();
    memset(s, 0, sizeof(*s));
    s->rom = rom;
    s->parts = BSCENE_PART_ALL;
}

void
bscene_free(bscene* s) {
    for (int i = 0; i < BSCENE_MODEL_CACHE; i++) {
        if (s->model_state[i] == 1) {
            nj_object_free(&s->models[i]);
        }
    }
    for (int i = 0; i < BMODEL_COUNT; i++) {
        if (s->custom_state[i] == 1) {
            nj_object_free(&s->custom[i]);
        }
    }
    for (int i = 0; i < s->motion_count; i++) {
        if (s->motions[i].state == 1) {
            nj_motion_free(&s->motions[i].data);
        }
    }
    memset(s, 0, sizeof(*s));
}

static const nj_object*
get_model(bscene* s, int idx) {
    if (idx >= BMODEL_BASE && idx < BMODEL_END) {
        int c = idx - BMODEL_BASE;
        if (s->custom_state[c] == 0) {
            s->custom_state[c] = bmodel_build(idx, &s->custom[c]) == 0 ? 1 : 2;
        }
        return s->custom_state[c] == 1 ? &s->custom[c] : NULL;
    }
    if (idx < 0 || idx >= BSCENE_MODEL_CACHE) {
        return NULL;
    }
    if (s->model_state[idx] == 0) {
        uint32_t addr = bios_model_addr(s->rom, idx);
        s->model_state[idx] = (addr && nj_object_load(s->rom, addr, &s->models[idx]) == 0) ? 1 : 2;
    }
    return s->model_state[idx] == 1 ? &s->models[idx] : NULL;
}

static const nj_motion*
get_motion(bscene* s, int model, int motion, int node_count) {
    for (int i = 0; i < s->motion_count; i++) {
        if (s->motions[i].model == model && s->motions[i].motion == motion) {
            return s->motions[i].state == 1 ? &s->motions[i].data : NULL;
        }
    }
    if (s->motion_count >= BSCENE_MOTION_CACHE) {
        return NULL;
    }
    int n = s->motion_count++;
    s->motions[n].model = model;
    s->motions[n].motion = motion;
    uint32_t addr = bios_motion_addr(s->rom, motion);
    s->motions[n].state = (addr && nj_motion_load(s->rom, addr, node_count, &s->motions[n].data) == 0) ? 1 : 2;
    return s->motions[n].state == 1 ? &s->motions[n].data : NULL;
}

int
bscene_project(nj_vec3 p, float* sx, float* sy, float* invw) {
    if (p.z > NEAR_Z) {
        return 0;
    }
    float iw = 1.0f / -p.z;
    *sx = BSCENE_SCREEN_W / 2 + BSCENE_FOCAL * p.x * iw;
    *sy = BSCENE_SCREEN_H / 2 - BSCENE_FOCAL * p.y * iw;
    *invw = iw;
    return 1;
}


/* ---- object output cache --------------------------------------------------------------------------------
 * Most objects look the same from one frame to the next (the icons at rest, the panel, the BACK marker, rows
 * that do not move). Their triangles are kept, keyed by everything that goes into drawing them, and sent again
 * without transforming, lighting or projecting anything. When the pool is full everything is dropped and filled
 * again from what the next frames draw. */
#define CACHE_TRIS 6144
#define CACHE_ENTRIES 128

typedef struct {
    uint32_t flags;
    uint16_t model, motion, texlist, pad;
    float pos[3];
    int32_t rot[3];
    float scl[3];
    float color[4];
    float mframe;
    int stretch_on;
    float stretch_a, stretch_b, stretch_f;
    int no_decals, panel_on, fullbright;
    float panel_fx, panel_fy;
    uint32_t panel_accent;
    int ovr_n;
    struct {
        int node, poly;
        uint32_t argb;
        int lit;
    } ovr[BSCENE_OVR_MAX];
} draw_key;

typedef struct {
    bscene_vtx v[3];
    bscene_texref tex;
} cached_tri;

typedef struct {
    draw_key key;
    uint32_t hash;
    int first, count;
    int valid;
} cache_entry;

static cached_tri cache_pool[CACHE_TRIS];
static int cache_pool_n;
static cache_entry cache_entries[CACHE_ENTRIES];
static int cache_entry_n;
static int cache_disabled;

/* Drawing states seen before: only a state that is drawn a second time is kept (an object that moves every frame
 * would only fill the pool). Keyed by the state's hash, so objects built on the fly (panels, rows) count too. */
#define SEEN_SLOTS 512
static uint32_t seen[SEEN_SLOTS]; /* hash | 1, 0 = free */
static int seen_n;

static int
seen_before(uint32_t hash) {
    uint32_t key = hash | 1u, i = (hash >> 1) & (SEEN_SLOTS - 1);
    for (;;) {
        if (seen[i] == key) {
            return 1;
        }
        if (!seen[i]) {
            break;
        }
        i = (i + 1) & (SEEN_SLOTS - 1);
    }
    if (seen_n >= SEEN_SLOTS * 3 / 4) { /* fairly full: start over */
        memset(seen, 0, sizeof(seen));
        seen_n = 0;
        i = (hash >> 1) & (SEEN_SLOTS - 1);
    }
    seen[i] = key;
    seen_n++;
    return 0;
}

/* capture state while an object is drawn for the first time */
static const bscene_sink* cap_out;
static int cap_first, cap_overflow;

static void
cache_flush(void) {
    cache_pool_n = 0;
    cache_entry_n = 0;
}

static void
seen_clear(void) {
    memset(seen, 0, sizeof(seen));
    seen_n = 0;
}

void
bscene_cache_clear(void) {
    cache_flush();
    seen_clear();
}

void
bscene_cache_enable(int on) {
    cache_disabled = !on;
    bscene_cache_clear();
}

static uint32_t
key_hash(const draw_key* k) {
    const uint32_t* w = (const uint32_t*)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*k) / 4; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h;
}

static void
make_key(const bscene* s, const bvm_obj* o, const float scl[3], const float* offs, draw_key* k) {
    memset(k, 0, sizeof(*k));
    k->flags = o->flags;
    k->model = o->model;
    k->motion = (o->flags & BVM_F_MOTION) ? o->motion : 0;
    k->texlist = o->texlist;
    memcpy(k->pos, o->pos, sizeof(k->pos));
    memcpy(k->rot, o->rot, sizeof(k->rot));
    memcpy(k->scl, scl, sizeof(k->scl));
    if (offs) {
        memcpy(k->color, offs, sizeof(k->color));
    }
    k->mframe = (o->flags & BVM_F_MOTION) ? o->motion_tw.cur : 0.0f;
    if (s->stretch_on) {
        k->stretch_on = 1;
        k->stretch_a = s->stretch_a;
        k->stretch_b = s->stretch_b;
        k->stretch_f = s->stretch_f;
    }
    k->no_decals = s->no_decals;
    k->fullbright = s->fullbright;
    if (s->panel_on) {
        k->panel_on = 1;
        k->panel_fx = s->panel_fx;
        k->panel_fy = s->panel_fy;
        k->panel_accent = s->panel_accent;
    }
    for (int i = 0; i < s->ovr_n && i < BSCENE_OVR_MAX; i++) {
        if (s->ovr[i].model == o->model) {
            k->ovr[k->ovr_n].node = s->ovr[i].node;
            k->ovr[k->ovr_n].poly = s->ovr[i].poly;
            k->ovr[k->ovr_n].argb = s->ovr[i].argb;
            k->ovr[k->ovr_n].lit = s->ovr[i].lit;
            k->ovr_n++;
        }
    }
}

static void
cap_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user;
    if (!cap_overflow) {
        if (cache_pool_n < CACHE_TRIS) {
            cached_tri* t = &cache_pool[cache_pool_n++];
            memcpy(t->v, v, sizeof(t->v));
            t->tex = tex;
        } else {
            cap_overflow = 1;
        }
    }
    cap_out->triangle(cap_out->user, v, tex);
}

static void
cap_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    if (cap_out->text) {
        cap_out->text(cap_out->user, obj, x, y, invw);
    }
}

static const bscene_sink cap_sink = {NULL, cap_triangle, cap_text};

/* Colours are worked out in floats as the BIOS does and packed with a float to *signed* int conversion
 * (ftrc, cheap on the SH-4; float to unsigned is a slow library call there). */
typedef struct {
    float a, r, g, b;
} fcolour;

static float
clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* The BIOS packs a lit colour with clamp to 0..1, times 255, truncate (0x8C0DF07C). */
static uint32_t
pack(fcolour c) {
    return ((uint32_t)(int)(clamp01(c.a) * 255.0f) << 24) | ((uint32_t)(int)(clamp01(c.r) * 255.0f) << 16)
         | ((uint32_t)(int)(clamp01(c.g) * 255.0f) << 8) | (uint32_t)(int)(clamp01(c.b) * 255.0f);
}

/* A material colour as the BIOS keeps it: ARGB bytes / 255 (0x8C0A1D08) plus the object's constant material.
 * The BIOS runs in offset-material mode, njControl3D(0x20) (merge at 0x8C094C52): diffuse += constant, and the
 * ambient colour is replaced by that diffuse, so the ambient chunks in the files never take part. */
static fcolour
material(uint32_t argb, const float* offs) {
    fcolour c = {(float)(argb >> 24) * (1.0f / 255.0f), (float)((argb >> 16) & 255) * (1.0f / 255.0f),
                 (float)((argb >> 8) & 255) * (1.0f / 255.0f), (float)(argb & 255) * (1.0f / 255.0f)};
    if (offs) {
        c.a += offs[0];
        c.r += offs[1];
        c.g += offs[2];
        c.b += offs[3];
    }
    return c;
}

/* The BIOS lit-vertex colour (0x8C098D60, directional light 0x8C0989D8):
 *     rgb = 0.5 * M.rgb (unless the strip ignores ambient) + 0.3 * d * M.rgb,   alpha = M.a
 * with d = max(0, -(L.N)), L = (0, 0, -1) in view space. */
static uint32_t
bios_lit(fcolour m, int ambient, float d) {
    float k = (ambient ? LIGHT_AMBIENT : 0.0f) + LIGHT_DIFFUSE * d;
    fcolour c = {m.a, m.r * k, m.g * k, m.b * k};
    return pack(c);
}

/* The specular term of a textured strip, the PVR offset colour (0x8C098968): 1.4 * spec.rgb * (d * d)^P[n], n = the
 * specular colour's alpha byte. Untextured strips never show it. Returns 0xFF000000 | rgb, 0 for none. */
static uint32_t
bios_offset(uint32_t specular, float d) {
    static const float P[17] = {0, 0.5f, 1, 1.5f, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128};
    int n = (int)(specular >> 24);
    float e = n < 17 ? P[n] : 128.0f, x = d * d;
    float k = 1.0f;
    if (d <= 0.0f) {
        return 0;
    }
    if (e > 0.0f) {
        int whole = (int)e;
        for (int i = 0; i < whole && k > 0.0005f; i++) {
            k *= x;
        }
        if (e - (float)whole > 0.0f) {
            k *= sqrtf(x);
        }
    }
    fcolour c = {1.0f, (float)((specular >> 16) & 255) * (LIGHT_SPECULAR / 255.0f) * k,
                 (float)((specular >> 8) & 255) * (LIGHT_SPECULAR / 255.0f) * k, (float)(specular & 255) * (LIGHT_SPECULAR / 255.0f) * k};
    return pack(c);
}

void
bscene_draw_panel(bscene* s, float x, float y, float w, float h, uint32_t accent, const bscene_sink* sink) {
    /* the model's corners sit at +-10 units; the panel is w/25 units half wide (decompile: panel_mesh_fit_rect) */
    float units_per_px = -BSCENE_PANEL_Z / 4000.0f;
    bvm_obj o;
    memset(&o, 0, sizeof(o));
    o.active = 1;
    o.flags = BVM_F_MODEL;
    o.model = BSCENE_PANEL_MODEL;
    o.pos[0] = (x + w / 2.0f - 320.0f) * units_per_px;
    o.pos[1] = (240.0f - (y + h / 2.0f)) * units_per_px;
    o.pos[2] = BSCENE_PANEL_Z;
    o.scale_tw[0].cur = o.scale_tw[1].cur = o.scale_tw[2].cur = 1.0f;
    s->panel_on = 1;
    s->panel_fx = w * units_per_px / 2.0f - 10.0f;
    s->panel_fy = h * units_per_px / 2.0f - 10.0f;
    s->panel_accent = accent;
    unsigned saved = s->parts;
    s->parts = BSCENE_PART_MODEL;
    bscene_draw_object(s, &o, sink);
    s->parts = saved;
    s->panel_on = 0;
}

/* The face of a row disc is a square with the picture mapped over it. Game pictures are square, so
 * the face is drawn as a circle (a fan inscribed in the square, same texture coordinates): the disc
 * keeps its round shape whatever the picture. */
#define BSCENE_ROUND_FACE_TEXLIST 0x1000
#define ROUND_SEGMENTS 20 /* a disc is about 40 px wide: more segments cost triangles and show nothing */
static void
draw_round_face(const nj_mesh* mesh, const nj_poly* poly, const nj_mat4* m, uint32_t argb, bscene_texref tex, const bscene_sink* sink) {
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, z = 0.0f;
    for (int i = 0; i < 6; i++) {
        const nj_vec3 p = mesh->verts[poly->corners[i].idx].pos;
        minx = p.x < minx ? p.x : minx;
        maxx = p.x > maxx ? p.x : maxx;
        miny = p.y < miny ? p.y : miny;
        maxy = p.y > maxy ? p.y : maxy;
        z = p.z;
    }
    float cx = (minx + maxx) / 2.0f, cy = (miny + maxy) / 2.0f, rx = (maxx - minx) / 2.0f, ry = (maxy - miny) / 2.0f;
    static float tcs[ROUND_SEGMENTS], tsn[ROUND_SEGMENTS];
    static int table_ok;
    if (!table_ok) {
        for (int i = 0; i < ROUND_SEGMENTS; i++) {
            float a = 6.2831853f * (float)i / ROUND_SEGMENTS;
            tcs[i] = cosf(a);
            tsn[i] = sinf(a);
        }
        table_ok = 1;
    }
    bscene_vtx pts[ROUND_SEGMENTS + 1];
    int ok[ROUND_SEGMENTS + 1];
    for (int i = 0; i <= ROUND_SEGMENTS; i++) {
        float cs = tcs[i % ROUND_SEGMENTS], sn = tsn[i % ROUND_SEGMENTS];
        nj_vec3 w = nj_mat_apply(m, (nj_vec3){cx + rx * cs, cy + ry * sn, z});
        ok[i] = bscene_project(w, &pts[i].x, &pts[i].y, &pts[i].invw);
        pts[i].u = 0.5f + 0.5f * cs;
        pts[i].v = 0.5f - 0.5f * sn;
        pts[i].argb = argb;
        pts[i].oargb = 0;
    }
    bscene_vtx c;
    nj_vec3 wc = nj_mat_apply(m, (nj_vec3){cx, cy, z});
    int cok = bscene_project(wc, &c.x, &c.y, &c.invw);
    c.u = c.v = 0.5f;
    c.argb = argb;
    c.oargb = 0;
    for (int i = 0; i < ROUND_SEGMENTS; i++) {
        bscene_vtx v[3] = {c, pts[i], pts[i + 1]};
        float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (cok && ok[i] && ok[i + 1] && area < 0.0f) {
            sink->triangle(sink->user, v, tex);
        }
    }
}

void
bscene_draw_model(bscene* s, int model, float cx, float cy, float scale, const float rot_deg[3], const bscene_sink* sink) {
    float units_per_px = -BSCENE_PANEL_Z / 4000.0f;
    bvm_obj o;
    memset(&o, 0, sizeof(o));
    o.active = 1;
    o.flags = BVM_F_MODEL;
    o.model = model;
    o.texlist = model;
    o.pos[0] = (cx - 320.0f) * units_per_px;
    o.pos[1] = (240.0f - cy) * units_per_px;
    o.pos[2] = BSCENE_PANEL_Z;
    o.scale_tw[0].cur = o.scale_tw[1].cur = o.scale_tw[2].cur = scale;
    for (int k = 0; k < 3; k++) {
        o.rot[k] = (int32_t)(rot_deg[k] * (65536.0f / 360.0f));
    }
    unsigned saved = s->parts;
    s->parts = BSCENE_PART_MODEL;
    bscene_draw_object(s, &o, sink);
    s->parts = saved;
}

/* A grid mesh made by script ops 0x80..0x85 with deformer 5 (mesh_deform_mode5 0x8C0167AA): the copy box's
 * progress bar. cols x rows cells of (cw, ch) / 10 units; var0 = progress (256 a column): the middle rows of the
 * filled columns are opaque, the rest clear; colours cycle with var0 and the texture coordinates are noise
 * (rand() / 32768 * 0.1953) over GBIX texture `mesh_tex`. */
static uint32_t mesh_seed = 12345u;

static void
draw_grid_mesh(const bscene* s, const bvm_obj* o, const nj_mat4* m, const float* offs, const bscene_sink* sink) {
    int cols = o->mesh_cols, rows = o->mesh_rows;
    if (cols < 1 || rows < 1 || cols > 32 || rows > 8 || o->mesh_mode != 5) {
        return;
    }
    (void)s;
    int v = o->var[0], full = v / 256, part = (v % 256) * 255 / 256;
    static bscene_vtx vt[33][9];
    static int ok[33][9];
    for (int i = 0; i <= cols; i++) {
        for (int j = 0; j <= rows; j++) {
            nj_vec3 p = {(float)(i * o->mesh_cw - cols * o->mesh_cw / 2) / 10.0f, (float)(j * o->mesh_ch - rows * o->mesh_ch / 2) / 10.0f,
                         0.0f};
            bscene_vtx* x = &vt[i][j];
            ok[i][j] = bscene_project(nj_mat_apply(m, p), &x->x, &x->y, &x->invw);
            mesh_seed = mesh_seed * 1103515245u + 12345u;
            x->u = (float)((mesh_seed >> 16) & 0x7FFF) / 32768.0f * 0.1953f;
            mesh_seed = mesh_seed * 1103515245u + 12345u;
            x->v = (float)((mesh_seed >> 16) & 0x7FFF) / 32768.0f * 0.1953f;
            float r = 127.0f * sinf((float)(i * 1000 + v * 15) * (6.2831853f / 65536.0f)) + 127.0f;
            float g = 127.0f * cosf((float)(i * 2000 + v * 20) * (6.2831853f / 65536.0f)) + 127.0f;
            float b = 127.0f * sinf((float)(i * 3000 + v * 21 + 0x4000) * (6.2831853f / 65536.0f)) + 127.0f;
            int a = 0;
            if (j >= 1 && j <= 3 && i >= 1) {
                a = i <= full ? 255 : (i == full + 1 ? part : 0);
            }
            if (offs) {
                a += (int)(offs[0] * 255.0f);
                a = a < 0 ? 0 : (a > 255 ? 255 : a);
            }
            x->argb = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            x->oargb = 0;
        }
    }
    bscene_texref tex = {BSCENE_TEX_GBIX, o->mesh_tex, 0};
    for (int i = 0; i < cols; i++) {
        for (int j = 0; j < rows; j++) {
            if (!(ok[i][j] && ok[i + 1][j] && ok[i][j + 1] && ok[i + 1][j + 1])) {
                continue;
            }
            bscene_vtx t1[3] = {vt[i][j], vt[i + 1][j], vt[i][j + 1]};
            bscene_vtx t2[3] = {vt[i + 1][j], vt[i + 1][j + 1], vt[i][j + 1]};
            sink->triangle(sink->user, t1, tex);
            sink->triangle(sink->user, t2, tex);
        }
    }
}

static float text_fade; /* bscene.fade of the scene drawn last, for bscene_text_alpha() */

float
bscene_text_alpha(const bvm_obj* o) {
    return (o->flags & BVM_F_FLAG17) ? 1.0f - text_fade : 1.0f;
}

nj_node*
bscene_node(bscene* s, int model, int node) {
    const nj_object* obj = get_model(s, model);
    return obj && node >= 0 && node < obj->count ? &obj->nodes[node] : NULL;
}

/* A window panel an object made with panel_create (popups, the copy box): model 39 with its corners moved out
 * by w / 25 - 10 and h / 25 - 10 units (panel_mesh_fit_rect 0x8C022500), at the object's place. */
static void
draw_object_panel(bscene* s, const bvm_obj* o, const bscene_sink* sink) {
    bvm_obj p = *o;
    p.flags = BVM_F_MODEL | (o->flags & BVM_F_FLAG17);
    p.model = p.texlist = BSCENE_PANEL_MODEL;
    unsigned saved = s->parts;
    s->parts = BSCENE_PART_MODEL;
    s->panel_on = 1;
    s->panel_fx = (float)o->panel_w / 25.0f - 10.0f;
    s->panel_fy = (float)o->panel_h / 25.0f - 10.0f;
    s->panel_accent = s->object_panel_accent ? s->object_panel_accent : 0xFFE0E0E0u;
    bscene_draw_object(s, &p, sink);
    s->panel_on = 0;
    s->parts = saved;
}

void
bscene_draw_object(bscene* s, const bvm_obj* o, const bscene_sink* sink) {
    if (!o->active || (o->flags & BVM_F_HIDE)) {
        return;
    }
    text_fade = s->fade;
    if ((o->flags & BVM_F_PANEL) && o->panel_w > 0 && o->panel_h > 0 && (s->parts & BSCENE_PART_MODEL)) {
        draw_object_panel(s, o, sink);
    }

    float scl[3] = {o->scale_tw[0].cur, o->scale_tw[1].cur, o->scale_tw[2].cur};
    nj_mat4 obj_m;
    nj_mat_object(&obj_m, o->pos, scl, o->rot);
    /* constant material (a, r, g, b offsets), and the screen transition's fade */
    float eoff[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float* offs = NULL;
    if (o->flags & BVM_F_COLOUR) {
        memcpy(eoff, o->color, sizeof(eoff));
        offs = eoff;
    }
    if ((o->flags & BVM_F_FLAG17) && s->fade > 0.0f) {
        eoff[0] -= s->fade;
        offs = eoff;
    }

    /* replay, or capture, the model's triangles (see the object output cache) */
    cache_entry* ce = NULL;
    const bscene_sink* real_sink = sink;
    if ((o->flags & BVM_F_MODEL) && (s->parts & BSCENE_PART_MODEL) && !cache_disabled) {
        draw_key key;
        make_key(s, o, scl, offs, &key);
        uint32_t h = key_hash(&key);
        for (int i = 0; i < cache_entry_n; i++) {
            cache_entry* e = &cache_entries[i];
            if (e->valid && e->hash == h && !memcmp(&e->key, &key, sizeof(key))) {
                for (int t = 0; t < e->count; t++) {
                    const cached_tri* ct = &cache_pool[e->first + t];
                    sink->triangle(sink->user, ct->v, ct->tex);
                }
                goto model_done;
            }
        }
        if (!seen_before(h)) {
            goto draw_model; /* changed since its last draw: draw it, keep nothing */
        }
        if (cache_entry_n >= CACHE_ENTRIES || cache_pool_n > CACHE_TRIS - 256) {
            cache_flush();
        }
        ce = &cache_entries[cache_entry_n++];
        ce->key = key;
        ce->hash = h;
        ce->valid = 0;
        ce->first = cache_pool_n;
        cap_out = sink;
        cap_first = cache_pool_n;
        cap_overflow = 0;
        sink = &cap_sink;
    }
draw_model:
    if ((o->flags & BVM_F_MODEL) && (s->parts & BSCENE_PART_MODEL)) {
        const nj_object* obj = get_model(s, o->model);
        if (obj) {
            const nj_motion* mo = (o->flags & BVM_F_MOTION) ? get_motion(s, o->model, o->motion, obj->count) : NULL;
            nj_mat4* world = s->pose;
            nj_object_pose(obj, mo, o->motion_tw.cur, world);

            for (int n = 0; n < obj->count; n++) {
                const nj_mesh* mesh = obj->nodes[n].mesh;
                if (!mesh || (obj->nodes[n].eval & NJ_EVAL_HIDE)) {
                    continue;
                }
                nj_mat4 m;
                nj_mat_mul(&m, &obj_m, &world[n]);

                /* Environment-mapped strips take u, v from the normal; work those out only if the mesh has one. */
                int any_env = 0;
                for (int p = 0; p < mesh->npolys; p++) {
                    any_env |= mesh->polys[p].strip_flags & STRIP_ENV;
                }
                /* Transform and project every vertex once; triangles only index into this. */
                for (int i = 0; i < mesh->nverts; i++) {
                    const nj_vertex* vx = &mesh->verts[i];
                    scratch_ok[i] = 0;
                    if (vx->valid) {
                        nj_vec3 src = vx->pos;
                        if (s->panel_on) { /* 37 vertices per corner: 0 top left, 1 top right, 2 bottom right, 3 bottom left */
                            int q = i / 37;
                            src.x += (q == 1 || q == 2) ? s->panel_fx : -s->panel_fx;
                            src.y += (q == 2 || q == 3) ? s->panel_fy : -s->panel_fy;
                        }
                        if (s->stretch_on) {
                            if (src.x > s->stretch_b) {
                                src.x = s->stretch_a + (s->stretch_b - s->stretch_a) * s->stretch_f + (src.x - s->stretch_b);
                            } else if (src.x > s->stretch_a) {
                                src.x = s->stretch_a + (src.x - s->stretch_a) * s->stretch_f;
                            }
                        }
                        nj_vec3 w = nj_mat_apply(&m, src);
                        scratch_ok[i] = (uint8_t)bscene_project(w, &scratch_x[i], &scratch_y[i], &scratch_w[i]);
                        {
                            /* 0x8C0989D8 with the light (0, 0, -1) in view space: d = max(0, nz) of the transformed normal,
                             * not renormalised (the object's scale counts) */
                            float d = 1.0f;
                            if (vx->has_nrm) {
                                d = m.m[2][0] * vx->nrm.x + m.m[2][1] * vx->nrm.y + m.m[2][2] * vx->nrm.z;
                                d = d > 0.0f ? d : 0.0f;
                            }
                            scratch_d[i] = s->fullbright ? 1.0f : d;
                        }
                        if (!any_env) {
                            /* nothing reads scratch_eu / scratch_ev */
                        } else if (vx->has_nrm) {
                            /* the normal in view space picks the point of the picture (a sphere map) */
                            float nx = m.m[0][0] * vx->nrm.x + m.m[0][1] * vx->nrm.y + m.m[0][2] * vx->nrm.z;
                            float ny = m.m[1][0] * vx->nrm.x + m.m[1][1] * vx->nrm.y + m.m[1][2] * vx->nrm.z;
                            float nz = m.m[2][0] * vx->nrm.x + m.m[2][1] * vx->nrm.y + m.m[2][2] * vx->nrm.z;
                            float len = sqrtf(nx * nx + ny * ny + nz * nz);
                            if (len > 1e-6f) {
                                nx /= len;
                                ny /= len;
                            }
                            scratch_eu[i] = 0.5f + 0.5f * nx;
                            scratch_ev[i] = 0.5f - 0.5f * ny;
                        } else {
                            scratch_eu[i] = scratch_ev[i] = 0.5f;
                        }
                    }
                }

                for (int p = 0; p < mesh->npolys; p++) {
                    const nj_poly* poly = &mesh->polys[p];
                    if (s->no_decals && poly->tex == 0) { /* texture 0 of a button is its picture */
                        continue;
                    }
                    bscene_texref tex = {BSCENE_TEX_NONE, 0, 0};
                    if (poly->tex >= 0) {
                        tex.kind = BSCENE_TEX_TEXLIST;
                        tex.a = o->texlist;
                        tex.b = poly->tex;
                    }
                    uint32_t diffuse = poly->has_diffuse ? poly->diffuse : 0u;
                    int forced = 0; /* a colour the scripts pick is shown as it is */
                    uint32_t forced_argb = 0;
                    for (int k = 0; k < s->ovr_n; k++) {
                        if (s->ovr[k].model == o->model && s->ovr[k].node == n && s->ovr[k].poly == p) {
                            if (s->ovr[k].lit) {
                                diffuse = s->ovr[k].argb;
                            } else {
                                forced_argb = s->ovr[k].argb;
                                forced = 1;
                            }
                        }
                    }
                    const fcolour mat = material(diffuse, offs);
                    uint32_t poly_argb = poly->has_diffuse ? pack(mat) : 0;
                    if (s->panel_on && p == 0) {
                        poly_argb = s->panel_accent; /* the rim */
                        forced = 1;
                    } else if (forced) {
                        poly_argb = forced_argb;
                    }
                    if (forced && offs && offs[0] != 0.0f) { /* the constant alpha still fades a forced colour */
                        float a = (float)(poly_argb >> 24) + offs[0] * 255.0f;
                        a = a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a);
                        poly_argb = (poly_argb & 0x00FFFFFFu) | ((uint32_t)a << 24);
                    }
                    if (poly->tex == 0 && o->texlist >= BSCENE_ROUND_FACE_TEXLIST && poly->has_uv && poly->ntris == 2) {
                        draw_round_face(mesh, poly, &m, poly_argb, tex, sink);
                        continue;
                    }
                    int lit = !(poly->strip_flags & STRIP_IGNORE_LIGHT);
                    const int amb_on = !(poly->strip_flags & STRIP_IGNORE_AMBIENT);
                    const int spec_on = poly->tex >= 0 && poly->has_specular && !(poly->strip_flags & STRIP_IGNORE_SPECULAR);
                    int cull = !(poly->strip_flags & STRIP_DOUBLE_SIDED);
                    const int use_lit = lit && poly->has_diffuse && !forced && !s->fullbright;
                    if (++lit_now == 0) { /* stamps wrapped: forget them all */
                        memset(lit_stamp, 0, sizeof(lit_stamp));
                        lit_now = 1;
                    }
                    for (int t = 0; t < poly->ntris; t++) {
                        bscene_vtx v[3];
                        int ok = 1;
                        for (int k = 0; k < 3 && ok; k++) {
                            const nj_corner* c = &poly->corners[t * 3 + k];
                            ok = scratch_ok[c->idx];
                            v[k].x = scratch_x[c->idx];
                            v[k].y = scratch_y[c->idx];
                            v[k].invw = scratch_w[c->idx];
                            if (poly->strip_flags & STRIP_ENV) {
                                v[k].u = scratch_eu[c->idx];
                                v[k].v = scratch_ev[c->idx];
                            } else {
                                v[k].u = c->u;
                                v[k].v = c->v;
                            }
                            uint32_t base = poly_argb;
                            if (!poly->has_diffuse) {
                                const nj_vertex* vx = &mesh->verts[c->idx];
                                base = pack(material(vx->has_col ? vx->col : 0xFFFFFFFFu, offs));
                            }
                            v[k].oargb = 0;
                            if (use_lit && mesh->verts[c->idx].has_nrm) {
                                if (lit_stamp[c->idx] != lit_now) {
                                    lit_stamp[c->idx] = lit_now;
                                    lit_argb[c->idx] = bios_lit(mat, amb_on, scratch_d[c->idx]);
                                    lit_oargb[c->idx] = spec_on ? bios_offset(poly->specular, scratch_d[c->idx]) : 0;
                                }
                                v[k].argb = lit_argb[c->idx];
                                v[k].oargb = lit_oargb[c->idx];
                            } else {
                                v[k].argb = base;
                            }
                        }
                        if (ok && cull) {
                            /* Front faces are counter-clockwise in view space (all the menu models are
                             * wound that way); the screen's y axis points down, so they come out with
                             * a negative area here. Back faces are not drawn. */
                            float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
                            ok = area < 0.0f;
                        }
                        if (ok) {
                            sink->triangle(sink->user, v, tex);
                        }
                    }
                }
            }
        }
    }

    if (ce) {
        if (cap_overflow) {
            cache_entry_n--; /* did not fit: drawn, but not kept */
            cache_pool_n = cap_first;
        } else {
            ce->count = cache_pool_n - cap_first;
            ce->valid = 1;
        }
        sink = real_sink;
    }
model_done:
    if (o->mesh_cols > 0 && o->mesh_mode == 5 && (s->parts & BSCENE_PART_MODEL)) {
        draw_grid_mesh(s, o, &obj_m, offs, sink);
    }
    if ((s->parts & BSCENE_PART_TEXT) && (o->flags & BVM_F_TEXT) && o->text_w > 0 && o->text_h > 0 && sink->text) {
        nj_vec3 anchor = {o->pos[0] + o->text_off[0], o->pos[1] + o->text_off[1], o->pos[2]};
        float sx, sy, iw;
        if (bscene_project(anchor, &sx, &sy, &iw)) {
            sink->text(sink->user, o, sx, sy, iw);
        }
    }
}

void
bscene_draw_objects(bscene* s, const bvm* vm, const bscene_sink* sink) {
    for (int i = 0; i < vm->count; i++) {
        bscene_draw_object(s, &vm->objs[vm->order[i]], sink);
    }
}

static void
draw_mesh(const dcbg_mesh* m, const dcbg_obj* o, const bscene_sink* sink) {
    dcbg_vertex out[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    dcbg_project(m, o, out);
    bscene_texref tex = {BSCENE_TEX_GBIX, m->tex, 0};
    if (m->tex == 0) {
        tex.kind = BSCENE_TEX_NONE;
    }
    int stride = m->rows + 1;
    for (int i = 0; i < m->cols; i++) {
        for (int j = 0; j < m->rows; j++) {
            const dcbg_vertex* q[4] = {&out[i * stride + j], &out[(i + 1) * stride + j], &out[i * stride + j + 1],
                                       &out[(i + 1) * stride + j + 1]};
            static const int order[2][3] = {{0, 1, 2}, {2, 1, 3}};
            for (int t = 0; t < 2; t++) {
                bscene_vtx v[3];
                for (int k = 0; k < 3; k++) {
                    const dcbg_vertex* src = q[order[t][k]];
                    v[k].x = src->sx;
                    v[k].y = src->sy;
                    v[k].invw = src->invw;
                    v[k].u = src->u;
                    v[k].v = src->v;
                    v[k].argb = src->argb;
                    v[k].oargb = 0;
                }
                sink->triangle(sink->user, v, tex);
            }
        }
    }
}

void
bscene_draw_background(const dcbg_state* bg, const bscene_sink* sink) {
    draw_mesh(&bg->swirl, &bg->swirl_obj, sink); /* object 0x120, drawn first (higher priority value) */
    draw_mesh(&bg->water, &bg->water_obj, sink); /* object 0x121 */
}

/* ---- translucent sorting (see bscene_sort_begin) ----------------------------------------------- */

typedef struct {
    bscene_vtx v[3];
    bscene_texref tex;
} sort_tri;

static sort_tri sort_buf[BSCENE_SORT_MAX];
typedef struct {
    float key;
    int idx;
} sort_key;

static sort_key sort_keys[BSCENE_SORT_MAX];
static int sort_n;
static const bscene_sink* sort_out;
static bscene_sink sort_sink;

static void
sort_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user;
    if (sort_n >= BSCENE_SORT_MAX) {
        sort_out->triangle(sort_out->user, v, tex);
        return;
    }
    sort_tri* t = &sort_buf[sort_n];
    memcpy(t->v, v, sizeof(t->v));
    t->tex = tex;
    sort_keys[sort_n].key = v[0].invw + v[1].invw + v[2].invw; /* larger 1/w = nearer */
    sort_keys[sort_n].idx = sort_n;
    sort_n++;
}

static void
sort_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    if (sort_out->text) {
        sort_out->text(sort_out->user, obj, x, y, invw);
    }
}

static int
sort_cmp(const void* a, const void* b) {
    const sort_key* ka = (const sort_key*)a;
    const sort_key* kb = (const sort_key*)b;
    if (ka->key != kb->key) {
        return ka->key < kb->key ? -1 : 1; /* far (small 1/w) first */
    }
    return ka->idx - kb->idx; /* same depth: keep the order they came in */
}

const bscene_sink*
bscene_sort_begin(const bscene_sink* out) {
    sort_out = out;
    sort_n = 0;
    sort_sink.user = NULL;
    sort_sink.triangle = sort_triangle;
    sort_sink.text = sort_text;
    return &sort_sink;
}

void
bscene_sort_end(void) {
    qsort(sort_keys, (size_t)sort_n, sizeof(sort_keys[0]), sort_cmp);
    for (int i = 0; i < sort_n; i++) {
        const sort_tri* t = &sort_buf[sort_keys[i].idx];
        sort_out->triangle(sort_out->user, t->v, t->tex);
    }
    sort_n = 0;
}
