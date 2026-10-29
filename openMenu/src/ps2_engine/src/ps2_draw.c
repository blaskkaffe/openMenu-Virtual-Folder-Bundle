/* ps2_draw: drawing helpers of the PS2-style menu, see ps2_draw.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ps2_draw.h"

#define MAX_MODEL_TRIS 256
#define CAMERA_DIST 9.0f /* default distance in model units between the eye and the model's centre plane */
#define DRAW_Z 0.5f

void
ps2d_init(ps2_draw* d, const ps2_pack* pack, const ps2_sink* sink) {
    memset(d, 0, sizeof(*d));
    d->pack = pack;
    d->sink = *sink;
    d->alpha = 1.0f;
}

uint32_t
ps2_fade(uint32_t argb, float f) {
    float a = (float)(argb >> 24) * f;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return ((uint32_t)(a + 0.5f) << 24) | (argb & 0x00FFFFFFu);
}

uint32_t
ps2_mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int sh = 0; sh <= 24; sh += 8) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        out |= (uint32_t)(x + (y - x) * t + 0.5f) << sh;
    }
    return out;
}

static void
emit(ps2_draw* d, const ps2_vtx v[3], int tex, int blend) {
    d->tris++;
    d->sink.tri(d->sink.user, v, tex, blend);
}

static void
emit_quad(ps2_draw* d, const ps2_vtx q[4], int tex, int blend) {
    /* q: top-left, top-right, bottom-left, bottom-right */
    ps2_vtx t0[3] = {q[0], q[1], q[2]};
    ps2_vtx t1[3] = {q[1], q[3], q[2]};
    emit(d, t0, tex, blend);
    emit(d, t1, tex, blend);
}

static ps2_vtx
vtx(const ps2_draw* d, float x, float y, float u, float v, uint32_t argb) {
    ps2_vtx r;
    r.x = x + d->ox;
    r.y = y + d->oy;
    r.z = DRAW_Z;
    r.u = u;
    r.v = v;
    r.argb = ps2_fade(argb, d->alpha);
    return r;
}

/* uv of the picture (0..1) -> uv of the texture */
static void
pic_uv(const ps2_draw* d, int tex, float* su, float* sv) {
    *su = 1.0f;
    *sv = 1.0f;
    if (tex >= 0 && tex < d->pack->ntex) {
        const ps2_tex_info* t = &d->pack->tex[tex];
        *su = (float)t->pic_w / (float)t->w;
        *sv = (float)t->pic_h / (float)t->h;
    }
}

void
ps2d_quad(ps2_draw* d, int tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
          uint32_t argb, int blend) {
    float su, sv;
    pic_uv(d, tex, &su, &sv);
    ps2_vtx q[4] = {vtx(d, x0, y0, u0 * su, v0 * sv, argb), vtx(d, x1, y0, u1 * su, v0 * sv, argb),
                    vtx(d, x0, y1, u0 * su, v1 * sv, argb), vtx(d, x1, y1, u1 * su, v1 * sv, argb)};
    emit_quad(d, q, tex, blend);
}

void
ps2d_sprite(ps2_draw* d, int tex, float cx, float cy, float w, float h, float angle, uint32_t argb, int blend) {
    float su, sv;
    pic_uv(d, tex, &su, &sv);
    float c = cosf(angle), s = sinf(angle);
    static const float cxs[4] = {-0.5f, 0.5f, -0.5f, 0.5f};
    static const float cys[4] = {-0.5f, -0.5f, 0.5f, 0.5f};
    ps2_vtx q[4];
    for (int i = 0; i < 4; i++) {
        float px = cxs[i] * w, py = cys[i] * h;
        q[i] = vtx(d, cx + px * c - py * s, cy + px * s + py * c, (cxs[i] + 0.5f) * su, (cys[i] + 0.5f) * sv, argb);
    }
    emit_quad(d, q, tex, blend);
}

void
ps2d_quad4(ps2_draw* d, int tex, const float x[4], const float y[4], const float u[4], const float v[4], const uint32_t argb[4],
           int blend) {
    float su, sv;
    pic_uv(d, tex, &su, &sv);
    ps2_vtx q[4];
    for (int i = 0; i < 4; i++) {
        q[i] = vtx(d, x[i], y[i], u[i] * su, v[i] * sv, argb[i]);
    }
    emit_quad(d, q, tex, blend);
}

void
ps2d_line(ps2_draw* d, float x0, float y0, float x1, float y1, float width, uint32_t argb, int blend) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {
        return;
    }
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    float xs[4] = {x0 + nx, x0 - nx, x1 + nx, x1 - nx};
    float ys[4] = {y0 + ny, y0 - ny, y1 + ny, y1 - ny};
    float z[4] = {0, 0, 0, 0};
    uint32_t c[4] = {argb, argb, argb, argb};
    ps2d_quad4(d, PS2_TEX_NONE, xs, ys, z, z, c, blend);
}

void
ps2d_gradient(ps2_draw* d, float x, float y, float w, float h, uint32_t top, uint32_t bottom, int blend) {
    ps2_vtx q[4] = {vtx(d, x, y, 0, 0, top), vtx(d, x + w, y, 0, 0, top), vtx(d, x, y + h, 0, 0, bottom),
                    vtx(d, x + w, y + h, 0, 0, bottom)};
    emit_quad(d, q, PS2_TEX_NONE, blend);
}

void
ps2d_gradient_h(ps2_draw* d, float x, float y, float w, float h, uint32_t left, uint32_t right, int blend) {
    ps2_vtx q[4] = {vtx(d, x, y, 0, 0, left), vtx(d, x + w, y, 0, 0, right), vtx(d, x, y + h, 0, 0, left),
                    vtx(d, x + w, y + h, 0, 0, right)};
    emit_quad(d, q, PS2_TEX_NONE, blend);
}

void
ps2d_rect(ps2_draw* d, float x, float y, float w, float h, uint32_t argb) {
    ps2d_gradient(d, x, y, w, h, argb, argb, PS2_BLEND_ALPHA);
}

/* ---- Text ------------------------------------------------------------------------------- */

/* Index of the glyph for a character; characters the font lacks show as '?'. */
static int
glyph_index(const ps2_pack* p, unsigned char c) {
    int i = (int)c - p->glyph_first;
    if (i < 0 || i >= p->glyph_count || (p->glyph[i].advance == 0 && c != ' ')) {
        i = '?' - p->glyph_first;
    }
    return i >= 0 && i < p->glyph_count ? i : -1;
}

static const ps2_glyph*
glyph_for(const ps2_pack* p, unsigned char c) {
    int i = glyph_index(p, c);
    return i >= 0 ? &p->glyph[i] : NULL;
}

float
ps2d_text_width(const ps2_draw* d, const char* s, float scale) {
    float w = 0.0f;
    for (; *s; s++) {
        const ps2_glyph* g = glyph_for(d->pack, (unsigned char)*s);
        w += g ? (float)g->advance : 0.0f;
    }
    return w * scale;
}

void
ps2d_text(ps2_draw* d, const char* s, float x, float y, float scale, uint32_t argb, int align) {
    const ps2_pack* p = d->pack;
    if (!s || !*s || p->font_tex < 0) {
        return;
    }
    const ps2_tex_info* ft = &p->tex[p->font_tex];
    float w = ps2d_text_width(d, s, scale);
    if (align == PS2_TEXT_CENTER) {
        x -= w / 2.0f;
    } else if (align == PS2_TEXT_RIGHT) {
        x -= w;
    }
    int cols = ft->pic_w / p->cell_w;
    float cw = (float)p->cell_w, ch = (float)p->cell_h;
    for (; *s; s++) {
        const ps2_glyph* g = glyph_for(p, (unsigned char)*s);
        if (!g) {
            continue;
        }
        if (g->width) {
            int ci = glyph_index(p, (unsigned char)*s);
            int col = ci % cols, row = ci / cols;
            float u0 = ((float)col * cw + (float)g->left) / (float)ft->w;
            float u1 = ((float)col * cw + (float)g->left + (float)g->width) / (float)ft->w;
            float v0 = ((float)row * ch) / (float)ft->h;
            float v1 = ((float)(row + 1) * ch) / (float)ft->h;
            /* quad in texture space: bypass the picture scaling of ps2d_quad() */
            ps2_vtx q[4] = {vtx(d, x, y, u0, v0, argb), vtx(d, x + (float)g->width * scale, y, u1, v0, argb),
                            vtx(d, x, y + ch * scale, u0, v1, argb),
                            vtx(d, x + (float)g->width * scale, y + ch * scale, u1, v1, argb)};
            emit_quad(d, q, p->font_tex, PS2_BLEND_ALPHA);
        }
        x += (float)g->advance * scale;
    }
}

void
ps2d_text_fit(ps2_draw* d, const char* s, float x, float y, float scale, uint32_t argb, int align, float max_w) {
    char buf[96];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    if (ps2d_text_width(d, buf, scale) > max_w) {
        while (n > 2) {
            n--;
            buf[n] = '\0';
            char tmp[100];
            memcpy(tmp, buf, n);
            memcpy(tmp + n, "..", 3);
            if (ps2d_text_width(d, tmp, scale) <= max_w) {
                memcpy(buf, tmp, n + 3);
                break;
            }
        }
    }
    ps2d_text(d, buf, x, y, scale, argb, align);
}

/* ---- Models ----------------------------------------------------------------------------- */

typedef struct {
    float depth;
    int tri;
} tri_key;

static int
by_depth_far_first(const void* a, const void* b) {
    float da = ((const tri_key*)a)->depth, db = ((const tri_key*)b)->depth;
    return da < db ? 1 : (da > db ? -1 : 0);
}

int
ps2d_model(ps2_draw* d, int model, const ps2_model_view* mv) {
    if (model < 0 || model >= d->pack->nmodels) {
        return 0;
    }
    const ps2_model_info* m = &d->pack->model[model];
    int ntris = m->nverts / 3;
    if (ntris > MAX_MODEL_TRIS) {
        ntris = MAX_MODEL_TRIS;
    }
    float cy_ = cosf(mv->yaw), sy = sinf(mv->yaw), cp = cosf(mv->pitch), sp = sinf(mv->pitch);
    float cr = cosf(mv->roll), sr = sinf(mv->roll);
    float cam = mv->dist > 0.0f ? mv->dist : CAMERA_DIST;
    /* light from the upper left front */
    const float lx = -0.35f, ly = 0.55f, lz = 0.76f;

    struct {
        float x, y, depth, nz, shade;
    } pv[MAX_MODEL_TRIS * 3];
    tri_key keys[MAX_MODEL_TRIS];
    int nkeys = 0;

    for (int t = 0; t < ntris; t++) {
        float nzsum = 0.0f, dsum = 0.0f;
        for (int k = 0; k < 3; k++) {
            const float* v = m->v + (size_t)(t * 3 + k) * 8;
            float x = v[0], y = v[1] + mv->y_offset, z = v[2];
            /* yaw about the vertical axis, then pitch about the horizontal one */
            float x1 = x * cy_ + z * sy, z1 = -x * sy + z * cy_;
            float y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
            float x2 = x1 * cr - y2 * sr;
            y2 = x1 * sr + y2 * cr;
            x1 = x2;
            float nx = v[3] * cy_ + v[5] * sy, nz1 = -v[3] * sy + v[5] * cy_;
            float ny2 = v[4] * cp - nz1 * sp, nz2 = v[4] * sp + nz1 * cp;
            float nx2 = nx * cr - ny2 * sr;
            ny2 = nx * sr + ny2 * cr;
            nx = nx2;
            float depth = cam - z2; /* distance from the eye */
            float persp = cam / depth;
            int i = t * 3 + k;
            pv[i].x = mv->cx + x1 * mv->scale * persp;
            pv[i].y = mv->cy - y2 * mv->scale * persp;
            pv[i].depth = depth;
            pv[i].nz = nz2;
            float diff = nx * lx + ny2 * ly + nz2 * lz;
            if (diff < 0.0f) diff = 0.0f;
            float shade = 0.62f + 0.48f * diff;
            if (mv->spec > 0.0f) {
                float h = diff * diff;
                shade += mv->spec * h * h;
            }
            pv[i].shade = shade;
            nzsum += nz2;
            dsum += depth;
        }
        if (nzsum / 3.0f < -0.02f) {
            continue; /* faces away from the viewer */
        }
        keys[nkeys].depth = dsum / 3.0f;
        keys[nkeys].tri = t;
        nkeys++;
    }
    qsort(keys, (size_t)nkeys, sizeof(keys[0]), by_depth_far_first);

    float su = 1.0f, sv = 1.0f;
    if (mv->tex >= 0 && mv->tex < d->pack->ntex) {
        pic_uv(d, mv->tex, &su, &sv);
    }
    for (int n = 0; n < nkeys; n++) {
        int t = keys[n].tri;
        ps2_vtx out[3];
        for (int k = 0; k < 3; k++) {
            const float* v = m->v + (size_t)(t * 3 + k) * 8;
            int i = t * 3 + k;
            float u = v[6], w = v[7];
            if (mv->planar_uv) {
                float ax = fabsf(v[3]), ay = fabsf(v[4]), az = fabsf(v[5]);
                float x = v[0], y = v[1], z = v[2];
                if (az >= ax && az >= ay) {
                    u = x * 0.5f / 1.04f + 0.5f;
                    if (v[5] < 0) u = 1.0f - u;
                    w = 1.0f - y / 2.08f;
                } else if (ax >= ay) {
                    u = z * 0.5f / 1.04f + 0.5f;
                    if (v[3] > 0) u = 1.0f - u;
                    w = 1.0f - y / 2.08f;
                } else {
                    u = x * 0.5f / 1.04f + 0.5f;
                    w = z * 0.5f / 1.04f + 0.5f;
                }
            }
            float s = pv[i].shade;
            uint32_t tint = mv->tint;
            int r = (int)(((tint >> 16) & 255) * s), g = (int)(((tint >> 8) & 255) * s), b = (int)((tint & 255) * s);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            out[k].x = pv[i].x + d->ox;
            out[k].y = pv[i].y + d->oy;
            out[k].z = DRAW_Z;
            out[k].u = u * su;
            out[k].v = w * sv;
            out[k].argb = ps2_fade((ps2_argb(255, r, g, b) & 0x00FFFFFFu) | (tint & 0xFF000000u), d->alpha);
        }
        emit(d, out, mv->tex, PS2_BLEND_ALPHA);
    }
    return nkeys;
}

/* ---- Glass cube ------------------------------------------------------------------------- */

void
ps2d_glass_cube(ps2_draw* d, const ps2_glass* g) {
    static const int face[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    static const float nrm[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    float cy_ = cosf(g->yaw), sy = sinf(g->yaw), cp = cosf(g->pitch), sp = sinf(g->pitch), cr = cosf(g->roll), sr = sinf(g->roll);
    float cam = g->dist > 0.0f ? g->dist : CAMERA_DIST;
    float px[8], py[8], pz[8];
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? 1.0f : -1.0f, y = (i & 2) ? 1.0f : -1.0f, z = (i & 4) ? 1.0f : -1.0f;
        float x1 = x * cy_ + z * sy, z1 = -x * sy + z * cy_;
        float y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
        float x2 = x1 * cr - y2 * sr;
        y2 = x1 * sr + y2 * cr;
        float persp = cam / (cam - z2);
        px[i] = g->cx + x2 * g->scale * persp;
        py[i] = g->cy - y2 * g->scale * persp;
        pz[i] = z2;
    }
    struct {
        float depth, nz;
        int f;
    } order[6];
    for (int f = 0; f < 6; f++) {
        float nx = nrm[f][0] * cy_ + nrm[f][2] * sy, nz1 = -nrm[f][0] * sy + nrm[f][2] * cy_;
        float ny2 = nrm[f][1] * cp - nz1 * sp, nz2 = nrm[f][1] * sp + nz1 * cp;
        (void)nx;
        (void)ny2;
        order[f].nz = nz2;
        order[f].f = f;
        order[f].depth = (pz[face[f][0]] + pz[face[f][1]] + pz[face[f][2]] + pz[face[f][3]]) / 4.0f;
    }
    for (int i = 1; i < 6; i++) { /* far to near */
        int j = i;
        float dd = order[i].depth, nn = order[i].nz;
        int ff = order[i].f;
        while (j > 0 && order[j - 1].depth > dd) {
            order[j] = order[j - 1];
            j--;
        }
        order[j].depth = dd;
        order[j].nz = nn;
        order[j].f = ff;
    }
    for (int k = 0; k < 6; k++) {
        int f = order[k].f;
        uint32_t c = order[k].nz > 0.0f ? g->front : g->back;
        float xs[4] = {px[face[f][0]], px[face[f][1]], px[face[f][3]], px[face[f][2]]};
        float ys[4] = {py[face[f][0]], py[face[f][1]], py[face[f][3]], py[face[f][2]]};
        float z[4] = {0, 0, 0, 0};
        /* a little shading by the facing so the faces can be told apart */
        float shade = 0.75f + 0.25f * fabsf(order[k].nz);
        uint32_t cc = ps2_argb((int)(c >> 24), (int)(((c >> 16) & 255) * shade), (int)(((c >> 8) & 255) * shade), (int)((c & 255) * shade));
        uint32_t cs[4] = {cc, cc, cc, cc};
        ps2d_quad4(d, PS2_TEX_NONE, xs, ys, z, z, cs, PS2_BLEND_ALPHA);
    }
    static const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {3, 7}, {2, 6}};
    for (int e = 0; e < 12; e++) {
        int a = edges[e][0], b = edges[e][1];
        float depth = (pz[a] + pz[b]) * 0.5f;
        float f = depth > 0.0f ? 1.0f : 0.55f; /* the far edges seen through the glass are dimmer */
        ps2d_line(d, px[a], py[a], px[b], py[b], g->edge_w, ps2_fade(g->edge, f), PS2_BLEND_ALPHA);
    }
}
