/*
 * bios_scene: BIOS menu state to screen-space triangles, see bios_scene.h.
 */
#include "bios_scene.h"

#include <string.h>

#define NEAR_Z (-1.0f) /* anything closer than this to the camera plane is dropped */

void
bscene_init(bscene* s, const bios_rom* rom) {
    memset(s, 0, sizeof(*s));
    s->rom = rom;
}

void
bscene_free(bscene* s) {
    for (int i = 0; i < BSCENE_MODEL_CACHE; i++) {
        if (s->model_state[i] == 1) {
            nj_object_free(&s->models[i]);
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

static float
clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Base colour plus the object's constant material offsets (a, r, g, b in 0..1 units). */
static uint32_t
shade(uint32_t base, const float* offs) {
    float c[4] = {(float)(base >> 24) / 255.0f, (float)((base >> 16) & 255) / 255.0f, (float)((base >> 8) & 255) / 255.0f,
                  (float)(base & 255) / 255.0f};
    if (offs) {
        for (int i = 0; i < 4; i++) {
            c[i] = clamp01(c[i] + offs[i]);
        }
    }
    return ((uint32_t)(c[0] * 255.0f + 0.5f) << 24) | ((uint32_t)(c[1] * 255.0f + 0.5f) << 16)
           | ((uint32_t)(c[2] * 255.0f + 0.5f) << 8) | (uint32_t)(c[3] * 255.0f + 0.5f);
}

void
bscene_draw_object(bscene* s, const bvm_obj* o, const bscene_sink* sink) {
    if (!o->active || (o->flags & BVM_F_HIDE)) {
        return;
    }

    float scl[3] = {o->scale_tw[0].cur, o->scale_tw[1].cur, o->scale_tw[2].cur};
    nj_mat4 obj_m;
    nj_mat_object(&obj_m, o->pos, scl, o->rot);

    if (o->flags & BVM_F_MODEL) {
        const nj_object* obj = get_model(s, o->model);
        if (obj) {
            const nj_motion* mo = (o->flags & BVM_F_MOTION) ? get_motion(s, o->model, o->motion, obj->count) : NULL;
            nj_mat4* world = s->pose;
            nj_object_pose(obj, mo, o->motion_tw.cur, world);
            const float* offs = (o->flags & BVM_F_COLOUR) ? o->color : NULL;

            for (int n = 0; n < obj->count; n++) {
                const nj_mesh* mesh = obj->nodes[n].mesh;
                if (!mesh) {
                    continue;
                }
                nj_mat4 m;
                nj_mat_mul(&m, &obj_m, &world[n]);
                for (int p = 0; p < mesh->npolys; p++) {
                    const nj_poly* poly = &mesh->polys[p];
                    bscene_texref tex = {BSCENE_TEX_NONE, 0, 0};
                    if (poly->tex >= 0) {
                        tex.kind = BSCENE_TEX_TEXLIST;
                        tex.a = o->texlist;
                        tex.b = poly->tex;
                    }
                    for (int t = 0; t < poly->ntris; t++) {
                        bscene_vtx v[3];
                        int ok = 1;
                        for (int k = 0; k < 3 && ok; k++) {
                            const nj_corner* c = &poly->corners[t * 3 + k];
                            const nj_vertex* vx = &mesh->verts[c->idx];
                            nj_vec3 w = nj_mat_apply(&m, vx->pos);
                            ok = bscene_project(w, &v[k].x, &v[k].y, &v[k].invw);
                            v[k].u = c->u;
                            v[k].v = c->v;
                            uint32_t base = poly->has_diffuse ? poly->diffuse : (vx->has_col ? vx->col : 0xFFFFFFFFu);
                            v[k].argb = shade(base, offs);
                        }
                        if (ok) {
                            sink->triangle(sink->user, v, tex);
                        }
                    }
                }
            }
        }
    }

    if ((o->flags & BVM_F_TEXT) && o->text_w > 0 && o->text_h > 0 && sink->text) {
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
