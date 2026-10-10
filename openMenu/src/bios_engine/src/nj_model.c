/*
 * nj_model: Ninja chunk-model objects and motions, see nj_model.h.
 * Structure and constants follow the reference parsers in the boot ROM decompile.
 */
#include "nj_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 256
#define MAX_DEPTH 16
#define MAX_VERTS 4096
#define MAX_STRIP_LEN 4096
#define MAX_CHUNKS 4096

#define NJ_TWO_PI 6.283185307179586f

typedef struct {
    const bios_rom* rom;
    int err;
} reader;

static const uint8_t*
rd_ptr(reader* r, uint32_t addr, size_t len) {
    const uint8_t* p = bios_rom_ptr(r->rom, addr, len);
    if (!p) {
        r->err = 1;
    }
    return p;
}

static uint32_t
rd_u32(reader* r, uint32_t addr) {
    const uint8_t* p = rd_ptr(r, addr, 4);
    return p ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24) : 0;
}

static uint16_t
rd_u16(reader* r, uint32_t addr) {
    const uint8_t* p = rd_ptr(r, addr, 2);
    return p ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
}

static float
rd_f32(reader* r, uint32_t addr) {
    uint32_t u = rd_u32(r, addr);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

/* ---- Vertex chunks ---------------------------------------------------------- */

/* Size in bytes of one vertex per chunk type 0x20..0x32, 0 = unsupported */
static int
vertex_size(unsigned type) {
    static const uint8_t sizes[] = {16, 32, 12, 16, 16, 16, 16, 16, 16, 24, 28, 28, 28, 28, 28, 28, 16, 20, 20};
    return (type >= 0x20 && type <= 0x32) ? sizes[type - 0x20] : 0;
}

/* Walk the vertex chunk list. With fill == NULL only the vertex count is measured. */
static int
parse_vlist(reader* r, uint32_t a, nj_mesh* fill, int* max_index) {
    for (int chunks = 0; chunks < MAX_CHUNKS; chunks++) {
        uint32_t h = rd_u32(r, a);
        unsigned t = h & 0xFF;
        if (r->err) {
            return -1;
        }
        if (t == 0xFF) {
            return 0;
        }
        int vs = vertex_size(t);
        if (!vs) {
            return -1;
        }
        uint32_t size_words = h >> 16;
        uint32_t info = rd_u32(r, a + 4);
        uint32_t off = info & 0xFFFF, cnt = info >> 16;
        if (off + cnt > MAX_VERTS) {
            return -1;
        }
        uint32_t p = a + 8;
        if (max_index && (int)(off + cnt) > *max_index) {
            *max_index = (int)(off + cnt);
        }
        for (uint32_t i = 0; fill && i < cnt; i++, p += (uint32_t)vs) {
            nj_vertex* v = &fill->verts[off + i];
            v->pos.x = rd_f32(r, p);
            v->pos.y = rd_f32(r, p + 4);
            v->pos.z = rd_f32(r, p + 8);
            v->valid = 1;
            if (t == 0x21 || (t >= 0x29 && t <= 0x2F)) {
                uint32_t n = (t == 0x21) ? 16 : 12;
                v->nrm.x = rd_f32(r, p + n);
                v->nrm.y = rd_f32(r, p + n + 4);
                v->nrm.z = rd_f32(r, p + n + 8);
                v->has_nrm = 1;
            }
            if (t == 0x23 || t == 0x2A) {
                v->col = rd_u32(r, p + (t == 0x23 ? 12 : 24));
                v->has_col = 1;
            }
        }
        if (!fill) {
            p += cnt * (uint32_t)vs;
        }
        if (p != a + 4 + size_words * 4) {
            return -1; /* size mismatch: not the layout we think it is */
        }
        a = p;
    }
    return -1;
}

/* ---- Polygon chunks ---------------------------------------------------------- */

static int
strip_extra(unsigned t) {
    static const uint8_t extra[] = {0, 2, 2, 3, 5, 5, 2, 4, 4, 0, 4, 4}; /* chunk types 64..75 */
    return extra[t - 64];
}

static float
strip_uv_scale(unsigned t) {
    switch (t) {
        case 65: case 68: case 71: case 74: return 255.0f;
        case 66: case 69: case 72: case 75: return 1023.0f;
        default: return 0.0f;
    }
}

static int
add_poly(nj_mesh* m, const nj_poly* p) {
    nj_poly* np = (nj_poly*)realloc(m->polys, sizeof(nj_poly) * (size_t)(m->npolys + 1));
    if (!np) {
        return -1;
    }
    m->polys = np;
    m->polys[m->npolys++] = *p;
    return 0;
}

static int
parse_strip(reader* r, uint32_t a, unsigned t, uint8_t flags, int tex, const nj_poly* state, nj_mesh* m) {
    uint32_t size = rd_u16(r, a + 2);
    uint32_t body = a + 4;
    uint16_t hdr = rd_u16(r, body);
    int nstrip = hdr & 0x3FFF, user = hdr >> 14;
    float uvs = strip_uv_scale(t);
    int extra = strip_extra(t);
    uint32_t q = body + 2;
    nj_poly poly = *state;
    poly.tex = tex;
    poly.strip_flags = flags;
    poly.has_uv = uvs != 0.0f;
    poly.ntris = 0;
    poly.corners = NULL;
    int cap = 0;

    for (int s = 0; s < nstrip && !r->err; s++) {
        int ln = (int16_t)rd_u16(r, q);
        q += 2;
        int rev = ln < 0;
        if (rev) {
            ln = -ln;
        }
        if (ln > MAX_STRIP_LEN) {
            free(poly.corners);
            return -1;
        }
        nj_corner prev[3];
        memset(prev, 0, sizeof(prev));
        for (int k = 0; k < ln; k++) {
            nj_corner c;
            c.idx = rd_u16(r, q);
            c.u = c.v = 0.0f;
            if (uvs != 0.0f) {
                c.u = (int16_t)rd_u16(r, q + 2) / uvs;
                c.v = (int16_t)rd_u16(r, q + 4) / uvs;
            }
            q += 2 + (uint32_t)extra * 2;
            if (k >= 2) {
                q += (uint32_t)user * 2;
            }
            prev[0] = prev[1];
            prev[1] = prev[2];
            prev[2] = c;
            if (k >= 2) {
                nj_corner a0 = prev[0], a1 = prev[1], a2 = prev[2];
                if ((((k - 2) & 1) == 1) ^ rev) {
                    nj_corner tmp = a0;
                    a0 = a1;
                    a1 = tmp;
                }
                if (poly.ntris + 1 > cap) {
                    cap = cap ? cap * 2 : 32;
                    nj_corner* nc = (nj_corner*)realloc(poly.corners, sizeof(nj_corner) * 3 * (size_t)cap);
                    if (!nc) {
                        free(poly.corners);
                        return -1;
                    }
                    poly.corners = nc;
                }
                poly.corners[poly.ntris * 3 + 0] = a0;
                poly.corners[poly.ntris * 3 + 1] = a1;
                poly.corners[poly.ntris * 3 + 2] = a2;
                poly.ntris++;
            }
        }
    }

    uint32_t end = body + size * 2;
    if (r->err || q > end + 0 || end - q > 2) {
        free(poly.corners);
        return -1;
    }
    if (add_poly(m, &poly) != 0) {
        free(poly.corners);
        return -1;
    }
    return 0;
}

static int
parse_plist(reader* r, uint32_t a, nj_mesh* m) {
    int tex = -1;
    nj_poly state;
    memset(&state, 0, sizeof(state));
    state.tex = -1;

    for (int chunks = 0; chunks < MAX_CHUNKS; chunks++) {
        uint16_t h = rd_u16(r, a);
        unsigned t = h & 0xFF;
        uint8_t fl = (uint8_t)(h >> 8);
        if (r->err) {
            return -1;
        }
        if (t == 0xFF) {
            return 0;
        }
        if (t == 0) {
            a += 2;
            continue;
        }
        if (t >= 1 && t <= 5) {
            if (t == 1) {
                state.blend = fl;
            }
            a += 2;
            continue;
        }
        if (t == 8 || t == 9) {
            tex = rd_u16(r, a + 2) & 0x1FFF;
            a += 4;
            continue;
        }
        uint32_t size = rd_u16(r, a + 2);
        uint32_t body = a + 4;
        if (t >= 16 && t <= 31) {
            /* material chunk: the colours present follow in the order diffuse, ambient, specular */
            uint32_t at = body;
            if (t & 1) {
                state.diffuse = rd_u32(r, at);
                state.has_diffuse = 1;
                at += 4;
            }
            if (t & 2) {
                state.ambient = rd_u32(r, at);
                state.has_ambient = 1;
                at += 4;
            }
            if (t & 4) {
                state.specular = rd_u32(r, at);
                state.has_specular = 1;
            }
            a = body + size * 2;
            continue;
        }
        if (t >= 56 && t <= 58) {
            a = body + size * 2;
            continue;
        }
        if (t >= 64 && t <= 75) {
            if (parse_strip(r, a, t, fl, tex, &state, m) != 0) {
                return -1;
            }
            a = body + size * 2;
            continue;
        }
        return -1;
    }
    return -1;
}

static nj_mesh*
load_mesh(reader* r, uint32_t addr) {
    uint32_t vl = rd_u32(r, addr), pl = rd_u32(r, addr + 4);
    if (r->err) {
        return NULL;
    }
    nj_mesh* m = (nj_mesh*)calloc(1, sizeof(nj_mesh));
    if (!m) {
        return NULL;
    }
    int n = 0;
    if (vl) {
        if (parse_vlist(r, vl, NULL, &n) != 0 || n <= 0) {
            goto fail;
        }
        m->verts = (nj_vertex*)calloc((size_t)n, sizeof(nj_vertex));
        if (!m->verts) {
            goto fail;
        }
        m->nverts = n;
        if (parse_vlist(r, vl, m, NULL) != 0) {
            goto fail;
        }
    }
    if (pl && parse_plist(r, pl, m) != 0) {
        goto fail;
    }
    /* every referenced vertex must exist */
    for (int i = 0; i < m->npolys; i++) {
        for (int k = 0; k < m->polys[i].ntris * 3; k++) {
            if (m->polys[i].corners[k].idx >= m->nverts || !m->verts[m->polys[i].corners[k].idx].valid) {
                goto fail;
            }
        }
    }
    return m;
fail:
    for (int i = 0; i < m->npolys; i++) {
        free(m->polys[i].corners);
    }
    free(m->polys);
    free(m->verts);
    free(m);
    return NULL;
}

/* ---- Object trees ------------------------------------------------------------ */

static int
walk(reader* r, uint32_t a, int parent, int depth, nj_object* obj) {
    while (a) {
        if (depth > MAX_DEPTH || obj->count >= MAX_NODES) {
            return -1;
        }
        nj_node n;
        memset(&n, 0, sizeof(n));
        n.eval = rd_u32(r, a);
        uint32_t model = rd_u32(r, a + 4);
        n.pos.x = rd_f32(r, a + 8);
        n.pos.y = rd_f32(r, a + 12);
        n.pos.z = rd_f32(r, a + 16);
        for (int i = 0; i < 3; i++) {
            n.ang[i] = (int16_t)(rd_u32(r, a + 0x14 + 4u * (uint32_t)i) & 0xFFFF);
        }
        n.scl.x = rd_f32(r, a + 0x20);
        n.scl.y = rd_f32(r, a + 0x24);
        n.scl.z = rd_f32(r, a + 0x28);
        uint32_t child = rd_u32(r, a + 0x2C);
        uint32_t sibling = rd_u32(r, a + 0x30);
        if (r->err) {
            return -1;
        }
        n.parent = parent;
        if (model) {
            n.mesh = load_mesh(r, model);
            if (!n.mesh) {
                return -1;
            }
        }
        nj_node* nn = (nj_node*)realloc(obj->nodes, sizeof(nj_node) * (size_t)(obj->count + 1));
        if (!nn) {
            free(n.mesh);
            return -1;
        }
        obj->nodes = nn;
        int self = obj->count++;
        obj->nodes[self] = n;
        if (walk(r, child, self, depth + 1, obj) != 0) {
            return -1;
        }
        a = sibling;
    }
    return 0;
}

static void
free_mesh(nj_mesh* m) {
    if (!m) {
        return;
    }
    for (int i = 0; i < m->npolys; i++) {
        free(m->polys[i].corners);
    }
    free(m->polys);
    free(m->verts);
    free(m);
}

void
nj_object_free(nj_object* obj) {
    if (!obj) {
        return;
    }
    for (int i = 0; i < obj->count; i++) {
        free_mesh(obj->nodes[i].mesh);
    }
    free(obj->nodes);
    obj->nodes = NULL;
    obj->count = 0;
}

int
nj_object_load(const bios_rom* rom, uint32_t addr, nj_object* out) {
    if (!rom || !out || !addr) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    reader r = {rom, 0};
    if (walk(&r, addr, -1, 0, out) != 0 || r.err || out->count == 0) {
        nj_object_free(out);
        return -1;
    }
    return 0;
}

/* ---- Motions ------------------------------------------------------------------ */

static int
load_channel(reader* r, uint32_t keys, uint32_t count, uint32_t nframes, int angles, nj_channel* ch) {
    if (count == 0) {
        return keys == 0 ? 0 : -1;
    }
    if (!keys || count > nframes + 1) {
        return -1;
    }
    ch->keys = (nj_key*)calloc(count, sizeof(nj_key));
    if (!ch->keys) {
        return -1;
    }
    ch->count = (int)count;
    for (uint32_t j = 0; j < count; j++) {
        uint32_t q = keys + 16 * j;
        ch->keys[j].frame = rd_u32(r, q);
        for (int t = 0; t < 3; t++) {
            if (angles) {
                uint32_t raw = rd_u32(r, q + 4 + 4u * (uint32_t)t);
                ch->keys[j].v[t] = (float)(int16_t)(raw & 0xFFFF);
            } else {
                ch->keys[j].v[t] = rd_f32(r, q + 4 + 4u * (uint32_t)t);
            }
        }
        if (j > 0 && ch->keys[j].frame < ch->keys[j - 1].frame) {
            return -1;
        }
    }
    return r->err ? -1 : 0;
}

void
nj_motion_free(nj_motion* m) {
    if (!m) {
        return;
    }
    for (int i = 0; i < m->count; i++) {
        free(m->nodes[i].pos.keys);
        free(m->nodes[i].ang.keys);
        free(m->nodes[i].scl.keys);
    }
    free(m->nodes);
    memset(m, 0, sizeof(*m));
}

int
nj_motion_load(const bios_rom* rom, uint32_t addr, int node_count, nj_motion* out) {
    if (!rom || !out || !addr || node_count <= 0 || node_count > MAX_NODES) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    reader r = {rom, 0};
    uint32_t md = rd_u32(&r, addr);
    uint32_t nf = rd_u32(&r, addr + 4);
    uint16_t inp = rd_u16(&r, addr + 10);
    if (r.err || nf > 100000) {
        return -1;
    }
    out->frames = (int)nf;
    out->spline = (inp & 0x40) != 0;
    out->nodes = (nj_motion_node*)calloc((size_t)node_count, sizeof(nj_motion_node));
    if (!out->nodes) {
        return -1;
    }
    out->count = node_count;
    for (int k = 0; k < node_count; k++) {
        uint32_t e = md + 24u * (uint32_t)k;
        uint32_t p[3], c[3];
        for (int i = 0; i < 3; i++) {
            p[i] = rd_u32(&r, e + 4u * (uint32_t)i);
            c[i] = rd_u32(&r, e + 12 + 4u * (uint32_t)i);
        }
        nj_motion_node* mn = &out->nodes[k];
        int unreadable = r.err;
        r.err = 0;
        if (unreadable || load_channel(&r, p[0], c[0], nf, 0, &mn->pos) != 0 || load_channel(&r, p[1], c[1], nf, 1, &mn->ang) != 0
            || load_channel(&r, p[2], c[2], nf, 0, &mn->scl) != 0) {
            /* The file does not say how many nodes it animates, so the first entry
             * that does not look like keyframe data ends the list (as the reference
             * tool does). Having none at all is an error. */
            free(mn->pos.keys);
            free(mn->ang.keys);
            free(mn->scl.keys);
            memset(mn, 0, sizeof(*mn));
            if (k == 0) {
                nj_motion_free(out);
                return -1;
            }
            out->count = k;
            return 0;
        }
    }
    return 0;
}

/* Linear sample of one channel; angles take the short way round. Returns 0 if the channel is empty. */
static int
sample_channel(const nj_channel* ch, float frame, int angles, float out[3]) {
    if (ch->count == 0) {
        return 0;
    }
    const nj_key* k = ch->keys;
    if (ch->count == 1 || frame <= (float)k[0].frame) {
        memcpy(out, k[0].v, sizeof(float) * 3);
        return 1;
    }
    if (frame >= (float)k[ch->count - 1].frame) {
        memcpy(out, k[ch->count - 1].v, sizeof(float) * 3);
        return 1;
    }
    int i = 0;
    while (i + 1 < ch->count && (float)k[i + 1].frame <= frame) {
        i++;
    }
    float span = (float)(k[i + 1].frame - k[i].frame);
    float t = span > 0.0f ? (frame - (float)k[i].frame) / span : 0.0f;
    for (int c = 0; c < 3; c++) {
        float a = k[i].v[c], b = k[i + 1].v[c];
        float d = b - a;
        if (angles) {
            while (d > 32768.0f) {
                d -= 65536.0f;
            }
            while (d < -32768.0f) {
                d += 65536.0f;
            }
        }
        out[c] = a + d * t;
    }
    return 1;
}

/* ---- Matrices ------------------------------------------------------------------- */

void
nj_mat_identity(nj_mat4* r) {
    memset(r, 0, sizeof(*r));
    r->m[0][0] = r->m[1][1] = r->m[2][2] = r->m[3][3] = 1.0f;
}

void
nj_mat_mul(nj_mat4* r, const nj_mat4* a, const nj_mat4* b) {
    nj_mat4 t;
    if (a->m[3][0] == 0.0f && a->m[3][1] == 0.0f && a->m[3][2] == 0.0f && a->m[3][3] == 1.0f && b->m[3][0] == 0.0f &&
        b->m[3][1] == 0.0f && b->m[3][2] == 0.0f && b->m[3][3] == 1.0f) {
        /* both affine (every matrix of the models and objects): 36 multiplications instead of 64 */
        for (int i = 0; i < 3; i++) {
            const float a0 = a->m[i][0], a1 = a->m[i][1], a2 = a->m[i][2];
            t.m[i][0] = a0 * b->m[0][0] + a1 * b->m[1][0] + a2 * b->m[2][0];
            t.m[i][1] = a0 * b->m[0][1] + a1 * b->m[1][1] + a2 * b->m[2][1];
            t.m[i][2] = a0 * b->m[0][2] + a1 * b->m[1][2] + a2 * b->m[2][2];
            t.m[i][3] = a0 * b->m[0][3] + a1 * b->m[1][3] + a2 * b->m[2][3] + a->m[i][3];
        }
        t.m[3][0] = t.m[3][1] = t.m[3][2] = 0.0f;
        t.m[3][3] = 1.0f;
        *r = t;
        return;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
        }
    }
    *r = t;
}

nj_vec3
nj_mat_apply(const nj_mat4* m, nj_vec3 p) {
    nj_vec3 r;
    r.x = m->m[0][0] * p.x + m->m[0][1] * p.y + m->m[0][2] * p.z + m->m[0][3];
    r.y = m->m[1][0] * p.x + m->m[1][1] * p.y + m->m[1][2] * p.z + m->m[1][3];
    r.z = m->m[2][0] * p.x + m->m[2][1] * p.y + m->m[2][2] * p.z + m->m[2][3];
    return r;
}

float
nj_ang_to_rad(int32_t a) {
    return (float)a * NJ_TWO_PI / 65536.0f;
}

/* sin and cos of a Ninja angle (0x10000 = 360 degrees), from a table as njSin / njCos do: much cheaper than sinf
 * on the SH-4, and the angles are whole Ninja units anyway (linear between entries for the fractions of motions). */
#define SIN_BITS 12
#define SIN_N (1 << SIN_BITS)
static float sin_tab[SIN_N + 1];
static int sin_ready;

static void
ang_sincos(float ang, float* s, float* c) {
    if (!sin_ready) {
        for (int i = 0; i <= SIN_N; i++) {
            sin_tab[i] = sinf((float)i * NJ_TWO_PI / (float)SIN_N);
        }
        sin_ready = 1;
    }
    /* position in the table, wrapped to one turn */
    float u = ang * ((float)SIN_N / 65536.0f);
    int i = (int)u;
    if (u < (float)i) {
        i--; /* floor for negative angles */
    }
    float f = u - (float)i;
    int is = i & (SIN_N - 1), ic = (i + SIN_N / 4) & (SIN_N - 1);
    *s = sin_tab[is] + (sin_tab[is + 1] - sin_tab[is]) * f;
    *c = sin_tab[ic] + (sin_tab[ic + 1] - sin_tab[ic]) * f;
}

typedef struct {
    float m[3][3];
} mat3;

static void
mul3(mat3* r, const mat3* a, const mat3* b) {
    mat3 t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
        }
    }
    *r = t;
}

static void
rot3_x(mat3* r, float ang) {
    float s, c;
    ang_sincos(ang, &s, &c);
    *r = (mat3){{{1.0f, 0.0f, 0.0f}, {0.0f, c, -s}, {0.0f, s, c}}};
}

static void
rot3_y(mat3* r, float ang) {
    float s, c;
    ang_sincos(ang, &s, &c);
    *r = (mat3){{{c, 0.0f, s}, {0.0f, 1.0f, 0.0f}, {-s, 0.0f, c}}};
}

static void
rot3_z(mat3* r, float ang) {
    float s, c;
    ang_sincos(ang, &s, &c);
    *r = (mat3){{{c, -s, 0.0f}, {s, c, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

/* [m * diag(scl) | pos] as an affine 4x4 */
static void
compose(nj_mat4* out, const mat3* m, const float scl[3], const float pos[3]) {
    for (int i = 0; i < 3; i++) {
        out->m[i][0] = m->m[i][0] * scl[0];
        out->m[i][1] = m->m[i][1] * scl[1];
        out->m[i][2] = m->m[i][2] * scl[2];
        out->m[i][3] = pos[i];
    }
    out->m[3][0] = out->m[3][1] = out->m[3][2] = 0.0f;
    out->m[3][3] = 1.0f;
}

void
nj_mat_object(nj_mat4* out, const float pos[3], const float scl[3], const int32_t rot[3]) {
    /* T * S * Rx * Ry * Rz */
    mat3 rx, ry, rz, r;
    rot3_x(&rx, (float)rot[0]);
    rot3_y(&ry, (float)rot[1]);
    rot3_z(&rz, (float)rot[2]);
    mul3(&r, &rx, &ry);
    mul3(&r, &r, &rz);
    for (int i = 0; i < 3; i++) { /* S on the left scales the rows */
        for (int j = 0; j < 3; j++) {
            r.m[i][j] *= scl[i];
        }
    }
    static const float one[3] = {1.0f, 1.0f, 1.0f};
    compose(out, &r, one, pos);
}

void
nj_node_matrix(const nj_node* n, const float* pos_override, const float* ang_override, const float* scl_override,
               nj_mat4* out) {
    float pos[3] = {n->pos.x, n->pos.y, n->pos.z};
    float scl[3] = {n->scl.x, n->scl.y, n->scl.z};
    float ang[3] = {(float)n->ang[0], (float)n->ang[1], (float)n->ang[2]};
    if (pos_override) {
        memcpy(pos, pos_override, sizeof(pos));
    }
    if (scl_override) {
        memcpy(scl, scl_override, sizeof(scl));
    }
    if (ang_override) {
        memcpy(ang, ang_override, sizeof(ang));
    }
    /* T * R * S, each part only if the node's eval flags allow it */
    static const float zero[3] = {0.0f, 0.0f, 0.0f}, one[3] = {1.0f, 1.0f, 1.0f};
    mat3 r = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    if (!(n->eval & NJ_EVAL_NO_ROTATE)) {
        mat3 rx, ry, rz;
        /* whole Ninja units, as the original's (int32_t) conversion of the angles */
        rot3_x(&rx, (float)(int32_t)ang[0]);
        rot3_y(&ry, (float)(int32_t)ang[1]);
        rot3_z(&rz, (float)(int32_t)ang[2]);
        if (n->eval & NJ_EVAL_ZXY) {
            mul3(&r, &ry, &rx);
            mul3(&r, &r, &rz);
        } else {
            mul3(&r, &rz, &ry);
            mul3(&r, &r, &rx);
        }
    }
    compose(out, &r, (n->eval & NJ_EVAL_NO_SCALE) ? one : scl, (n->eval & NJ_EVAL_NO_TRANSLATE) ? zero : pos);
}

void
nj_object_pose(const nj_object* obj, const nj_motion* motion, float frame, nj_mat4* world) {
    for (int i = 0; i < obj->count; i++) {
        float p[3], a[3], s[3];
        int hp = 0, ha = 0, hs = 0;
        if (motion && i < motion->count) {
            hp = sample_channel(&motion->nodes[i].pos, frame, 0, p);
            ha = sample_channel(&motion->nodes[i].ang, frame, 1, a);
            hs = sample_channel(&motion->nodes[i].scl, frame, 0, s);
        }
        nj_mat4 local;
        nj_node_matrix(&obj->nodes[i], hp ? p : NULL, ha ? a : NULL, hs ? s : NULL, &local);
        if (obj->nodes[i].parent >= 0) {
            nj_mat_mul(&world[i], &world[obj->nodes[i].parent], &local);
        } else {
            world[i] = local;
        }
    }
}
