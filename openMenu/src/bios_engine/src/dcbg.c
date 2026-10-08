/* Adapted from the BIOS-menu decompile project (background/dcbg.c). Constants and maths
 * are the reconstructed ones; no ROM data is contained here. */
/*
 * dcbg.c - Dreamcast BIOS menu background, reference implementation.
 * Reconstructed from the boot ROM (KATANA KABUTO Ver.1.01d):
 *   grid_mesh_build_rest  8C015C60     mesh_deform_mode2 (ripple) 8C015F84
 *   mesh_deform_mode3 (swirl) 8C016202  grid_mesh_draw 8C015E60
 *   vertex projection 8C027E30          scripts 8C070C20 / 8C070CB4
 */
#include "dcbg.h"
#include <math.h>

#define NJ_TWO_PI_ROM 6.283183574676514f      /* 0x8C015E44 */

#if defined(_arch_dreamcast)
/* fsca: same table-free hardware sin/cos the BIOS uses */
static inline void njsincos(int a, float *s, float *c) {
    __asm__("lds %2,fpul\n\tfsca fpul,dr0\n\tfmov fr0,%0\n\tfmov fr1,%1"
            : "=f"(*s), "=f"(*c) : "r"(a & 0xffff) : "fpul", "fr0", "fr1");
}
#else
static inline void njsincos(int a, float *s, float *c) {
    double r = (double)(a & 0xffff) * (6.283185307179586 / 65536.0);
    *s = (float)sin(r); *c = (float)cos(r);
}
#endif
static inline float njsin(int a) { float s, c; njsincos(a, &s, &c); return s; }

static void mesh_build(dcbg_mesh *m, int cw, int ch, int cols, int rows) {
    int i, j, half_w = (cols * cw) / 2, half_h = (rows * ch) / 2;
    m->cols = cols; m->rows = rows; m->tex = 7;
    for (i = 0; i <= cols; i++)
        for (j = 0; j <= rows; j++) {
            int k = i * (rows + 1) + j;
            float x = (float)(i * cw - half_w) / 10.0f;
            float y = (float)(j * ch - half_h) / 10.0f;
            m->rx[k] = x; m->ry[k] = y;
            m->rr[k] = sqrtf(x * x + y * y);
            m->ra[k] = (uint16_t)(int)((atan2f(y, x) * 65536.0f) / NJ_TWO_PI_ROM);
            m->ru[k] = (float)i / (float)cols; m->rv[k] = (float)j / (float)rows;
            m->x[k] = x; m->y[k] = y; m->z[k] = 0.0f;
            m->u[k] = m->ru[k]; m->v[k] = m->rv[k];
            m->argb[k] = 0x00FFFFFF;
        }
}

/* mesh_deform_mode2: water ripple */
static void mesh_ripple(dcbg_mesh *m, int var0, int var6, int custom, int hidden) {
    const float K1 = custom ? 180.0f : 250.0f, K2 = custom ? 13.0f : 17.0f, AMP = custom ? 0.1f : 0.2f;
    int k, n = (m->cols + 1) * (m->rows + 1);
    for (k = 0; k < n; k++) {
        float r = m->rr[k];
        float z = njsin((int)(r * 10000.0f) + var0) * AMP;
        int a;
        uint32_t rgb;
        m->z[k] = z;
        if (!hidden) {
            a = (int)(((float)((short)(int)(z * K1) + 150) - r * K2) - (float)var6);
            m->y[k] = m->ry[k]; m->u[k] = m->ru[k]; m->v[k] = m->rv[k];
            rgb = 0xFFFFFF;
        } else {
            a = (int)(((float)((short)(int)(z * 250.0f) + 170) - r * 16.0f) - (float)var6);
            m->y[k] = 0.7f * m->ry[k] + -12.0f;
            m->u[k] = m->ru[k] * 0.625f; m->v[k] = m->rv[k] * 0.9375f;
            rgb = (200u << 16) | (220u << 8) | 0xFF;
        }
        if (a < 0) a = 0; else if (a > 255) a = 255;
        m->argb[k] = ((uint32_t)a << 24) | rgb;
    }
    m->tex = hidden ? 0 : custom ? 666 : 7;
}

/* mesh_deform_mode3: swirl */
static void mesh_swirl(dcbg_mesh *m, int var6, int custom) {
    const float A = custom ? 14.0f : 19.0f, B = custom ? 20.5f : 22.0f, Z0 = custom ? 0.5f : 0.0f;
    int k, n = (m->cols + 1) * (m->rows + 1);
    for (k = 0; k < n; k++) {
        float r = m->rr[k], s, c, d, fa;
        int ang = (int)((float)m->ra[k] + 38000.0f / r) & 0xffff;
        njsincos(ang, &s, &c);
        m->x[k] = s * r;
        m->y[k] = c * r;
        d = 700.0f / (r * 64.0f);
        m->z[k] = Z0 - d;
        fa = ((float)var6 + A * (d * d)) + B * r;
        m->argb[k] = (m->argb[k] & 0x00FFFFFF) | ((uint32_t)(255.0f > fa ? (0xFF - (int)fa) & 0xFF : 0) << 24);
    }
    m->tex = custom ? 666 : 7;
}

void dcbg_init(dcbg_state *s, int custom_texture, int hidden_mode) {
    dcbg_obj *o;
    s->custom = custom_texture; s->hidden = hidden_mode; s->frame = 0;
    mesh_build(&s->swirl, 15, 15, 15, 15);
    mesh_build(&s->water, 15, 15, 12, 12);
    o = &s->swirl_obj;   /* script @8C070C20 (values are 24.8 fixed point in the ROM) */
    o->pos[0] = 0; o->pos[1] = 3000 / 256.0f; o->pos[2] = -100000 / 256.0f;
    o->scl[0] = 900 / 256.0f; o->scl[1] = 1500 / 256.0f; o->scl[2] = 900 / 256.0f;
    o->rx = 0x3800; o->rz = 0; o->rz_step = -16;
    o = &s->water_obj;   /* script @8C070CB4 */
    o->pos[0] = 0; o->pos[1] = -3200 / 256.0f; o->pos[2] = -100000 / 256.0f;
    o->scl[0] = o->scl[1] = o->scl[2] = 1000 / 256.0f;
    o->rx = 0xC800; o->rz = 0; o->rz_step = 0;
    s->sw_var6 = 240; s->sw_var7 = 0; s->sw_intro = 20; s->sw_phase = 0;
    s->wa_var0 = 0; s->wa_var6 = 240; s->wa_var7 = 0; s->wa_intro = 20; s->wa_phase = 0;
}

void dcbg_fade_out(dcbg_state *s) { s->sw_var7 = 1; s->wa_var7 = 1; }

void dcbg_step(dcbg_state *s) {
    /* object 0x120 */
    if (s->sw_intro > 0) { s->sw_var6 -= 12; mesh_swirl(&s->swirl, s->sw_var6, s->custom); s->sw_intro--; }
    else if (s->sw_phase == 0) { if (s->sw_var7) s->sw_var6 += 20; s->sw_phase = 1; }
    else { mesh_swirl(&s->swirl, s->sw_var6, s->custom); s->sw_phase = 0; }
    /* object 0x121 */
    if (s->wa_intro > 0) {
        s->wa_var6 -= 12; s->wa_var0 -= 250;
        mesh_ripple(&s->water, s->wa_var0, s->wa_var6, s->custom, s->hidden); s->wa_intro--;
    } else if (s->wa_phase == 0) { if (s->wa_var7) s->wa_var6 += 20; s->wa_phase = 1; }
    else {
        s->wa_var0 -= 250;
        mesh_ripple(&s->water, s->wa_var0, s->wa_var6, s->custom, s->hidden); s->wa_phase = 0;
    }
    s->swirl_obj.rz += s->swirl_obj.rz_step;
    s->frame++;
}

void dcbg_gradient(const dcbg_state *s, uint32_t *top, uint32_t *bottom) {
    if (!s->hidden) { *top = 0xFFB0D0D0; *bottom = 0xFF4060C0; }
    else            { *top = 0xFF00A0C0; *bottom = 0xFF002060; }
}

int dcbg_project(const dcbg_mesh *m, const dcbg_obj *o, dcbg_vertex *out) {
    float sx, cx, sz, cz;
    float R[3][3];
    int k, n = (m->cols + 1) * (m->rows + 1);
    njsincos(o->rx, &sx, &cx);
    njsincos(o->rz, &sz, &cz);
    /* Ninja: M = T * S * Rx * Ry * Rz (Ry = 0 here); view = scale(-1,-1,1) */
    R[0][0] = cz;      R[0][1] = -sz;     R[0][2] = 0;
    R[1][0] = cx * sz; R[1][1] = cx * cz; R[1][2] = -sx;
    R[2][0] = sx * sz; R[2][1] = sx * cz; R[2][2] = cx;
    for (k = 0; k < n; k++) {
        float x = m->x[k], y = m->y[k], z = m->z[k];
        float wx = o->scl[0] * (R[0][0] * x + R[0][1] * y + R[0][2] * z) + o->pos[0];
        float wy = o->scl[1] * (R[1][0] * x + R[1][1] * y + R[1][2] * z) + o->pos[1];
        float wz = o->scl[2] * (R[2][0] * x + R[2][1] * y + R[2][2] * z) + o->pos[2];
        float iw = 1.0f / -wz;
        out[k].sx = 320.0f + 4000.0f * wx * iw;
        out[k].sy = 240.0f - 4000.0f * wy * iw;
        out[k].invw = iw;
        out[k].u = m->u[k]; out[k].v = m->v[k];
        out[k].argb = m->argb[k];
    }
    return n;
}

#ifdef DCBG_TEST
/* cc -DDCBG_TEST dcbg.c -lm && ./a.out 200  -> dumps the projected vertices of frame N */
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    static dcbg_state s;
    static dcbg_vertex v[(DCBG_MAX_COLS + 1) * (DCBG_MAX_ROWS + 1)];
    int i, n, f = argc > 1 ? atoi(argv[1]) : 200;
    dcbg_init(&s, argc > 2 ? atoi(argv[2]) : 0, argc > 3 ? atoi(argv[3]) : 0);
    for (i = 0; i < f; i++) dcbg_step(&s);
    n = dcbg_project(&s.swirl, &s.swirl_obj, v);
    for (i = 0; i < n; i++) printf("S %d %.3f %.3f %.6f %.4f %.4f %08x\n", i, v[i].sx, v[i].sy, v[i].invw, v[i].u, v[i].v, v[i].argb);
    n = dcbg_project(&s.water, &s.water_obj, v);
    for (i = 0; i < n; i++) printf("W %d %.3f %.3f %.6f %.4f %.4f %08x\n", i, v[i].sx, v[i].sy, v[i].invw, v[i].u, v[i].v, v[i].argb);
    return 0;
}
#endif
