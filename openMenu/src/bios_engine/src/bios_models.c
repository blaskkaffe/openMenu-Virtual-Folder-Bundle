/*
 * bios_models: procedural models in the style of the BIOS menu, see bios_models.h.
 * Flat shaded solids made of boxes, cylinders and a low poly sphere; the winding of every face is
 * fixed up against an outward direction, so the primitives only have to get the geometry right.
 */
#include "bios_models.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VERTS 4096
#define MAX_POLYS 12
#define MAX_TRIS 1600
#define PI_F 3.14159265f

#define STRIP_IGNORE_LIGHT 0x01
#define STRIP_DOUBLE_SIDED 0x10

typedef struct {
    int tex;
    uint32_t diffuse;
    uint8_t flags;
    int ntris;
    nj_corner* corners;
} bpoly;

typedef struct {
    nj_vertex* verts;
    int nverts;
    bpoly polys[MAX_POLYS];
    int npolys;
    int err;
} builder;

typedef nj_vec3 v3;

static v3
v3m(float x, float y, float z) {
    v3 r = {x, y, z};
    return r;
}
static v3
sub(v3 a, v3 b) {
    return v3m(a.x - b.x, a.y - b.y, a.z - b.z);
}
static v3
add(v3 a, v3 b) {
    return v3m(a.x + b.x, a.y + b.y, a.z + b.z);
}
static v3
mul(v3 a, float f) {
    return v3m(a.x * f, a.y * f, a.z * f);
}
static float
dot(v3 a, v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static v3
cross(v3 a, v3 b) {
    return v3m(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static v3
norm(v3 a) {
    float l = sqrtf(dot(a, a));
    return l > 1e-9f ? mul(a, 1.0f / l) : v3m(0, 0, 1);
}

static int
poly_new(builder* b, int tex, uint32_t diffuse, uint8_t flags) {
    if (b->npolys >= MAX_POLYS) {
        b->err = 1;
        return 0;
    }
    bpoly* p = &b->polys[b->npolys];
    p->tex = tex;
    p->diffuse = diffuse;
    p->flags = flags;
    p->ntris = 0;
    p->corners = (nj_corner*)calloc(3 * MAX_TRIS, sizeof(nj_corner));
    if (!p->corners) {
        b->err = 1;
    }
    return b->npolys++;
}

/* One triangle. `out` is a direction the face should look toward; nrm is NULL for a flat face. */
static void
tri(builder* b, int poly, const v3 p[3], const v3* nrm, const float uv[3][2], v3 out) {
    bpoly* bp = &b->polys[poly];
    if (b->err || !bp->corners || bp->ntris >= MAX_TRIS || b->nverts + 3 > MAX_VERTS) {
        b->err = 1;
        return;
    }
    int order[3] = {0, 1, 2};
    v3 n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
    if (dot(n, n) < 1e-12f) {
        return; /* degenerate, e.g. at a pole */
    }
    if (dot(n, out) < 0.0f) {
        order[1] = 2;
        order[2] = 1;
        n = mul(n, -1.0f);
    }
    n = norm(n);
    for (int k = 0; k < 3; k++) {
        int s = order[k];
        nj_vertex* v = &b->verts[b->nverts];
        memset(v, 0, sizeof(*v));
        v->pos = p[s];
        v->nrm = nrm ? nrm[s] : n;
        v->valid = v->has_nrm = 1;
        nj_corner* c = &bp->corners[bp->ntris * 3 + k];
        c->idx = (uint16_t)b->nverts++;
        c->u = uv ? uv[s][0] : 0.0f;
        c->v = uv ? uv[s][1] : 0.0f;
    }
    bp->ntris++;
}

static void
quad(builder* b, int poly, v3 a, v3 bb, v3 c, v3 d, const float uv[4][2], v3 out) {
    v3 t0[3] = {a, bb, c}, t1[3] = {a, c, d};
    float u0[3][2], u1[3][2];
    if (uv) {
        memcpy(u0[0], uv[0], 8);
        memcpy(u0[1], uv[1], 8);
        memcpy(u0[2], uv[2], 8);
        memcpy(u1[0], uv[0], 8);
        memcpy(u1[1], uv[2], 8);
        memcpy(u1[2], uv[3], 8);
    }
    tri(b, poly, t0, NULL, uv ? u0 : NULL, out);
    tri(b, poly, t1, NULL, uv ? u1 : NULL, out);
}

/* Axis aligned box between two corners; faces listed in `mask` get polygon `poly` (bit 0..5 = +x -x +y -y +z -z). */
static void
box(builder* b, int poly, float x0, float y0, float z0, float x1, float y1, float z1) {
    v3 c000 = v3m(x0, y0, z0), c100 = v3m(x1, y0, z0), c110 = v3m(x1, y1, z0), c010 = v3m(x0, y1, z0);
    v3 c001 = v3m(x0, y0, z1), c101 = v3m(x1, y0, z1), c111 = v3m(x1, y1, z1), c011 = v3m(x0, y1, z1);
    quad(b, poly, c100, c110, c111, c101, NULL, v3m(1, 0, 0));
    quad(b, poly, c000, c001, c011, c010, NULL, v3m(-1, 0, 0));
    quad(b, poly, c010, c011, c111, c110, NULL, v3m(0, 1, 0));
    quad(b, poly, c000, c100, c101, c001, NULL, v3m(0, -1, 0));
    quad(b, poly, c001, c101, c111, c011, NULL, v3m(0, 0, 1));
    quad(b, poly, c000, c010, c110, c100, NULL, v3m(0, 0, -1));
}

/* Rectangular frustum standing on y: bottom half sizes (w0, d0) at y0, top (w1, d1) at y1, centred on (cx, cz). */
static void
frustum(builder* b, int poly, float cx, float cz, float y0, float y1, float w0, float d0, float w1, float d1, int bottom) {
    v3 bt[4] = {v3m(cx - w0, y0, cz - d0), v3m(cx + w0, y0, cz - d0), v3m(cx + w0, y0, cz + d0), v3m(cx - w0, y0, cz + d0)};
    v3 tp[4] = {v3m(cx - w1, y1, cz - d1), v3m(cx + w1, y1, cz - d1), v3m(cx + w1, y1, cz + d1), v3m(cx - w1, y1, cz + d1)};
    v3 mid = v3m(cx, (y0 + y1) / 2, cz);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        v3 fc = mul(add(add(bt[i], bt[j]), add(tp[i], tp[j])), 0.25f);
        quad(b, poly, bt[i], bt[j], tp[j], tp[i], NULL, sub(fc, mid));
    }
    quad(b, poly, tp[0], tp[1], tp[2], tp[3], NULL, v3m(0, 1, 0));
    if (bottom) {
        quad(b, poly, bt[0], bt[1], bt[2], bt[3], NULL, v3m(0, -1, 0));
    }
}

#define CAP_BOTTOM 1
#define CAP_TOP 2

/* Cylinder (or cone frustum) from base centre `c` along `axis`; smooth radial normals on the side. */
static void
cylinder(builder* b, int side_poly, int cap_poly, int caps, int seg, v3 c, v3 axis, float r0, float r1, float len) {
    v3 a = norm(axis);
    v3 ref = fabsf(a.y) < 0.9f ? v3m(0, 1, 0) : v3m(1, 0, 0);
    v3 u = norm(cross(a, ref)), v = cross(a, u);
    v3 top = add(c, mul(a, len));
    for (int i = 0; i < seg; i++) {
        float t0 = 2 * PI_F * i / seg, t1 = 2 * PI_F * (i + 1) / seg;
        v3 d0 = add(mul(u, cosf(t0)), mul(v, sinf(t0))), d1 = add(mul(u, cosf(t1)), mul(v, sinf(t1)));
        v3 b0 = add(c, mul(d0, r0)), b1 = add(c, mul(d1, r0));
        v3 t0p = add(top, mul(d0, r1)), t1p = add(top, mul(d1, r1));
        v3 q1[3] = {b0, b1, t1p}, q2[3] = {b0, t1p, t0p};
        v3 n1[3] = {d0, d1, d1}, n2[3] = {d0, d1, d0};
        v3 out = add(d0, d1);
        tri(b, side_poly, q1, n1, NULL, out);
        tri(b, side_poly, q2, n2, NULL, out);
        if ((caps & CAP_TOP) && r1 > 0.0f) {
            v3 f[3] = {top, t0p, t1p};
            tri(b, cap_poly, f, NULL, NULL, a);
        }
        if ((caps & CAP_BOTTOM) && r0 > 0.0f) {
            v3 f[3] = {c, b0, b1};
            tri(b, cap_poly, f, NULL, NULL, mul(a, -1.0f));
        }
    }
}

/* Solid of revolution about the y axis through (cx, cy, cz): rings of (radius, height above cy), bottom to
 * top, z squeezed by zs. Normals point away from the inner point (cx, cy + inner_y, cz), which makes domes
 * look round. A cap closes the top (and the bottom if bottom_cap). */
static void
lathe(builder* b, int poly, int seg, const float (*ring)[2], int n, v3 c, float zs, float inner_y, int top_cap, int bottom_cap) {
    v3 in = v3m(c.x, c.y + inner_y, c.z);
    for (int i = 0; i + 1 < n; i++) {
        for (int k = 0; k < seg; k++) {
            float t0 = 2 * PI_F * k / seg, t1 = 2 * PI_F * (k + 1) / seg;
            v3 p[4] = {v3m(c.x + ring[i][0] * cosf(t0), c.y + ring[i][1], c.z + ring[i][0] * sinf(t0) * zs),
                       v3m(c.x + ring[i][0] * cosf(t1), c.y + ring[i][1], c.z + ring[i][0] * sinf(t1) * zs),
                       v3m(c.x + ring[i + 1][0] * cosf(t1), c.y + ring[i + 1][1], c.z + ring[i + 1][0] * sinf(t1) * zs),
                       v3m(c.x + ring[i + 1][0] * cosf(t0), c.y + ring[i + 1][1], c.z + ring[i + 1][0] * sinf(t0) * zs)};
            v3 nr[4];
            for (int q = 0; q < 4; q++) {
                nr[q] = norm(sub(p[q], in));
            }
            v3 out = add(nr[0], nr[2]);
            v3 t1v[3] = {p[0], p[1], p[2]}, t2v[3] = {p[0], p[2], p[3]};
            v3 n1[3] = {nr[0], nr[1], nr[2]}, n2[3] = {nr[0], nr[2], nr[3]};
            tri(b, poly, t1v, n1, NULL, out);
            tri(b, poly, t2v, n2, NULL, out);
        }
    }
    for (int cap = 0; cap < 2; cap++) {
        if ((cap == 0 && !top_cap) || (cap == 1 && !bottom_cap)) {
            continue;
        }
        int r = cap == 0 ? n - 1 : 0;
        float y = c.y + ring[r][1];
        for (int k = 0; k < seg; k++) {
            float t0 = 2 * PI_F * k / seg, t1 = 2 * PI_F * (k + 1) / seg;
            v3 f[3] = {v3m(c.x, y, c.z), v3m(c.x + ring[r][0] * cosf(t0), y, c.z + ring[r][0] * sinf(t0) * zs),
                       v3m(c.x + ring[r][0] * cosf(t1), y, c.z + ring[r][0] * sinf(t1) * zs)};
            tri(b, poly, f, NULL, NULL, v3m(0, cap == 0 ? 1.0f : -1.0f, 0));
        }
    }
}

/* Bent bar with a rectangular section (hy x hz half sizes) through `n` centres in the x-y plane. */
static void
bar(builder* b, int poly, const v3* c, int n, float hy, float hz) {
    v3 ring[8][4];
    for (int i = 0; i < n; i++) {
        v3 t = norm(sub(c[i + (i + 1 < n)], c[i - (i > 0)]));
        v3 nn = v3m(-t.y, t.x, 0.0f);
        ring[i][0] = add(add(c[i], mul(nn, hy)), v3m(0, 0, hz));
        ring[i][1] = add(add(c[i], mul(nn, -hy)), v3m(0, 0, hz));
        ring[i][2] = add(add(c[i], mul(nn, -hy)), v3m(0, 0, -hz));
        ring[i][3] = add(add(c[i], mul(nn, hy)), v3m(0, 0, -hz));
    }
    for (int i = 0; i + 1 < n; i++) {
        v3 mid = mul(add(c[i], c[i + 1]), 0.5f);
        for (int k = 0; k < 4; k++) {
            int j = (k + 1) & 3;
            v3 fc = mul(add(add(ring[i][k], ring[i][j]), add(ring[i + 1][k], ring[i + 1][j])), 0.25f);
            quad(b, poly, ring[i][k], ring[i][j], ring[i + 1][j], ring[i + 1][k], NULL, sub(fc, mid));
        }
    }
}

/* ---- finishing ----------------------------------------------------------------------------- */

static void
builder_free(builder* b) {
    for (int i = 0; i < b->npolys; i++) {
        free(b->polys[i].corners);
    }
    free(b->verts);
}

static int
builder_finish(builder* b, nj_object* out) {
    int ok = !b->err && b->nverts > 0;
    nj_node* node = ok ? (nj_node*)calloc(1, sizeof(nj_node)) : NULL;
    nj_mesh* mesh = node ? (nj_mesh*)calloc(1, sizeof(nj_mesh)) : NULL;
    if (mesh) {
        mesh->verts = (nj_vertex*)calloc((size_t)b->nverts, sizeof(nj_vertex));
        mesh->polys = (nj_poly*)calloc((size_t)b->npolys, sizeof(nj_poly));
    }
    if (!mesh || !mesh->verts || !mesh->polys) {
        if (mesh) {
            free(mesh->verts);
            free(mesh->polys);
        }
        free(mesh);
        free(node);
        builder_free(b);
        return -1;
    }
    memcpy(mesh->verts, b->verts, (size_t)b->nverts * sizeof(nj_vertex));
    mesh->nverts = b->nverts;
    for (int i = 0; i < b->npolys; i++) {
        const bpoly* s = &b->polys[i];
        nj_poly* d = &mesh->polys[mesh->npolys];
        if (s->ntris == 0) {
            continue;
        }
        d->tex = s->tex;
        d->diffuse = s->diffuse;
        d->has_diffuse = 1;
        d->has_uv = s->tex >= 0;
        d->strip_flags = s->flags;
        d->ntris = s->ntris;
        d->corners = (nj_corner*)malloc((size_t)s->ntris * 3 * sizeof(nj_corner));
        if (!d->corners) {
            continue; /* leaves the poly empty; drop it */
        }
        memcpy(d->corners, s->corners, (size_t)s->ntris * 3 * sizeof(nj_corner));
        mesh->npolys++;
    }
    node->scl.x = node->scl.y = node->scl.z = 1.0f;
    node->parent = -1;
    node->mesh = mesh;
    out->count = 1;
    out->nodes = node;
    builder_free(b);
    return 0;
}

static int
builder_start(builder* b) {
    memset(b, 0, sizeof(*b));
    b->verts = (nj_vertex*)calloc(MAX_VERTS, sizeof(nj_vertex));
    return b->verts ? 0 : -1;
}

/* ---- the models ---------------------------------------------------------------------------- */

static void
build_phone(builder* b) {
    int body = poly_new(b, -1, BMODEL_NOTE_COLOR, 0);
    int light = poly_new(b, -1, 0xFFB299FFu, 0);
    int white = poly_new(b, -1, 0xFFFFFFFFu, 0);
    int hub = poly_new(b, -1, 0xFFD2D2E6u, 0);

    /* square plinth, then the rounded, domed body of an old bakelite set */
    frustum(b, body, 0.0f, 0.0f, 0.0f, 0.8f, 4.3f, 3.5f, 4.0f, 3.2f, 0);
    static const float shell[4][2] = {{3.7f, 0.0f}, {3.6f, 0.9f}, {2.9f, 1.8f}, {1.6f, 2.4f}};
    lathe(b, body, 10, shell, 4, v3m(0.0f, 0.8f, 0.0f), 0.85f, 0.0f, 1, 0);
    /* the dial: one white disc with a small hub, on the sloped front */
    v3 tilt = norm(v3m(0.0f, cosf(0.85f), sinf(0.85f)));
    v3 centre = v3m(0.0f, 2.0f, 1.9f);
    cylinder(b, white, white, CAP_TOP, 12, centre, tilt, 1.75f, 1.75f, 0.3f);
    cylinder(b, hub, hub, CAP_TOP, 6, add(centre, mul(tilt, 0.3f)), tilt, 0.6f, 0.5f, 0.2f);
    /* handset: two round cups joined by an arched grip */
    static const float cup[3][2] = {{1.15f, 0.0f}, {1.3f, 0.8f}, {0.7f, 1.7f}};
    lathe(b, light, 7, cup, 3, v3m(-2.9f, 2.5f, 0.0f), 1.0f, -0.6f, 1, 0);
    lathe(b, light, 7, cup, 3, v3m(2.9f, 2.5f, 0.0f), 1.0f, -0.6f, 1, 0);
    const v3 grip[4] = {{-2.9f, 3.9f, 0.0f}, {-1.0f, 4.75f, 0.0f}, {1.0f, 4.75f, 0.0f}, {2.9f, 3.9f, 0.0f}};
    bar(b, light, grip, 4, 0.42f, 0.42f);
}

#define GLOBE_R 4.6f
#define GLOBE_FRONT_LON (-75.0f) /* the Americas face the viewer */
#define GLOBE_LEAN 0.35f           /* the north pole leans toward the viewer (radians) */
#ifndef GLOBE_SUBDIV
#define GLOBE_SUBDIV 2 /* 20 * 4^2 = 320 triangles; 3 gives 1280 */
#endif
#define GLOBE_MAX_FACES (20 << (2 * GLOBE_SUBDIV))

static v3
globe_orient(v3 p) {
    /* turn the earth so GLOBE_FRONT_LON faces +z, then lean the north pole toward the viewer */
    float a = -GLOBE_FRONT_LON * (PI_F / 180.0f);
    v3 q = v3m(p.x * cosf(a) + p.z * sinf(a), p.y, -p.x * sinf(a) + p.z * cosf(a));
    return v3m(q.x, q.y * cosf(GLOBE_LEAN) - q.z * sinf(GLOBE_LEAN), q.y * sinf(GLOBE_LEAN) + q.z * cosf(GLOBE_LEAN));
}

/* Continents and big islands as closed (lon, lat) loops in degrees. */
#define LOOP_END 999.0f
static const float globe_coast[][2] = {
    /* North America */
    {-168, 66}, {-162, 70}, {-156, 71.5}, {-141, 69.5}, {-128, 70}, {-115, 68}, {-95, 72}, {-85, 69}, {-82, 63}, {-93, 59}, {-94, 57}, {-80, 52}, {-79, 57}, {-77, 62}, {-70, 60}, {-62, 58}, {-56, 52}, {-60, 47}, {-66, 45}, {-70, 42}, {-74, 40}, {-76, 35}, {-81, 31}, {-80, 26}, {-82, 26}, {-84, 30}, {-90, 29.5}, {-94, 29}, {-97, 26}, {-97.5, 22}, {-95, 19}, {-91, 19}, {-90, 21}, {-87, 21}, {-88, 16}, {-84, 15}, {-83, 10}, {-79, 8.5}, {-83, 8}, {-86, 11}, {-92, 14.5}, {-96, 16}, {-105, 20}, {-109, 26}, {-114, 31}, {-117, 33}, {-121, 35}, {-124, 40}, {-124, 46}, {-125, 49}, {-130, 54}, {-135, 58}, {-140, 60}, {-148, 60}, {-152, 58}, {-158, 56}, {-163, 55}, {-158, 58}, {-162, 61}, {-165, 63}, {LOOP_END, 0},
    /* Greenland */
    {-73, 78}, {-60, 82}, {-30, 83}, {-20, 80}, {-18, 76}, {-22, 70}, {-32, 68}, {-43, 60}, {-48, 61}, {-53, 67}, {-56, 70}, {-67, 76}, {LOOP_END, 0},
    /* South America */
    {-77.5, 8.5}, {-72, 12}, {-65, 10.5}, {-60, 8}, {-52, 5}, {-50, 0}, {-44, -2}, {-35, -5}, {-35, -9}, {-39, -13}, {-39, -18}, {-41, -22}, {-48, -26}, {-53, -34}, {-58, -35}, {-57, -38}, {-62, -39}, {-65, -45}, {-68, -50}, {-69, -54}, {-72, -53}, {-75, -47}, {-73, -40}, {-71, -30}, {-70, -18}, {-76, -14}, {-81, -6}, {-80, -2}, {-78, 2}, {LOOP_END, 0},
    /* Africa */
    {-17, 21}, {-16, 28}, {-9, 32}, {-6, 36}, {10, 37}, {11, 33}, {20, 31}, {32, 31}, {34, 28}, {38, 20}, {43, 12}, {51, 11.5}, {48, 5}, {40, -3}, {40, -11}, {35, -20}, {33, -26}, {27, -34}, {20, -35}, {18, -30}, {12, -17}, {13, -8}, {9, -1}, {9, 4}, {5, 6}, {-2, 5}, {-8, 4.5}, {-13, 8}, {-17, 13}, {LOOP_END, 0},
    /* Eurasia */
    {-9, 37}, {-9, 43}, {-2, 44}, {-5, 48}, {2, 51}, {5, 53}, {8, 54}, {8, 57}, {10, 57}, {12, 54}, {21, 55}, {24, 59}, {30, 60}, {23, 60}, {22, 65}, {17, 61}, {19, 59}, {16, 56}, {12, 56}, {11, 59}, {5, 59}, {5, 62}, {14, 67}, {25, 71}, {33, 70}, {41, 67}, {40, 64}, {45, 68}, {60, 69}, {70, 73}, {80, 73}, {100, 77}, {113, 74}, {130, 71}, {140, 72}, {160, 70}, {170, 70}, {180, 68}, {180, 65}, {178, 63}, {163, 60}, {156, 51}, {155, 58}, {142, 59}, {137, 54}, {141, 52}, {140, 48}, {135, 43}, {130, 42}, {129, 35}, {126, 35}, {125, 40}, {121, 40}, {122, 37}, {119, 35}, {122, 31}, {121, 28}, {115, 23}, {110, 21}, {108, 17}, {106, 10}, {105, 9}, {101, 13}, {100, 7}, {103, 1.5}, {100, 3}, {98, 10}, {98, 16}, {94, 16}, {92, 22}, {87, 21.5}, {80, 15}, {77, 8}, {73, 17}, {72, 21}, {67, 24.5}, {61, 25}, {57, 26}, {56, 27}, {51, 30}, {49, 30}, {51, 25}, {56, 24}, {59, 22}, {52, 16}, {44, 12.5}, {43, 15}, {39, 21}, {35, 28}, {35, 31}, {36, 36.5}, {27, 37}, {26, 40.5}, {23, 40}, {24, 37}, {21, 37}, {19, 42}, {13, 45}, {12, 44}, {18, 40}, {16, 38}, {12, 41}, {9, 44}, {3, 43}, {0, 39}, {-2, 37}, {-6, 36}, {LOOP_END, 0},
    /* Australia */
    {114, -22}, {122, -18}, {130, -12}, {137, -12}, {136, -16}, {141, -17}, {142, -11}, {146, -19}, {153, -26}, {151, -33}, {147, -38}, {141, -38}, {135, -35}, {131, -31}, {124, -33}, {115, -34}, {114, -28}, {LOOP_END, 0},
    /* UK */
    {-5, 50}, {1, 51}, {2, 53}, {-3, 56}, {-5, 58.5}, {-6, 56}, {-3, 54}, {-5, 52}, {LOOP_END, 0},
    /* Japan */
    {130, 31}, {136, 34}, {140, 36}, {142, 40}, {142, 44}, {140, 42}, {137, 37}, {131, 34}, {LOOP_END, 0},
    /* Madagascar */
    {44, -25}, {47, -25}, {50, -15}, {49, -12}, {44, -17}, {LOOP_END, 0},
    /* Borneo */
    {109, 1}, {117, 7}, {119, 5}, {116, -4}, {110, -3}, {LOOP_END, 0},
    /* Sumatra */
    {95, 5}, {106, -6}, {104, -6}, {98, 0}, {LOOP_END, 0},
    /* New Guinea */
    {131, -1}, {141, -3}, {150, -10}, {142, -9}, {138, -8}, {LOOP_END, 0},
    /* New Zealand */
    {172, -34}, {178, -38}, {175, -41}, {167, -46}, {172, -41}, {LOOP_END, 0},
    {LOOP_END, LOOP_END},
};

/* Land test on the coarse continent loops (lon, lat in degrees), plus ice at the poles. */
static int
globe_surface(float lon, float lat) {
    if (lat > 78.0f || lat < -70.0f) {
        return 2; /* ice */
    }
    int start = 0, inside = 0;
    for (int i = 0;; i++) {
        if (globe_coast[i][0] == LOOP_END) {
            if (globe_coast[i][1] == LOOP_END) {
                break;
            }
            int n = i - start, in = 0;
            for (int k = 0, j = n - 1; k < n; j = k++) {
                const float* pk = globe_coast[start + k];
                const float* pj = globe_coast[start + j];
                if (((pk[1] > lat) != (pj[1] > lat)) && (lon < (pj[0] - pk[0]) * (lat - pk[1]) / (pj[1] - pk[1]) + pk[0])) {
                    in = !in;
                }
            }
            inside |= in;
            start = i + 1;
        }
    }
    return inside ? 1 : 0;
}

static void
build_globe(builder* b) {
    /* BIOS palette: the green of the menu icons, a deep blue, a light ice colour */
    int sea = poly_new(b, -1, 0xFF2A5FD8u, 0);
    int land = poly_new(b, -1, 0xFF1CB257u, 0);
    int ice = poly_new(b, -1, 0xFFEAF2FFu, 0);

    /* icosahedron, subdivided twice: a faceted ball */
    static v3 vert[12 + 2 * GLOBE_MAX_FACES]; /* corners of the ball, with the midpoints added by the subdivision */
    static int face[GLOBE_MAX_FACES][3];
    int nv = 12, nf = 20;
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    {
        const float a = 1.0f, c = t;
        const float pts[12][3] = {{-a, c, 0}, {a, c, 0}, {-a, -c, 0}, {a, -c, 0}, {0, -a, c}, {0, a, c},
                                  {0, -a, -c}, {0, a, -c}, {c, 0, -a}, {c, 0, a}, {-c, 0, -a}, {-c, 0, a}};
        for (int i = 0; i < 12; i++) {
            vert[i] = norm(v3m(pts[i][0], pts[i][1], pts[i][2]));
        }
    }
    static const int f0[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                  {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    memcpy(face, f0, sizeof(f0));
    for (int level = 0; level < GLOBE_SUBDIV; level++) {
        static int nface[GLOBE_MAX_FACES][3];
        int nn = 0;
        for (int f = 0; f < nf; f++) {
            int m[3];
            for (int k = 0; k < 3; k++) {
                v3 mid = norm(add(vert[face[f][k]], vert[face[f][(k + 1) % 3]]));
                int found = -1;
                for (int q = 12; q < nv; q++) {
                    if (fabsf(vert[q].x - mid.x) + fabsf(vert[q].y - mid.y) + fabsf(vert[q].z - mid.z) < 1e-5f) {
                        found = q;
                        break;
                    }
                }
                if (found < 0) {
                    vert[nv] = mid;
                    found = nv++;
                }
                m[k] = found;
            }
            int a = face[f][0], bb = face[f][1], c = face[f][2];
            int quads[4][3] = {{a, m[0], m[2]}, {m[0], bb, m[1]}, {m[2], m[1], c}, {m[0], m[1], m[2]}};
            for (int k = 0; k < 4; k++) {
                memcpy(nface[nn++], quads[k], sizeof(int) * 3);
            }
        }
        nf = nn;
        memcpy(face, nface, sizeof(int) * (size_t)nf * 3);
    }
    for (int f = 0; f < nf; f++) {
        v3 corner[3], c = v3m(0, 0, 0);
        for (int k = 0; k < 3; k++) {
            corner[k] = vert[face[f][k]];
            c = add(c, corner[k]);
        }
        c = norm(c);
        float lat = asinf(c.y) * (180.0f / PI_F), lon = atan2f(c.x, c.z) * (180.0f / PI_F); /* unit sphere, lon 0 toward +z */
        /* land when at least two of four sample points (the middle and three points toward the corners) are land */
        int kind = globe_surface(lon, lat), votes = kind == 1;
        if (kind != 2) {
            for (int k = 0; k < 3; k++) {
                v3 q = norm(add(mul(c, 0.5f), mul(corner[k], 0.5f)));
                float qlat = asinf(q.y) * (180.0f / PI_F), qlon = atan2f(q.x, q.z) * (180.0f / PI_F);
                votes += globe_surface(qlon, qlat) == 1;
            }
            kind = votes >= 2;
        }
        v3 p[3];
        for (int k = 0; k < 3; k++) {
            p[k] = add(v3m(0.0f, 5.0f, 0.0f), mul(globe_orient(corner[k]), GLOBE_R));
        }
        tri(b, kind == 1 ? land : (kind == 2 ? ice : sea), p, NULL, NULL, sub(p[0], v3m(0, 5.0f, 0)));
    }
}

/* The jewel cases. Width x, height y, depth z; scaled down to menu icon size. */
static void
build_case(builder* b, uint32_t spine_c, uint32_t tray_c, uint32_t edge_c, int pal) {
    const float S = 0.75f;
    /* The PAL case has a wide blue spine band that also shows on the front, the picture fills the rest. */
    const float hw = (pal ? 6.78f : 6.6f) * S, hh = 6.2f * S, hd = (pal ? 1.0f : 0.5f) * S; /* the PAL cases are about twice as thick: 20 mm against 10 (measured from a photo of a stack) */
    const float spine_w = (pal ? 1.5f : 1.0f) * S;
    /* art window on the front: 11.8 x 11.8 */
    const float ax0 = pal ? -hw + spine_w : -5.4f * S, ax1 = hw - 0.2f * S, ay0 = -5.9f * S, ay1 = 5.9f * S;

    /* polygon order is draw order (the PVR sorts by submission): what lies behind first, the
     * pictures after the solid body, the translucent plastic last */
    int tray = poly_new(b, -1, tray_c, 0);
    int spine = poly_new(b, -1, spine_c, 0);
    int mark = poly_new(b, -1, 0xFFFFFFFFu, 0);
    int back = poly_new(b, BMODEL_TEX_BACK, 0xFFFFFFFFu, 0);
    int front = poly_new(b, BMODEL_TEX_FRONT, 0xFFFFFFFFu, 0);
    int edge = poly_new(b, -1, edge_c, STRIP_DOUBLE_SIDED); /* translucent plastic goes last */

    float fuv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}}; /* bottom-left, bottom-right, top-right, top-left */
    const float az = hd - 0.2f * S; /* the picture lies just behind the clear front: 2 mm gap */
    quad(b, front, v3m(ax0, ay0, az), v3m(ax1, ay0, az), v3m(ax1, ay1, az), v3m(ax0, ay1, az), fuv, v3m(0, 0, 1));
    /* the back picture covers the whole back plate; seen from behind, +x is on the left */
    float buv[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
    float bz = -hd - 0.01f;
    quad(b, back, v3m(-hw, -hh, bz), v3m(hw, -hh, bz), v3m(hw, hh, bz), v3m(-hw, hh, bz), buv, v3m(0, 0, -1));
    /* tray (behind the picture) and the spine */
    box(b, tray, -hw, -hh, -hd, hw, hh, -hd + 0.15f * S);
    box(b, spine, -hw, -hh, -hd, -hw + spine_w, hh, hd);
    if (pal) {
        /* the embossed label panel on the spine */
        int label = poly_new(b, -1, 0xFF3C66CCu, 0);
        /* The label runs the length of the spine but leaves a rim of dark blue as thick as the top edge (0.3 cm)
         * at both ends. It sits toward the front of the spine: 2 mm behind the front face, 13 mm wide. */
        const float label_front = hd - 0.2f * S, label_back = label_front - 1.3f * S;
        box(b, label, -hw - 0.04f, -hh + 0.3f * S, label_back, -hw + 0.02f, hh - 0.3f * S, label_front);
    } else {
        box(b, mark, -hw - 0.02f, -hh + 0.6f * S, -hd + 0.15f * S, -hw + 0.02f, hh - 0.6f * S, hd - 0.15f * S);
    }
    /* clear plastic: the outer faces of the edges and the rim of the front lid. No inner walls, so the
     * picture does not look like it lies at the bottom of a deep well. */
    quad(b, edge, v3m(-hw, hh, -hd), v3m(hw, hh, -hd), v3m(hw, hh, hd), v3m(-hw, hh, hd), NULL, v3m(0, 1, 0));     /* top */
    quad(b, edge, v3m(-hw, -hh, -hd), v3m(hw, -hh, -hd), v3m(hw, -hh, hd), v3m(-hw, -hh, hd), NULL, v3m(0, -1, 0)); /* bottom */
    quad(b, edge, v3m(hw, -hh, -hd), v3m(hw, hh, -hd), v3m(hw, hh, hd), v3m(hw, -hh, hd), NULL, v3m(1, 0, 0));      /* right */
    quad(b, edge, v3m(-hw, ay1, hd), v3m(hw, ay1, hd), v3m(hw, hh, hd), v3m(-hw, hh, hd), NULL, v3m(0, 0, 1));      /* front rim, top */
    quad(b, edge, v3m(-hw, -hh, hd), v3m(hw, -hh, hd), v3m(hw, ay0, hd), v3m(-hw, ay0, hd), NULL, v3m(0, 0, 1));    /* bottom */
    quad(b, edge, v3m(ax1, ay0, hd), v3m(hw, ay0, hd), v3m(hw, ay1, hd), v3m(ax1, ay1, hd), NULL, v3m(0, 0, 1));    /* right */
    if (ax0 > -hw + spine_w + 0.001f) {
        quad(b, edge, v3m(-hw + spine_w, ay0, hd), v3m(ax0, ay0, hd), v3m(ax0, ay1, hd), v3m(-hw + spine_w, ay1, hd), NULL, v3m(0, 0, 1));
    }
}

int
bmodel_build(int id, nj_object* out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    builder b;
    if (builder_start(&b) != 0) {
        return -1;
    }
    switch (id) {
        case BMODEL_PHONE: build_phone(&b); break;
        case BMODEL_GLOBE: build_globe(&b); break;
        case BMODEL_CASE_WHITE: build_case(&b, 0xFFF4F4F8u, 0xFF2C2C34u, 0x70DCE6F0u, 0); break;
        case BMODEL_CASE_PAL: build_case(&b, 0xFF1E3C96u, 0xFF1E3C96u, 0x802850B4u, 1); break;
        default:
            builder_free(&b);
            return -1;
    }
    return builder_finish(&b, out);
}
