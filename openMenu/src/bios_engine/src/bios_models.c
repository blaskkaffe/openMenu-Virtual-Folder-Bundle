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
frustum(builder* b, int poly, float cx, float cz, float y0, float y1, float w0, float d0, float w1, float d1) {
    v3 bt[4] = {v3m(cx - w0, y0, cz - d0), v3m(cx + w0, y0, cz - d0), v3m(cx + w0, y0, cz + d0), v3m(cx - w0, y0, cz + d0)};
    v3 tp[4] = {v3m(cx - w1, y1, cz - d1), v3m(cx + w1, y1, cz - d1), v3m(cx + w1, y1, cz + d1), v3m(cx - w1, y1, cz + d1)};
    v3 mid = v3m(cx, (y0 + y1) / 2, cz);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        v3 fc = mul(add(add(bt[i], bt[j]), add(tp[i], tp[j])), 0.25f);
        quad(b, poly, bt[i], bt[j], tp[j], tp[i], NULL, sub(fc, mid));
    }
    quad(b, poly, tp[0], tp[1], tp[2], tp[3], NULL, v3m(0, 1, 0));
    quad(b, poly, bt[0], bt[1], bt[2], bt[3], NULL, v3m(0, -1, 0));
}

/* Cylinder (or cone frustum) from base centre `c` along `axis`; smooth radial normals on the side. */
static void
cylinder(builder* b, int side_poly, int cap_poly, int seg, v3 c, v3 axis, float r0, float r1, float len) {
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
        if (r1 > 0.0f) {
            v3 f[3] = {top, t0p, t1p};
            tri(b, cap_poly, f, NULL, NULL, a);
        }
        if (r0 > 0.0f) {
            v3 f[3] = {c, b0, b1};
            tri(b, cap_poly, f, NULL, NULL, mul(a, -1.0f));
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
    const uint32_t body_c = BMODEL_NOTE_COLOR, light_c = 0xFFB299FFu, dark_c = 0xFF4C3DB3u;
    int body = poly_new(b, -1, body_c, 0);
    int light = poly_new(b, -1, light_c, 0);
    int dark = poly_new(b, -1, dark_c, 0);
    int white = poly_new(b, -1, 0xFFFFFFFFu, 0);
    int hub = poly_new(b, -1, 0xFFD2D2E6u, 0);

    /* base, wider at the foot */
    frustum(b, body, 0.0f, 0.0f, 0.0f, 2.4f, 4.6f, 3.6f, 3.8f, 3.0f);
    /* dial plate and the white dial disc, tilted toward the viewer */
    v3 tilt = norm(v3m(0.0f, cosf(0.55f), sinf(0.55f)));
    v3 centre = v3m(0.0f, 2.4f, 0.9f);
    cylinder(b, dark, dark, 24, add(centre, mul(tilt, -0.05f)), tilt, 2.75f, 2.75f, 0.35f);
    cylinder(b, white, white, 24, add(centre, mul(tilt, 0.3f)), tilt, 2.3f, 2.3f, 0.35f);
    cylinder(b, hub, hub, 12, add(centre, mul(tilt, 0.65f)), tilt, 0.75f, 0.6f, 0.25f);
    /* cradle posts under the handset cups */
    cylinder(b, dark, dark, 8, v3m(-3.4f, 2.4f, -1.6f), v3m(0, 1, 0), 0.5f, 0.5f, 1.5f);
    cylinder(b, dark, dark, 8, v3m(3.4f, 2.4f, -1.6f), v3m(0, 1, 0), 0.5f, 0.5f, 1.5f);
    /* handset: grip bar and the two cups */
    box(b, light, -3.4f, 4.5f, -2.0f, 3.4f, 5.3f, -1.2f);
    cylinder(b, light, body, 14, v3m(-3.4f, 3.9f, -1.6f), v3m(0, 1, 0), 1.05f, 1.15f, 1.5f);
    cylinder(b, light, body, 14, v3m(3.4f, 3.9f, -1.6f), v3m(0, 1, 0), 1.05f, 1.15f, 1.5f);
}

#define GLOBE_LON 24
#define GLOBE_LAT 12
#define GLOBE_R_SEA 3.9f
#define GLOBE_R_LAND 4.1f
/* Coarse map, 15 degree cells, north to south, west (180W) to east. '#' is land. */
static const char* const globe_map[GLOBE_LAT] = {
    "........###.............", /* 90N - 75N */
    ".##########.############", /* 75N - 60N */
    ".#######...#############", /* 60N - 45N */
    "...#####...##.########..", /* 45N - 30N */
    "....###....#####.####...", /* 30N - 15N */
    "......###..#####.#.###..", /* 15N - 0 */
    ".......###..###....####.", /* 0 - 15S */
    ".......##....##....####.", /* 15S - 30S */
    ".......#.....#.....###.#", /* 30S - 45S */
    ".......#................", /* 45S - 60S */
    "......##..#####..######.", /* 60S - 75S */
    "########################", /* 75S - 90S */
};

static int
globe_land(int lon, int lat) {
    if (lat < 0 || lat >= GLOBE_LAT) {
        return 0;
    }
    lon = ((lon % GLOBE_LON) + GLOBE_LON) % GLOBE_LON;
    return globe_map[lat][lon] == '#';
}

static v3
sphere_pt(int lon, int lat, float r) {
    float lam = (-PI_F) + 2 * PI_F * lon / GLOBE_LON; /* longitude, 0 faces +z */
    float phi = PI_F / 2 - PI_F * lat / GLOBE_LAT;
    return v3m(r * cosf(phi) * sinf(lam), r * sinf(phi), r * cosf(phi) * cosf(lam));
}

/* Rotate about z by `a` radians (the tilt of the earth's axis) */
static v3
tilt_z(v3 p, float a) {
    return v3m(p.x * cosf(a) - p.y * sinf(a), p.x * sinf(a) + p.y * cosf(a), p.z);
}

static void
build_globe(builder* b) {
    int sea = poly_new(b, -1, 0xFF2D6EDCu, 0);
    int land = poly_new(b, -1, 0xFF3CB43Cu, 0);
    int cliff = poly_new(b, -1, 0xFF2A7F2Au, 0);
    int metal = poly_new(b, -1, 0xFFC8C8D2u, 0);
    int dark = poly_new(b, -1, 0xFF5A5A6Eu, 0);
    const float tilt = 0.41f; /* 23.5 degrees */
    const v3 gc = v3m(0.0f, 5.6f, 0.0f);

    for (int lat = 0; lat < GLOBE_LAT; lat++) {
        for (int lon = 0; lon < GLOBE_LON; lon++) {
            int is_land = globe_land(lon, lat);
            float r = is_land ? GLOBE_R_LAND : GLOBE_R_SEA;
            v3 p00 = add(gc, tilt_z(sphere_pt(lon, lat, r), tilt));
            v3 p10 = add(gc, tilt_z(sphere_pt(lon + 1, lat, r), tilt));
            v3 p01 = add(gc, tilt_z(sphere_pt(lon, lat + 1, r), tilt));
            v3 p11 = add(gc, tilt_z(sphere_pt(lon + 1, lat + 1, r), tilt));
            v3 mid = mul(add(add(p00, p10), add(p01, p11)), 0.25f);
            v3 out = sub(mid, gc);
            int pm = is_land ? land : sea;
            if (lat == 0) {
                v3 t[3] = {p00, p10, p01};
                tri(b, pm, t, NULL, NULL, out);
                v3 t2[3] = {p10, p11, p01};
                tri(b, pm, t2, NULL, NULL, out);
            } else if (lat == GLOBE_LAT - 1) {
                v3 t[3] = {p00, p10, p01};
                tri(b, pm, t, NULL, NULL, out);
                v3 t2[3] = {p10, p11, p01};
                tri(b, pm, t2, NULL, NULL, out);
            } else {
                quad(b, pm, p00, p10, p11, p01, NULL, out);
            }
            if (is_land) {
                /* cliffs toward neighbouring sea cells, from the sea level up to the land */
                int nl[4][2] = {{lon - 1, lat}, {lon + 1, lat}, {lon, lat - 1}, {lon, lat + 1}};
                for (int k = 0; k < 4; k++) {
                    int nlat = nl[k][1];
                    if (nlat < 0 || nlat >= GLOBE_LAT || globe_land(nl[k][0], nlat)) {
                        continue;
                    }
                    int ea = (k < 2) ? lon + k : lon, la = (k < 2) ? lat : lat + (k - 2);
                    int eb = (k < 2) ? lon + k : lon + 1, lb = (k < 2) ? lat + 1 : lat + (k - 2);
                    v3 hi0 = add(gc, tilt_z(sphere_pt(ea, la, GLOBE_R_LAND), tilt));
                    v3 hi1 = add(gc, tilt_z(sphere_pt(eb, lb, GLOBE_R_LAND), tilt));
                    v3 lo0 = add(gc, tilt_z(sphere_pt(ea, la, GLOBE_R_SEA), tilt));
                    v3 lo1 = add(gc, tilt_z(sphere_pt(eb, lb, GLOBE_R_SEA), tilt));
                    v3 fc = mul(add(add(hi0, hi1), add(lo0, lo1)), 0.25f);
                    v3 dir = sub(fc, mid); /* away from the land cell's own middle */
                    quad(b, cliff, lo0, lo1, hi1, hi0, NULL, dir);
                }
            }
        }
    }
    /* the stand: foot, stem and the half ring that holds the earth (in the x-y plane, under the globe) */
    cylinder(b, metal, dark, 20, v3m(0, 0, 0), v3m(0, 1, 0), 2.4f, 2.0f, 0.5f);
    cylinder(b, metal, metal, 10, v3m(0, 0.5f, 0), v3m(0, 1, 0), 0.4f, 0.4f, 1.0f);
    const int steps = 14;
    const float ring_r = 4.7f, th = 0.22f;
    for (int i = 0; i < steps; i++) {
        float a0 = PI_F * (1.15f + 0.7f * i / steps), a1 = PI_F * (1.15f + 0.7f * (i + 1) / steps);
        v3 q0 = add(gc, v3m(ring_r * cosf(a0), ring_r * sinf(a0), 0)), q1 = add(gc, v3m(ring_r * cosf(a1), ring_r * sinf(a1), 0));
        v3 s = v3m(0, 0, th);
        v3 r0 = norm(sub(q0, gc)), r1 = norm(sub(q1, gc));
        v3 in0 = mul(r0, th), in1 = mul(r1, th);
        v3 o0f = add(q0, s), o1f = add(q1, s), o0b = sub(q0, s), o1b = sub(q1, s);
        v3 i0f = sub(o0f, in0), i1f = sub(o1f, in1), i0b = sub(o0b, in0), i1b = sub(o1b, in1);
        v3 oc = mul(add(q0, q1), 0.5f);
        v3 outd = sub(oc, gc);
        quad(b, metal, o0f, o1f, o1b, o0b, NULL, outd);           /* outside */
        quad(b, metal, i0f, i1f, i1b, i0b, NULL, mul(outd, -1));  /* inside */
        quad(b, metal, i0f, i1f, o1f, o0f, NULL, v3m(0, 0, 1));   /* front */
        quad(b, metal, i0b, i1b, o1b, o0b, NULL, v3m(0, 0, -1));  /* back */
    }
}

/* The jewel cases. Width x, height y, depth z; scaled down to menu icon size. */
static void
build_case(builder* b, uint32_t spine_c, uint32_t tray_c, uint32_t edge_c, int pal) {
    const float S = 0.75f;
    const float hw = 6.6f * S, hh = 6.2f * S, hd = 0.5f * S;
    const float spine_w = 1.0f * S;
    /* art window on the front: 11.8 x 11.8, leaving the hinge side on the left */
    const float ax0 = -5.4f * S, ax1 = 6.4f * S, ay0 = -5.9f * S, ay1 = 5.9f * S;

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
        /* white band and a small round mark on the blue spine, in the manner of the PAL cases */
        box(b, mark, -hw - 0.02f, hh - 2.2f * S, -hd + 0.1f * S, -hw + 0.02f, hh - 1.9f * S, hd - 0.1f * S);
        cylinder(b, mark, mark, 16, v3m(-hw - 0.03f, 0.0f, 0.0f), v3m(1, 0, 0), 0.28f * S * 2, 0.28f * S * 2, 0.02f);
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
