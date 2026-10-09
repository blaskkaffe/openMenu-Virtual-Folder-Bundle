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

#define GLOBE_LON 10
#define GLOBE_LAT 6
#define GLOBE_R 4.6f
#define GLOBE_LINE_R 1.04f /* the coast lines float a little above the faceted ball */

static v3
sphere_pt(float lon_deg, float lat_deg, float r) {
    float lam = lon_deg * (PI_F / 180.0f), phi = lat_deg * (PI_F / 180.0f); /* longitude 0 faces +z */
    const float tilt = 0.41f;                                              /* 23.5 degrees of axial tilt */
    v3 p = v3m(r * cosf(phi) * sinf(lam), r * sinf(phi), r * cosf(phi) * cosf(lam));
    return v3m(p.x * cosf(tilt) - p.y * sinf(tilt), p.x * sinf(tilt) + p.y * cosf(tilt), p.z);
}

/* Coast lines as closed (lon, lat) loops in degrees, a few points per continent. */
#define LOOP_END 999.0f
static const float globe_coast[][2] = {
    /* North America */
    {-165, 65}, {-125, 70}, {-95, 72}, {-62, 58}, {-56, 48}, {-76, 35}, {-81, 25}, {-90, 29},
    {-97, 22}, {-80, 8}, {-105, 20}, {-124, 40}, {-130, 55}, {LOOP_END, 0},
    /* Greenland */
    {-55, 60}, {-20, 70}, {-25, 80}, {-60, 82}, {-65, 70}, {LOOP_END, 0},
    /* South America */
    {-80, 8}, {-62, 10}, {-35, -6}, {-40, -22}, {-58, -38}, {-68, -52}, {-74, -40}, {-71, -18}, {-81, -5}, {LOOP_END, 0},
    /* Africa */
    {-17, 21}, {-10, 35}, {10, 37}, {32, 31}, {43, 12}, {51, 12}, {40, -15}, {20, -35}, {12, -18}, {9, 4}, {-8, 5},
    {LOOP_END, 0},
    /* Europe and Asia */
    {-9, 37}, {-4, 48}, {8, 54}, {5, 62}, {25, 70}, {60, 70}, {100, 77}, {140, 72}, {178, 66}, {155, 58}, {130, 42}, {122, 30},
    {108, 20}, {100, 2}, {92, 22}, {78, 8}, {66, 25}, {50, 28}, {36, 36}, {15, 40}, {LOOP_END, 0},
    /* Australia */
    {114, -22}, {130, -12}, {142, -11}, {153, -26}, {146, -39}, {135, -33}, {115, -34}, {LOOP_END, 0},
    {LOOP_END, LOOP_END},
};

static void
globe_line(builder* b, int poly, v3 p0, v3 p1) {
    v3 mid = norm(add(p0, p1));
    v3 w = mul(norm(cross(mid, sub(p1, p0))), 0.11f);
    quad(b, poly, add(p0, w), add(p1, w), sub(p1, w), sub(p0, w), NULL, mid);
}

static void
build_globe(builder* b) {
    int sea = poly_new(b, -1, 0xFF2D6EDCu, 0);
    int line = poly_new(b, -1, 0xFF3CC83Cu, STRIP_DOUBLE_SIDED);
    /* a smooth shaded low poly ball */
    for (int lat = 0; lat < GLOBE_LAT; lat++) {
        for (int lon = 0; lon < GLOBE_LON; lon++) {
            float l0 = -180.0f + 360.0f * lon / GLOBE_LON, l1 = -180.0f + 360.0f * (lon + 1) / GLOBE_LON;
            float a0 = 90.0f - 180.0f * lat / GLOBE_LAT, a1 = 90.0f - 180.0f * (lat + 1) / GLOBE_LAT;
            v3 p00 = sphere_pt(l0, a0, GLOBE_R), p10 = sphere_pt(l1, a0, GLOBE_R);
            v3 p01 = sphere_pt(l0, a1, GLOBE_R), p11 = sphere_pt(l1, a1, GLOBE_R);
            v3 out = add(add(p00, p10), add(p01, p11));
            if (lat == 0) {
                v3 t[3] = {p00, p11, p01};
                v3 n[3] = {norm(p00), norm(p11), norm(p01)};
                tri(b, sea, t, n, NULL, out);
            } else if (lat == GLOBE_LAT - 1) {
                v3 t[3] = {p00, p10, p01};
                v3 n[3] = {norm(p00), norm(p10), norm(p01)};
                tri(b, sea, t, n, NULL, out);
            } else {
                v3 t1[3] = {p00, p10, p11}, t2[3] = {p00, p11, p01};
                v3 n1[3] = {norm(p00), norm(p10), norm(p11)}, n2[3] = {norm(p00), norm(p11), norm(p01)};
                tri(b, sea, t1, n1, NULL, out);
                tri(b, sea, t2, n2, NULL, out);
            }
        }
    }
    /* the map: coast lines as thin ribbons, long edges cut in two so they do not dive into the ball */
    int start = 0;
    for (int i = 0;; i++) {
        if (globe_coast[i][0] == LOOP_END) {
            if (globe_coast[i][1] == LOOP_END) {
                break;
            }
            int n = i - start;
            for (int k = 0; k < n; k++) {
                const float* a = globe_coast[start + k];
                const float* c = globe_coast[start + (k + 1) % n];
                float dl = fabsf(c[0] - a[0]), dp = fabsf(c[1] - a[1]);
                float len = dl > dp ? dl : dp;
                int cuts = len > 40.0f ? 2 : 1;
                for (int q = 0; q < cuts; q++) {
                    float f0 = (float)q / cuts, f1 = (float)(q + 1) / cuts;
                    globe_line(b, line, sphere_pt(a[0] + (c[0] - a[0]) * f0, a[1] + (c[1] - a[1]) * f0, GLOBE_R * GLOBE_LINE_R),
                               sphere_pt(a[0] + (c[0] - a[0]) * f1, a[1] + (c[1] - a[1]) * f1, GLOBE_R * GLOBE_LINE_R));
                }
            }
            start = i + 1;
        }
    }
}

/* The jewel cases. Width x, height y, depth z; scaled down to menu icon size. */
static void
build_case(builder* b, uint32_t spine_c, uint32_t tray_c, uint32_t edge_c, int pal) {
    const float S = 0.75f;
    /* The PAL case has a wide blue spine band that also shows on the front, the picture fills the rest. */
    const float hw = (pal ? 6.78f : 6.6f) * S, hh = 6.2f * S, hd = 0.5f * S;
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
    quad(b, front, v3m(ax0, ay0, hd), v3m(ax1, ay0, hd), v3m(ax1, ay1, hd), v3m(ax0, ay1, hd), fuv, v3m(0, 0, 1));
    /* the back picture covers the whole back plate; seen from behind, +x is on the left */
    float buv[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
    float bz = -hd - 0.01f;
    quad(b, back, v3m(-hw, -hh, bz), v3m(hw, -hh, bz), v3m(hw, hh, bz), v3m(-hw, hh, bz), buv, v3m(0, 0, -1));
    /* tray (behind the picture) and the spine */
    box(b, tray, -hw, -hh, -hd, hw, hh, -hd + 0.15f * S);
    box(b, spine, -hw, -hh, -hd, -hw + spine_w, hh, hd);
    if (pal) {
        /* the embossed label panel on the spine, lower part */
        int label = poly_new(b, -1, 0xFF3C66CCu, 0);
        box(b, label, -hw - 0.04f, -4.9f * S, -hd + 0.18f * S, -hw + 0.02f, 1.2f * S, hd - 0.18f * S);
    } else {
        box(b, mark, -hw - 0.02f, -hh + 0.6f * S, -hd + 0.15f * S, -hw + 0.02f, hh - 0.6f * S, hd - 0.15f * S);
    }
    /* clear plastic: frame of the front lid and the edges */
    float t = 0.3f * S;
    box(b, edge, -hw, ay1, -hd, hw, hh, hd);                       /* top, from the edge round to the front */
    box(b, edge, -hw, -hh, -hd, hw, ay0, hd);                      /* bottom */
    box(b, edge, ax1, ay0, -hd, hw, ay1, hd);                      /* right */
    box(b, edge, -hw + spine_w, ay0, hd - t, ax0, ay1, hd);        /* hinge side of the lid */
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
