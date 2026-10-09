/*
 * bios_scene: BIOS menu state to screen-space triangles, see bios_scene.h.
 */
#include "bios_scene.h"

#include <math.h>

#include <string.h>

#define NEAR_Z (-1.0f) /* anything closer than this to the camera plane is dropped */
#define MAX_MESH_VERTS 4096 /* nj_model never produces more */

/* The BIOS lights its models with one directional light that shines along the view
 * direction (0, 0, -1), plus an ambient term (nj_set_screen_params(0.5, 0.5, 0.5) in the
 * menu init). The colour is the material colour times (ambient + diffuse * N.L). The exact
 * light intensities live in data I could not pin down, so the split below is a tuned guess. */
#define LIGHT_AMBIENT 0.5f
#define LIGHT_DIFFUSE 0.5f
#define STRIP_IGNORE_LIGHT 0x01
#define STRIP_IGNORE_AMBIENT 0x04
#define STRIP_DOUBLE_SIDED 0x10
#define STRIP_ENV 0x40 /* environment mapping: u, v come from the vertex normal, not from the file */

/* Per-object scratch for projected vertices (the engine is single threaded). */
static float scratch_x[MAX_MESH_VERTS], scratch_y[MAX_MESH_VERTS], scratch_w[MAX_MESH_VERTS];
static int16_t scratch_l[MAX_MESH_VERTS]; /* light factor per vertex, 0..256 */
static int16_t scratch_n[MAX_MESH_VERTS]; /* N.L per vertex, 0..256 (256 when the vertex has no normal) */
static float scratch_eu[MAX_MESH_VERTS], scratch_ev[MAX_MESH_VERTS]; /* environment map u, v per vertex */
static uint8_t scratch_ok[MAX_MESH_VERTS];

void
bscene_init(bscene* s, const bios_rom* rom) {
    memset(s, 0, sizeof(*s));
    s->rom = rom;
    s->parts = BSCENE_PART_ALL;
    s->amb_k = 256;
    s->ambient_models = 1ull << 3; /* the alarm clock: its gold bells are lit from the front only and went olive */
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

/* Integer colour maths on purpose: on the SH-4 a float to unsigned conversion is a slow library
 * call, and this runs for every corner of every triangle. */
static int
clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* Constant material offsets (a, r, g, b in 0..1 units) as 0..255 integers. */
static void
offsets_to_int(const float* offs, int out[4]) {
    for (int i = 0; i < 4; i++) {
        out[i] = (int)(offs[i] * 255.0f);
    }
}

/* Base colour plus the object's constant material offsets. */
static uint32_t
shade(uint32_t base, const int* off) {
    if (!off) {
        return base;
    }
    int a = clamp255((int)(base >> 24) + off[0]);
    int r = clamp255((int)((base >> 16) & 255) + off[1]);
    int g = clamp255((int)((base >> 8) & 255) + off[2]);
    int b = clamp255((int)(base & 255) + off[3]);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Multiply the colour channels (not the alpha) by f / 256. */
static uint32_t
scale_rgb(uint32_t argb, int f) {
    if (f >= 256) {
        return argb;
    }
    uint32_t r = (((argb >> 16) & 255) * (uint32_t)f) >> 8;
    uint32_t g = (((argb >> 8) & 255) * (uint32_t)f) >> 8;
    uint32_t b = ((argb & 255) * (uint32_t)f) >> 8;
    return (argb & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

/* Alpha of two layers of the same translucent surface on top of each other. */
static uint32_t
double_alpha(uint32_t argb) {
    uint32_t a = argb >> 24;
    a = a + (((255 - a) * a) / 255);
    return (argb & 0x00FFFFFFu) | (a << 24);
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
#define ROUND_SEGMENTS 32
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
    bscene_vtx pts[ROUND_SEGMENTS + 1];
    int ok[ROUND_SEGMENTS + 1];
    for (int i = 0; i <= ROUND_SEGMENTS; i++) {
        float a = 6.2831853f * (float)(i % ROUND_SEGMENTS) / ROUND_SEGMENTS;
        float cs = cosf(a), sn = sinf(a);
        nj_vec3 w = nj_mat_apply(m, (nj_vec3){cx + rx * cs, cy + ry * sn, z});
        ok[i] = bscene_project(w, &pts[i].x, &pts[i].y, &pts[i].invw);
        pts[i].u = 0.5f + 0.5f * cs;
        pts[i].v = 0.5f - 0.5f * sn;
        pts[i].argb = argb;
    }
    bscene_vtx c;
    nj_vec3 wc = nj_mat_apply(m, (nj_vec3){cx, cy, z});
    int cok = bscene_project(wc, &c.x, &c.y, &c.invw);
    c.u = c.v = 0.5f;
    c.argb = argb;
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

void
bscene_draw_object(bscene* s, const bvm_obj* o, const bscene_sink* sink) {
    if (!o->active || (o->flags & BVM_F_HIDE)) {
        return;
    }

    float scl[3] = {o->scale_tw[0].cur, o->scale_tw[1].cur, o->scale_tw[2].cur};
    nj_mat4 obj_m;
    nj_mat_object(&obj_m, o->pos, scl, o->rot);

    if ((o->flags & BVM_F_MODEL) && (s->parts & BSCENE_PART_MODEL)) {
        const nj_object* obj = get_model(s, o->model);
        if (obj) {
            const nj_motion* mo = (o->flags & BVM_F_MOTION) ? get_motion(s, o->model, o->motion, obj->count) : NULL;
            nj_mat4* world = s->pose;
            nj_object_pose(obj, mo, o->motion_tw.cur, world);
            int off_i[4];
            const int* offs = NULL;
            if (o->flags & BVM_F_COLOUR) {
                offsets_to_int(o->color, off_i);
                offs = off_i;
            }

            for (int n = 0; n < obj->count; n++) {
                const nj_mesh* mesh = obj->nodes[n].mesh;
                if (!mesh) {
                    continue;
                }
                nj_mat4 m;
                nj_mat_mul(&m, &obj_m, &world[n]);

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
                        /* only the z of the rotated normal matters for a light along the view axis */
                        int light = 256;
                        if (vx->has_nrm) {
                            float nz = m.m[2][0] * vx->nrm.x + m.m[2][1] * vx->nrm.y + m.m[2][2] * vx->nrm.z;
                            if (s->light_x != 0.0f || s->light_y != 0.0f) { /* a light that is not along the view axis */
                                float nx = m.m[0][0] * vx->nrm.x + m.m[0][1] * vx->nrm.y + m.m[0][2] * vx->nrm.z;
                                float ny = m.m[1][0] * vx->nrm.x + m.m[1][1] * vx->nrm.y + m.m[1][2] * vx->nrm.z;
                                nz = nz * s->light_z + ny * s->light_y + nx * s->light_x;
                            }
                            nz = nz > 0.0f ? (nz > 1.0f ? 1.0f : nz) : 0.0f;
                            light = (int)((LIGHT_AMBIENT + LIGHT_DIFFUSE * nz) * 256.0f);
                            light = light > 256 ? 256 : light;
                        }
                        if (s->fullbright) {
                            light = 256;
                        }
                        scratch_l[i] = (int16_t)light;
                        {
                            float nl = 1.0f;
                            if (vx->has_nrm) {
                                nl = m.m[2][0] * vx->nrm.x + m.m[2][1] * vx->nrm.y + m.m[2][2] * vx->nrm.z;
                                nl = nl > 0.0f ? (nl > 1.0f ? 1.0f : nl) : 0.0f;
                            }
                            scratch_n[i] = (int16_t)(s->fullbright ? 256 : (int)(nl * 256.0f));
                        }
                        if (vx->has_nrm) {
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
                    uint32_t poly_argb = poly->has_diffuse ? shade(poly->diffuse, offs) : 0;
                    if (s->panel_on && p == 0) {
                        poly_argb = s->panel_accent; /* the rim */
                    }
                    for (int k = 0; k < s->ovr_n; k++) {
                        if (s->ovr[k].model == o->model && s->ovr[k].node == n && s->ovr[k].poly == p) {
                            poly_argb = s->ovr[k].argb;
                        }
                    }
                    if (poly->has_diffuse && s->double_alpha) {
                        poly_argb = double_alpha(poly_argb);
                    }
                    if (poly->tex == 0 && o->texlist >= BSCENE_ROUND_FACE_TEXLIST && poly->has_uv && poly->ntris == 2) {
                        draw_round_face(mesh, poly, &m, poly_argb, tex, sink);
                        continue;
                    }
                    int lit = !(poly->strip_flags & STRIP_IGNORE_LIGHT);
                    const int use_ambient = (s->ambient_models >> (o->model & 63) & 1) && o->model < 64 && poly->has_diffuse && poly->has_ambient && !(poly->strip_flags & STRIP_IGNORE_AMBIENT);
                    const uint32_t amb = use_ambient ? shade(poly->ambient, offs) : 0;
                    const int amb_k = s->amb_k; /* 0..256 */
                    int cull = !(poly->strip_flags & STRIP_DOUBLE_SIDED);
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
                            uint32_t base;
                            if (poly->has_diffuse) {
                                base = poly_argb;
                            } else {
                                const nj_vertex* vx = &mesh->verts[c->idx];
                                base = shade(vx->has_col ? vx->col : 0xFFFFFFFFu, offs);
                                if (s->double_alpha) {
                                    base = double_alpha(base);
                                }
                            }
                            if (lit && use_ambient && mesh->verts[c->idx].has_nrm) {
                                /* the lit colour, but never darker than the material's own ambient colour: the faces of
                                 * the clock's gold bells that turn away from the light stay gold instead of olive */
                                uint32_t d = scale_rgb(base, scratch_l[c->idx]);
                                int r = (int)((d >> 16) & 255), g = (int)((d >> 8) & 255), bl = (int)(d & 255);
                                int ar = (int)((amb >> 16) & 255) * amb_k >> 8, ag = (int)((amb >> 8) & 255) * amb_k >> 8,
                                    ab = (int)(amb & 255) * amb_k >> 8;
                                r = r > ar ? r : ar;
                                g = g > ag ? g : ag;
                                bl = bl > ab ? bl : ab;
                                v[k].argb = (base & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
                            } else {
                                v[k].argb = lit ? scale_rgb(base, scratch_l[c->idx]) : base;
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
